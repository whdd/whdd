#!/bin/sh
set -e
`basename $0`/build_depends.sh
make

echo '
To install, type

make install

It will install executable `whdd`.'

