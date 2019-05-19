/**
 * @file
 * libavformat API example.
 *
 * Output a media file in any supported libavformat format. The default
 * codecs are used.
 * @example muxing.c
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <structmember.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <libavformat/avformat.h>
#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/log.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavutil/timestamp.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#define SCALE_FLAGS SWS_BICUBIC

// a wrapper around a single output AVStream
typedef struct OutputStream {
    AVStream *st;
    AVCodecContext *encoding_context;

    /* pts of the next frame that will be generated */
    int64_t next_pts;

    AVFrame *frame;
    AVFrame *tmp_frame;

    struct SwsContext *sws_ctx;
} OutputStream;

/* FFmpegWriter type code */
typedef struct {
    PyObject_HEAD
    OutputStream video_stream;
    AVFormatContext *oc;
    AVPacket *pkt;
    AVCodec *video_codec;
    int debug;
} FFmpegWriterObject;

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}

/* Add an output stream. */
static void add_stream(OutputStream *ost, AVFormatContext *oc,
                       AVCodec **codec,
                       enum AVCodecID codec_id,
                       int height, int width, int fps) {
    AVCodecContext *c;

    /* find the encoder */
    *codec = avcodec_find_encoder(codec_id);
    if (!(*codec)) {
        fprintf(stderr, "Could not find encoder for '%s'\n",
                avcodec_get_name(codec_id));
        exit(1);
    }

    // allocate AVFormatContext
    ost->st = avformat_new_stream(oc, NULL);
    if (!ost->st) {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }
    ost->st->id = oc->nb_streams-1;
    c = avcodec_alloc_context3(*codec);
    if (!c) {
        fprintf(stderr, "Could not alloc an encoding context\n");
        exit(1);
    }

    // configure AVFormatContext
    ost->encoding_context = c;
    c->codec_id = codec_id;

    /* Resolution must be a multiple of two. */
    c->width    = width;
    c->height   = height;
    /* timebase: This is the fundamental unit of time (in seconds) in terms
     * of which frame timestamps are represented. For fixed-fps content,
     * timebase should be 1/framerate and timestamp increments should be
     * identical to 1. */
    ost->st->time_base = (AVRational){ 1, fps };
    c->time_base       = ost->st->time_base;
    c->framerate       = (AVRational){ fps, 1 };

    c->pix_fmt       = AV_PIX_FMT_YUV420P;
    c->gop_size      = 10; /* emit one intra frame every twelve frames at most */
    // c->bit_rate = 400000;
    if (c->codec_id == AV_CODEC_ID_H264) {
        av_opt_set(c->priv_data, "preset", "ultrafast", 0);
    }

    // allocate SwsContext
    ost->sws_ctx = sws_getContext(
        ost->encoding_context->width,
        ost->encoding_context->height,
        AV_PIX_FMT_RGBA,
        ost->encoding_context->width,
        ost->encoding_context->height,
        c->pix_fmt,
        SCALE_FLAGS, NULL, NULL, NULL);
}

/**************************************************************/
/* video output */

static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
    AVFrame *picture;
    int ret;

    picture = av_frame_alloc();
    if (!picture)
        return NULL;

    picture->format = pix_fmt;
    picture->width  = width;
    picture->height = height;

    /* allocate the buffers for the frame data */
    ret = av_frame_get_buffer(picture, 32);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate frame data.\n");
        exit(1);
    }

    return picture;
}

static void open_video(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg)
{
    int ret;
    AVCodecContext *c = ost->encoding_context;
    AVDictionary *opt = NULL;

    av_dict_copy(&opt, opt_arg, 0);

    /* open the codec */
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        fprintf(stderr, "Could not open video codec: %s\n", av_err2str(ret));
        exit(1);
    }

    /* allocate and init a re-usable frame */
    ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
    if (!ost->frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }

    /* If the output format is not YUV420P, then a temporary YUV420P
     * picture is needed too. It is then converted to the required
     * output format. */
    ost->tmp_frame = alloc_picture(AV_PIX_FMT_RGBA, c->width, c->height);
    if (!ost->tmp_frame) {
        fprintf(stderr, "Could not allocate temporary picture\n");
        exit(1);
    }

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ost->st->codecpar, c);
    if (ret < 0) {
        fprintf(stderr, "Could not copy the stream parameters\n");
        exit(1);
    }
}

