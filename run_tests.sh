#!/usr/bin/env bash

rm -rf build
cmake -S . -B build
make -C build

gcc main.c ./libtdmm/tdmm.c -o hw7 -I./libtdmm/ -lm

gcc test.c ./libtdmm/tdmm.c -o test -I./libtdmm/ -lm