#!/bin/sh
set -e
cmake . && make
echo '
To install, type

make install

It will install executable `whdd`.'

