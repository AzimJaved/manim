#!/usr/bin/env python
import itertools
import ffmpeg_writer
import sys

height = 1440
width = 2560
fps = 60
bytes_per_frame = height * width * 4


infile = open('rgba.out', 'rb')
rgba = infile.read()

num_bytes = len(rgba)
num_pixels = num_bytes / 4
num_frames = num_bytes / bytes_per_frame
print(f"Read {num_bytes} bytes ({num_pixels} pixels and {num_frames} frames)")

frame = rgba[:bytes_per_frame]

print("Sending byte string:")
for i in range(12):
    sys.stdout.write(f"0x{frame[i]:02x} ")
sys.stdout.write("...\n\n")
sys.stdout.flush()

c = ffmpeg_writer.FFmpegWriter("out.mp4", 1440, 2560, 60)
for i in range(60 * 3):
    frame = rgba[bytes_per_frame * i:bytes_per_frame * (i + 1)]
    c.process_frame(frame)
c.finish()