static void close_stream(AVFormatContext *oc, OutputStream *ost)
{
    avcodec_free_context(&ost->encoding_context);
    sws_freeContext(ost->sws_ctx);
    av_frame_free(&ost->frame);
    av_frame_free(&ost->tmp_frame);
}

/**************************************************************/
/* media file output */

static PyObject* makevid(FFmpegWriterObject *self, PyObject *args) {
    int ret;
    Py_ssize_t frame_length;
    if (!PyArg_ParseTuple(args,
                          "s#",
                          &self->video_stream.tmp_frame->data[0],
                          &frame_length)) {
        printf("Error parsing Python arguments\n");
        return Py_None;
    }
    // TODO: check frame length

    /* when we pass a frame to the encoder, it may keep a reference to it
     * internally; make sure we do not overwrite it here */
    if (av_frame_make_writable(self->video_stream.frame) < 0)
        exit(1);

    // convert RGBA pixels to YUV
    sws_scale(self->video_stream.sws_ctx,
              (const uint8_t * const *)self->video_stream.tmp_frame->data,
              self->video_stream.tmp_frame->linesize,
              0,
              self->video_stream.encoding_context->height,
              self->video_stream.frame->data,
              self->video_stream.frame->linesize);
    self->video_stream.frame->pts = self->video_stream.next_pts++;

    AVFrame *frame = self->video_stream.frame;

    /* send the frame to the encoder */
    if (self->debug && frame)
        printf("Send frame %3"PRId64"\n", frame->pts);

    ret = avcodec_send_frame(self->video_stream.encoding_context, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame for encoding\n");
        exit(1);
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(self->video_stream.encoding_context,
                                     self->pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            fprintf(stderr, "Error during encoding\n");
            exit(1);
        }

        /* rescale output packet timestamp values from codec to stream timebase */
        av_packet_rescale_ts(self->pkt,
                             self->video_stream.encoding_context->time_base,
                             self->video_stream.st->time_base);
        self->pkt->stream_index = self->video_stream.st->index;

        /* Write the compressed frame to the media file. */
        if (self->debug)
            log_packet(self->oc, self->pkt);
        ret = av_interleaved_write_frame(self->oc, self->pkt);
    }
    return Py_None;
}

