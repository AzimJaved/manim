#!/usr/bin/env python
import itertools
import myModule
import sys

height = 1440
width = 2560
fps = 60
bytes_per_frame = height * width * 4


infile = open('rgb.out', 'rb')
rgba = infile.read()

num_bytes = len(rgba)
num_pixels = num_bytes / 4
num_frames = num_bytes / bytes_per_frame
print(f"Read {num_bytes} bytes ({num_pixels} pixels and {num_frames} frames)")

frame = rgba[:bytes_per_frame]

f = open('out.mp4', 'wb')
print("Sending byte string:")
for i in range(12):
    sys.stdout.write(f"0x{frame[i]:02x} ")
sys.stdout.write("...\n\n")
sys.stdout.flush()
myModule.helloworld(f.fileno(), rgba)
