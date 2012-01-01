#!/bin/sh
set -e
cmake CMakeLists.txt && make
echo '
To install, type

make install

It will install executables whdd-cli, whdd-curses.'

