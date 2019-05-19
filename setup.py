#!/usr/bin/env python

from setuptools import setup
# from distutils.core import setup, Extension
# import os
# 
# FFMPEG_INSTALL = 'ffmpeg/install'
# ffmpeg_writer = Extension(
#     'manimlib.ffmpeg_writer',
#     include_dirs = [os.path.join(FFMPEG_INSTALL, 'include')],
#     library_dirs = [os.path.join(FFMPEG_INSTALL, 'lib')],
#     libraries = [
#         "avcodec",
#         "avdevice",
#         "avfilter",
#         "avformat",
#         "avutil",
#         "m",
#         "postproc",
#         "pthread",
#         "swresample",
#         "swscale",
#         "x264",
#         "xcb",
#         "xcb-shape",
#         "xcb-shm",
#         "xcb-xfixes",
#         "z",
#     ],
#     sources = [os.path.join('ffmpeg', 'extension', 'encode_video.c')],
# )

setup(
    setup_requires=['pbr'],
    pbr=True,
    # ext_modules = [ffmpeg_writer]
)
