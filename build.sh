#!/bin/sh
set -e
`dirname $0`/build_depends.sh
make

echo '
To install, type

sudo make install

It will install executable `whdd`.'

