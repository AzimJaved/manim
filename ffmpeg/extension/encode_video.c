/*
 * Copyright (c) 2003 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

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

#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#define STREAM_DURATION   10.0
#define STREAM_FRAME_RATE 60 /* 25 images/s */
#define STREAM_PIX_FMT    AV_PIX_FMT_YUV420P /* default pix_fmt */

#define SCALE_FLAGS SWS_BICUBIC

/* Custom type code */
typedef struct {
    PyObject_HEAD
    int frame_count;
    AVCodecContext* context;
} CustomObject;

// a wrapper around a single output AVStream
typedef struct OutputStream {
    AVStream *st;
    AVCodecContext *encoding_context;

    /* pts of the next frame that will be generated */
    int64_t next_pts;
    int samples_count;

    AVFrame *frame;
    AVFrame *tmp_frame;

    float t, tincr, tincr2;

    struct SwsContext *sws_ctx;
    struct SwrContext *swr_ctx;
} OutputStream;

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}

static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{
    /* rescale output packet timestamp values from codec to stream timebase */
    av_packet_rescale_ts(pkt, *time_base, st->time_base);
    pkt->stream_index = st->index;

    /* Write the compressed frame to the media file. */
    log_packet(fmt_ctx, pkt);
    return av_interleaved_write_frame(fmt_ctx, pkt);
}

/* Add an output stream. */
static void add_stream(OutputStream *ost, AVFormatContext *oc,
                       AVCodec **codec,
                       enum AVCodecID codec_id)
{
    AVCodecContext *c;
    int i;

    /* find the encoder */
    *codec = avcodec_find_encoder(codec_id);
    if (!(*codec)) {
        fprintf(stderr, "Could not find encoder for '%s'\n",
                avcodec_get_name(codec_id));
        exit(1);
    }

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
    ost->encoding_context = c;

    switch ((*codec)->type) {
    case AVMEDIA_TYPE_AUDIO:
        c->sample_fmt  = (*codec)->sample_fmts ?
            (*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        c->bit_rate    = 64000;
        c->sample_rate = 44100;
        if ((*codec)->supported_samplerates) {
            c->sample_rate = (*codec)->supported_samplerates[0];
            for (i = 0; (*codec)->supported_samplerates[i]; i++) {
                if ((*codec)->supported_samplerates[i] == 44100)
                    c->sample_rate = 44100;
            }
        }
        c->channels        = av_get_channel_layout_nb_channels(c->channel_layout);
        c->channel_layout = AV_CH_LAYOUT_STEREO;
        if ((*codec)->channel_layouts) {
            c->channel_layout = (*codec)->channel_layouts[0];
            for (i = 0; (*codec)->channel_layouts[i]; i++) {
                if ((*codec)->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
                    c->channel_layout = AV_CH_LAYOUT_STEREO;
            }
        }
        c->channels        = av_get_channel_layout_nb_channels(c->channel_layout);
        ost->st->time_base = (AVRational){ 1, c->sample_rate };
        break;

    case AVMEDIA_TYPE_VIDEO:
        c->codec_id = codec_id;

        c->bit_rate = 400000;
        /* Resolution must be a multiple of two. */
        c->width    = 2560;
        c->height   = 1440;
        /* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */
        ost->st->time_base = (AVRational){ 1, STREAM_FRAME_RATE };
        c->time_base       = ost->st->time_base;

        c->gop_size      = 12; /* emit one intra frame every twelve frames at most */
        c->pix_fmt       = AV_PIX_FMT_YUV420P;
        if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
            /* just for testing, we also add B-frames */
            c->max_b_frames = 2;
        }
        if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
            /* Needed to avoid using macroblocks in which some coeffs overflow.
             * This does not happen with normal video, it just happens here as
             * the motion of the chroma plane does not match the luma plane. */
            c->mb_decision = 2;
        }
    break;

    default:
        break;
    }

