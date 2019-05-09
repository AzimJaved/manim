# use pkg-config for getting CFLAGS and LDLIBS
FFMPEG_LIBS=libavdevice   \
            libavformat   \
            libavfilter   \
            libavcodec    \
            libswresample \
            libswscale    \
            libavutil
CFLAGS := $(shell pkg-config --cflags $(FFMPEG_LIBS)) $(CFLAGS)
LDLIBS := $(shell pkg-config --libs $(FFMPEG_LIBS)) $(LDLIBS)
EXAMPLES=encode_video
OBJS=$(addsuffix .o,$(EXAMPLES))
all: $(OBJS) $(EXAMPLES)
clean:
	$(RM) $(EXAMPLES) $(OBJS)
