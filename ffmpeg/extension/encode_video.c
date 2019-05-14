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
    // if (frame)
    //     printf("Send frame %3"PRId64"\n", frame->pts);

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

        // printf("Write packet %3"PRId64" (size=%5d)\n", pkt->pts, pkt->size);
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
    if (i == 0) {
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
    const char *codec_name = "mpeg4";
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
    ctx->bit_rate = 400000;
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

    if (codec->id == AV_CODEC_ID_H264)
        av_opt_set(ctx->priv_data, "preset", "slow", 0);

    // initialize the context
    int ret = avcodec_open2(ctx, codec, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open codec: %s\n", av_err2str(ret));
        exit(1);
    }
    return ctx;
}


static PyObject* helloworld(PyObject* self, PyObject* args)
{
    // read an rgba frame and file descriptor
    int file_descriptor;
    const char *frame_data;
    Py_ssize_t frame_length;
    if (!PyArg_ParseTuple(args, "is#", &file_descriptor, &frame_data, &frame_length)) {
        return Py_None;
    }
    printf("Read file descriptor %d\n", file_descriptor);
    printf("Read %ld bytes of frame data\n", frame_length);
    printf("Read byte string:\n");
    for (int i = 0; i < 12; i++)
        printf("0x%02x ", (unsigned char)frame_data[i]);
    printf("...\n");

    AVCodecContext* ctx = get_context();
    for (int i = 0; i < 60; i++) {
        render(file_descriptor, (const uint8_t *)frame_data, frame_length, ctx, 0, i);
    }
    render(file_descriptor, (const uint8_t *)frame_data, frame_length, ctx, 1, -1);

    avcodec_free_context(&ctx);

    return Py_None;
}

// Our Module's Function Definition struct
// We require this `NULL` to signal the end of our method
// definition
static PyMethodDef myMethods[] = {
    { "helloworld", helloworld, METH_VARARGS, "Prints Hello World" },
    { NULL, NULL, 0, NULL }
};

// Our Module Definition struct
static struct PyModuleDef myModule = {
    PyModuleDef_HEAD_INIT,
    .m_name = "myModule",
    .m_doc = "Test Module",
    .m_size = -1,
    .m_methods = myMethods,
};

/* Custom type code */
typedef struct {
    PyObject_HEAD
    int frame_count;
    AVCodecContext* context;
} CustomObject;

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

static PyObject* Custom_process_frame(CustomObject *self, PyObject *args) {
    // read an rgba frame and file descriptor
    int file_descriptor;
    const char *frame_data;
    Py_ssize_t frame_length;
    int frame_number;
    if (!PyArg_ParseTuple(args, "is#i", &file_descriptor, &frame_data, &frame_length, &frame_number)) {
        return Py_None;
    }
    printf("Read file descriptor %d\n", file_descriptor);
    printf("Read %ld bytes of frame data\n", frame_length);
    printf("Read byte string:\n");
    for (int i = 0; i < 12; i++)
        printf("0x%02x ", (unsigned char)frame_data[i]);
    printf("...\n");

    render(file_descriptor, (const uint8_t *)frame_data, frame_length,
           self->context, 0, self->frame_count);
    self->frame_count++;
    return Py_None;
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
    {"process_frame", (PyCFunction) Custom_process_frame, METH_VARARGS,
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
