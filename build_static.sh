#!/bin/sh
set -e
cmake -DSTATIC=ON . && make
