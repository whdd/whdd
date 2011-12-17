#!/bin/sh
set -e

./version.sh > version.h

CFLAGS='-g -ggdb -O0 -Wall -Wextra'
LDFLAGS=''

OBJECTS='libdevcheck device action utils readtest zerofill'

for x in $OBJECTS
do
    gcc -c ${x}.c -o ${x}.o $CFLAGS
    OBJFILES="$OBJFILES ${x}.o"
done

ar rcs libdevcheck.a $OBJFILES

