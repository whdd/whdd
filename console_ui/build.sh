#!/bin/sh
set -e

CFLAGS='-g -ggdb -O0 -Wall -Wextra -I../libdevcheck'
LDFLAGS='-lrt -pthread'

OBJECTS='main'

for x in $OBJECTS
do
    gcc -c ${x}.c -o ${x}.o $CFLAGS
    OBJFILES="$OBJFILES ${x}.o"
done

gcc $OBJFILES ../libdevcheck/libdevcheck.a -o console_ui $LDFLAGS
