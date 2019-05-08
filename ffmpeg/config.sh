#!/bin/bash

./configure \
    --prefix="../install" \
    --extra-cflags="-I$INSTALL_DIR/include" \
    --extra-ldflags="-L$INSTALL_DIR/lib" \
    --bindir="$INSTALL_DIR/bin" \
    --enable-gpl \
    --enable-libx264
make
make install