    /* Some formats want stream headers to be separate. */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
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
    av_frame_free(&ost->frame);
    av_frame_free(&ost->tmp_frame);
    sws_freeContext(ost->sws_ctx);
    swr_free(&ost->swr_ctx);
}

/**************************************************************/
/* media file output */

static PyObject* makevid(CustomObject *self, PyObject *args) {
    // read an rgba frame and file descriptor
    int file_descriptor;
    const char *frame_data;
    Py_ssize_t frame_length;
    int frame_number;
    if (!PyArg_ParseTuple(args, "is#i", &file_descriptor, &frame_data,
            &frame_length, &frame_number)) {
        return Py_None;
    }

    OutputStream video_st = { 0 };
    const char *filename;
    AVOutputFormat *fmt;
    AVFormatContext *oc;
    AVCodec *video_codec;
    int ret;
    int have_video = 0;
    int encode_video = 0;
    AVDictionary *opt = NULL;
    filename = "out.mp4";

    // for (int i = 2; i+1 < argc; i+=2) {
    //     if (!strcmp(argv[i], "-flags") || !strcmp(argv[i], "-fflags"))
    //         av_dict_set(&opt, argv[i]+1, argv[i+1], 0);
    // }

    /* allocate the output media context */
    avformat_alloc_output_context2(&oc, NULL, NULL, filename);
    if (!oc) {
        printf("Could not deduce output format from file extension: using MPEG.\n");
        avformat_alloc_output_context2(&oc, NULL, "mpeg", filename);
    }
    if (!oc)
        return Py_None;

    fmt = oc->oformat;

    /* Add the audio and video streams using the default format codecs
     * and initialize the codecs. */
    if (fmt->video_codec != AV_CODEC_ID_NONE) {
        add_stream(&video_st, oc, &video_codec, fmt->video_codec);
        have_video = 1;
        encode_video = 1;
    }

    /* Now that all the parameters are set, we can open the audio and
     * video codecs and allocate the necessary encode buffers. */
    if (have_video)
        open_video(oc, video_codec, &video_st, opt);

    av_dump_format(oc, 0, filename, 1);

    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&oc->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open '%s': %s\n", filename,
                    av_err2str(ret));
            return Py_None;
        }
    }

    /* Write the stream header, if any. */
    ret = avformat_write_header(oc, &opt);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file: %s\n",
                av_err2str(ret));
        return Py_None;
    }

    // while (encode_video || encode_audio) {
    int got_packet = 0;
    AVFrame *frame;
    AVPacket pkt = { 0 };
    for (int i = 0; i < 60; i++) {
        /* encode one video frame and send it to the muxer */
        /* check if we want to generate more frames */

        /* when we pass a frame to the encoder, it may keep a reference to it
         * internally; make sure we do not overwrite it here */
        if (av_frame_make_writable(video_st.frame) < 0)
            exit(1);

        
        uint64_t bytes_per_frame = 2560 * 1440 * 4;
        uint64_t offset = bytes_per_frame * ((uint64_t)video_st.next_pts % 60);
        printf("pts    = %" PRIu64 "\n", video_st.next_pts);
        printf("offset = %" PRIu64 "\n", offset);
        video_st.tmp_frame->data[0] = (uint8_t*)frame_data + offset;
        video_st.sws_ctx = sws_getContext(video_st.encoding_context->width,
                                      video_st.encoding_context->height,
                                      AV_PIX_FMT_RGBA,
                                      video_st.encoding_context->width,
                                      video_st.encoding_context->height,
                                      AV_PIX_FMT_YUV420P,
                                      SCALE_FLAGS, NULL, NULL, NULL);
        sws_scale(video_st.sws_ctx,
                  (const uint8_t * const *)video_st.tmp_frame->data,
                  video_st.tmp_frame->linesize,
                  0,
                  video_st.encoding_context->height,
                  video_st.frame->data,
                  video_st.frame->linesize);
        video_st.frame->pts = video_st.next_pts++;
        frame = video_st.frame;

        av_init_packet(&pkt);

        /* encode the image */
        ret = avcodec_encode_video2(video_st.encoding_context, &pkt, frame, &got_packet);
        if (ret < 0) {
            fprintf(stderr, "Error encoding video frame: %s\n", av_err2str(ret));
            exit(1);
        }

        if (got_packet) {
            /* rescale output packet timestamp values from codec to stream timebase */
            av_packet_rescale_ts(&pkt, video_st.encoding_context->time_base,
                    video_st.st->time_base);
            pkt.stream_index = video_st.st->index;

            /* Write the compressed frame to the media file. */
            log_packet(oc, &pkt);
            ret = av_interleaved_write_frame(oc, &pkt);
        } else {
            ret = 0;
        }

        if (ret < 0) {
            fprintf(stderr, "Error while writing video frame: %s\n", av_err2str(ret));
            exit(1);
        }
    }

    while (got_packet) {
        /* encode one video frame and send it to the muxer */
        /* check if we want to generate more frames */
        frame = NULL;

        av_init_packet(&pkt);

        /* encode the image */
        ret = avcodec_encode_video2(video_st.encoding_context, &pkt, frame, &got_packet);
        if (ret < 0) {
            fprintf(stderr, "Error encoding video frame: %s\n", av_err2str(ret));
            exit(1);
        }

        if (got_packet) {
            /* rescale output packet timestamp values from codec to stream timebase */
            av_packet_rescale_ts(&pkt, video_st.encoding_context->time_base,
                    video_st.st->time_base);
            pkt.stream_index = video_st.st->index;

            /* Write the compressed frame to the media file. */
            log_packet(oc, &pkt);
            ret = av_interleaved_write_frame(oc, &pkt);
        } else {
            ret = 0;
        }

        if (ret < 0) {
            fprintf(stderr, "Error while writing video frame: %s\n", av_err2str(ret));
            exit(1);
        }
    }

    /* Write the trailer, if any. The trailer must be written before you
     * close the CodecContexts open when you wrote the header; otherwise
     * av_write_trailer() may try to use memory that was freed on
     * av_codec_close(). */
    av_write_trailer(oc);

    /* Close each codec. */
    if (have_video)
        close_stream(oc, &video_st);

    if (!(fmt->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_closep(&oc->pb);

    /* free the stream */
    avformat_free_context(oc);

    return Py_None;
}

/*
 * Copyright (c) 2001 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * video encoding with libavcodec API example
 *
 * @example encode_video.c
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <structmember.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>

#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

static void encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt, FILE* f)
{
    int ret;

    /* send the frame to the encoder */
    if (frame)
        printf("Send frame %3"PRId64"\n", frame->pts);

    ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending a frame for encoding\n");
        exit(1);
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error during encoding\n");
            exit(1);
        }

        printf("Write packet %3"PRId64" (size=%5d)\n", pkt->pts, pkt->size);
        fwrite(pkt->data, 1, pkt->size, f);
        av_packet_unref(pkt);
    }
}

