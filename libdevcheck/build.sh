#!/bin/sh
set -e

CFLAGS='-g -ggdb -O0 -Wall -Wextra'
LDFLAGS=''

OBJECTS='libdevcheck device utils'

for x in $OBJECTS
do
    gcc -c ${x}.c -o ${x}.o $CFLAGS
    OBJFILES="$OBJFILES ${x}.o"
done

ar rcs libdevcheck.a $OBJFILES