static PyObject* finish(FFmpegWriterObject *self, PyObject *args) {
    AVFrame *frame = NULL;
    int ret;

    /* send the frame to the encoder */
    if (self->debug && frame)
        printf("Send frame %3"PRId64"\n", frame->pts);

    // enter draining mode
    ret = avcodec_send_frame(self->video_stream.encoding_context, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame for encoding\n");
        exit(1);
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(self->video_stream.encoding_context, self->pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            fprintf(stderr, "Error during encoding\n");
            exit(1);
        }

        /* rescale output packet timestamp values from codec to stream timebase */
        av_packet_rescale_ts(self->pkt, self->video_stream.encoding_context->time_base,
                self->video_stream.st->time_base);
        self->pkt->stream_index = self->video_stream.st->index;

        /* Write the compressed frame to the media file. */
        if (self->debug)
            log_packet(self->oc, self->pkt);
        ret = av_interleaved_write_frame(self->oc, self->pkt);
    }

    /* Write the trailer, if any. The trailer must be written before you
     * close the CodecContexts open when you wrote the header; otherwise
     * av_write_trailer() may try to use memory that was freed on
     * av_codec_close(). */
    av_write_trailer(self->oc);

    /* Close the codec. */
    close_stream(self->oc, &self->video_stream);

    if (!(self->oc->oformat->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_closep(&self->oc->pb);

    /* free the stream */
    avformat_free_context(self->oc);

    /* free the packet */
    av_packet_free(&self->pkt);

    return Py_None;
}

// Our Module Definition struct
static struct PyModuleDef ffmpeg_writer = {
    PyModuleDef_HEAD_INIT,
    .m_name = "ffmpeg_writer",
    .m_doc = "Test Module",
    .m_size = -1,
};

static PyMemberDef FFmpegWriter_members[] = {
    {NULL}
};

static PyObject* FFmpegWriter_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    FFmpegWriterObject *self;
    self = (FFmpegWriterObject *) type->tp_alloc(type, 0);
    if (self != NULL) {
        self->video_stream = (OutputStream) { NULL };
        self->oc = NULL;
        self->pkt = NULL;
        self->video_codec = NULL;
        self->debug = 0;
    }
    return (PyObject *) self;
}

static int FFmpegWriter_init(FFmpegWriterObject *self, PyObject *args, PyObject *kwds) {
    int ret;
    AVDictionary *opt = NULL;

    // Parse python arguments
    char *filename;
    int height, width, fps;
    static char *kwlist[] = {"filename", "height", "width", "fps", "debug", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "siii|p", kwlist, &filename,
                                     &height, &width, &fps, &self->debug))
        return -1;

    av_log_set_level(AV_LOG_QUIET);
    /* allocate the output media context */
    if (&self->oc != NULL) {
        avformat_free_context(self->oc);
    }
    avformat_alloc_output_context2(&self->oc, NULL, NULL, filename);
    if (!self->oc) {
        printf("Could not deduce output format from file extension.\n");
        avformat_alloc_output_context2(&self->oc, NULL, "libx264", filename);
    }
    if (!self->oc || self->oc->oformat->video_codec == AV_CODEC_ID_NONE)
        return -1;

    /* Add the video stream using the default format codecs
     * and initialize the codecs. */
    add_stream(&self->video_stream,
               self->oc,
               &self->video_codec,
               self->oc->oformat->video_codec,
               height, width, fps);

    /* Now that all the parameters are set, we can open the video codec and
     * allocate the necessary encode buffers. */
    open_video(self->oc, self->video_codec, &self->video_stream, opt);

    av_dump_format(self->oc, 0, filename, 1);

    /* open the output file, if needed */
    if (!(self->oc->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&self->oc->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open '%s': %s\n", filename,
                    av_err2str(ret));
            return -1;
        }
    }

    /* Write the stream header, if any. */
    ret = avformat_write_header(self->oc, &opt);
    if (ret < 0) {
        fprintf(stderr,
                "Error occurred when opening output file: %s\n",
                av_err2str(ret));
        return -1;
    }

    self->pkt = av_packet_alloc();
    return 0;
}

static void FFmpegWriter_dealloc(FFmpegWriterObject *self) {
    Py_TYPE(self)->tp_free((PyObject *) self);
}

static PyMethodDef FFmpegWriter_methods[] = {
    {"process_frame", (PyCFunction) makevid, METH_VARARGS, "return zero"},
    {"finish", (PyCFunction) finish, METH_NOARGS, "return zero"},
    {NULL}  /* sentinel */
};

static PyTypeObject FFmpegWriterType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "manimlib.ffmpeg_writer.FFmpegWriter",
    .tp_doc = "FFmpegWriter objects",
    .tp_basicsize = sizeof(FFmpegWriterObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = FFmpegWriter_new,
    .tp_init = (initproc) FFmpegWriter_init,
    .tp_dealloc = (destructor) FFmpegWriter_dealloc,
    .tp_members = FFmpegWriter_members,
    .tp_methods = FFmpegWriter_methods,
};
/* FFmpegWriter type code */

// Initializes our module using our above struct
PyMODINIT_FUNC PyInit_ffmpeg_writer(void)
{
    PyObject *m;
    m = PyModule_Create(&ffmpeg_writer);
    if (m == NULL)
        return NULL;

    // add class (type)
    if (PyType_Ready(&FFmpegWriterType) < 0)
        return NULL;
    Py_INCREF(&FFmpegWriterType);
    PyModule_AddObject(m, "FFmpegWriter", (PyObject *) &FFmpegWriterType);
    return m;
}
