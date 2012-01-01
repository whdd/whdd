#!/bin/sh
set -e
cmake . && make
echo '
To install, type

make install

It will install executables whdd-cli, whdd-curses.'