void render(int file_descriptor, const uint8_t* frame_data, int frame_length,
            AVCodecContext* ctx, int finish, int i) {
    if (finish) {
        // get a packet
        AVPacket *pkt = av_packet_alloc();
        if (!pkt)
            exit(1);

        FILE* f = fopen("out.mp4", "a");
        if (!f) {
            fprintf(stderr, "Could not open file\n");
            exit(1);
        }
        /* flush the encoder */
        encode(ctx, NULL, pkt, f);

        /* add sequence end code to have a real MPEG file */
        uint8_t endcode[] = { 0, 0, 1, 0xb7 };
        fwrite(endcode, 1, sizeof(endcode), f);
        fclose(f);

        // av_frame_free(&frame);
        av_packet_free(&pkt);
        return;
    }

    int x, y, ret;
    // get a packet
    AVPacket *pkt = av_packet_alloc();
    if (!pkt)
        exit(1);

    struct SwsContext *rgba_yuv_swsctx = sws_getContext(
            ctx->width, ctx->height, AV_PIX_FMT_RGBA,
            ctx->width, ctx->height, AV_PIX_FMT_YUV420P,
            0, 0, 0, 0);

    struct SwsContext *yuv_rgba_swsctx = sws_getContext(
            ctx->width, ctx->height, AV_PIX_FMT_YUV420P,
            ctx->width, ctx->height, AV_PIX_FMT_RGBA,
            0, 0, 0, 0);

    struct SwsContext *noop_swsctx = sws_getContext(
            ctx->width, ctx->height, AV_PIX_FMT_YUV420P,
            ctx->width, ctx->height, AV_PIX_FMT_YUV420P,
            0, 0, 0, 0);

    pkt->data = NULL;
    pkt->size = 0;
    fflush(stdout);

    // TODO: encode the first frame
    if (0 && i == 0) {
        AVFrame *frame = av_frame_alloc();
        // get and configure a frame
        if (!frame) {
            fprintf(stderr, "Could not allocate video frame\n");
            exit(1);
        }
        frame->format = ctx->pix_fmt;
        frame->width  = ctx->width;
        frame->height = ctx->height;
        ret = av_image_alloc(
            frame->data, frame->linesize,
            ctx->width, ctx->height,
            ctx->pix_fmt, 32);
        if (ret < 0) {
            fprintf(stderr, "Could not allocate the video frame data\n");
            exit(1);
        } else {}
        /* prepare a dummy image */
        /* Y */
        for (y = 0; y < ctx->height; y++) {
            for (x = 0; x < ctx->width; x++) {
                frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
            }
        }
        /* Cb and Cr */
        for (y = 0; y < ctx->height/2; y++) {
            for (x = 0; x < ctx->width/2; x++) {
                frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
                frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 5;
            }
        }
        frame->pts = i;
        FILE* f = fopen("out.mp4", "w");
        if (!f) {
            fprintf(stderr, "Could not open file\n");
            exit(1);
        }
        encode(ctx, frame, pkt, f);
        fclose(f);
        av_frame_free(&frame);
        return;
    }

    // get and configure another frame
    AVFrame *frame2 = av_frame_alloc();
    if (!frame2) {
        fprintf(stderr, "Could not allocate video frame2\n");
        exit(1);
    }
    frame2->format = AV_PIX_FMT_RGBA;
    frame2->width  = ctx->width;
    frame2->height = ctx->height;
    ret = av_image_alloc(
        frame2->data, frame2->linesize,
        ctx->width, ctx->height,
        AV_PIX_FMT_RGBA, 32);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate the video frame2 data\n");
        exit(1);
    } else {}

    int bytes_per_frame = 4 * 2560 * 1440;
    frame2->data[0] = frame_data;

    // get and configure another frame
    AVFrame *frame3 = av_frame_alloc();
    if (!frame3) {
        fprintf(stderr, "Could not allocate video frame3\n");
        exit(1);
    }
    frame3->format = ctx->pix_fmt;
    frame3->width  = ctx->width;
    frame3->height = ctx->height;
    ret = av_image_alloc(
        frame3->data, frame3->linesize,
        ctx->width, ctx->height,
        ctx->pix_fmt, 32);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate the video frame3 data\n");
        exit(1);
    } else {}
    // convert rgba->yuv
    sws_scale(rgba_yuv_swsctx,
              (const uint8_t * const *)frame2->data,
              frame2->linesize,
              0,
              ctx->height,
              frame3->data,
              frame3->linesize);

    frame3->pts = i;
    FILE* f = fopen("out.mp4", "a");
    if (!f) {
        fprintf(stderr, "Could not open file\n");
        exit(1);
    }
    encode(ctx, frame3, pkt, f);
    fclose(f);
    av_frame_free(&frame2);
    av_frame_free(&frame3);
    return;
}

