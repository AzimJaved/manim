#!/bin/bash

FFMPEG_DIR="FFmpeg-n4.1.3"
INSTALL_DIR="../install"
cd $FFMPEG_DIR && ./configure \
    --prefix="$INSTALL_DIR" \
    --extra-cflags="-I$INSTALL_DIR/include -fPIC" \
    --extra-ldflags="-L$INSTALL_DIR/lib" \
    --bindir="$INSTALL_DIR/bin" \
    --enable-shared \
    --enable-gpl \
    --enable-libx264
make -j4
make install
