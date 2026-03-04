#!/usr/bin/env bash

rm -rf build
cmake -S . -B build
make -C build

gcc -g -Wall -Wextra main.c ./libtdmm/tdmm.c -o hw7 -I libtdmm

gcc -g -Wall -Wextra test.c ./libtdmm/tdmm.c -o test -I libtdmm