AVCodecContext* get_context() {
    // get the codec
    // const char *codec_name = "mpeg4";
    const char *codec_name = "libx264";
    const AVCodec *codec;
    codec = avcodec_find_encoder_by_name(codec_name);
    if (!codec) {
        fprintf(stderr, "Codec '%s' not found\n", codec_name);
        exit(1);
    }

    // get the codec context
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    // put sample parameters
    ctx->bit_rate = 1000000;
    // resolution must be a multiple of two
    ctx->width = 2560;
    ctx->height = 1440;
    // frames per second
    ctx->time_base = (AVRational){1, 60};
    ctx->framerate = (AVRational){60, 1};

    /* emit one intra frame every ten frames
     * check frame pict_type before passing frame
     * to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
     * then gop_size is ignored and the output of encoder
     * will always be I frame irrespective to gop_size
     */
    ctx->gop_size = 10;
    ctx->max_b_frames = 1;
    ctx->pix_fmt = AV_PIX_FMT_YUV420P;

    if (codec->id == AV_CODEC_ID_H264) {
        av_opt_set(ctx->priv_data, "tune", "animation", 0);
        // medium is default
        av_opt_set(ctx->priv_data, "preset", "medium", 0);
        // 23 is default
        // av_opt_set(ctx->priv_data, "crf", "23", 0);
    }

    // initialize the context
    int ret = avcodec_open2(ctx, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open codec: %s\n", av_err2str(ret));
        exit(1);
    }
    return ctx;
}


// Our Module Definition struct
static struct PyModuleDef myModule = {
    PyModuleDef_HEAD_INIT,
    .m_name = "myModule",
    .m_doc = "Test Module",
    .m_size = -1,
};

static PyMemberDef Custom_members[] = {
    {"frame_count", T_OBJECT_EX, offsetof(CustomObject, frame_count), 0,
     "frame count"},
    {NULL}
};

static PyObject* Custom_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    CustomObject *self;
    self = (CustomObject *) type->tp_alloc(type, 0);
    if (self != NULL) {
        self->context = get_context();
        if (self->context == NULL) {
            Py_DECREF(self);
            return NULL;
        }
        self->frame_count = 0;
    }
    return (PyObject *) self;
}

static void Custom_dealloc(CustomObject *self) {
    avcodec_free_context(&self->context);
    Py_TYPE(self)->tp_free((PyObject *) self);
}

static PyObject* Custom_finish(CustomObject *self, PyObject *args) {
    // read an rgba frame and file descriptor
    int file_descriptor;
    const char *frame_data;
    Py_ssize_t frame_length;
    if (!PyArg_ParseTuple(args, "is#", &file_descriptor, &frame_data, &frame_length)) {
        return Py_None;
    }
    render(file_descriptor, (const uint8_t *)frame_data, frame_length,
           self->context, 1, -1);
    return Py_None;
}

static PyMethodDef Custom_methods[] = {
    {"process_frame", (PyCFunction) makevid, METH_VARARGS,
     "return zero"},
    {"finish", (PyCFunction) Custom_finish, METH_VARARGS,
     "return zero"},
    {NULL}  /* Sentinel */
};

static PyTypeObject CustomType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "myModule.Custom",
    .tp_doc = "Custom objects",
    .tp_basicsize = sizeof(CustomObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = Custom_new,
    .tp_dealloc = (destructor) Custom_dealloc,
    .tp_members = Custom_members,
    .tp_methods = Custom_methods,
};
/* Custom type code */

// Initializes our module using our above struct
PyMODINIT_FUNC PyInit_myModule(void)
{
    PyObject *m;
    m = PyModule_Create(&myModule);
    if (m == NULL)
        return NULL;

    // add class (type)
    if (PyType_Ready(&CustomType) < 0)
        return NULL;
    Py_INCREF(&CustomType);
    PyModule_AddObject(m, "Custom", (PyObject *) &CustomType);
    return m;
}
