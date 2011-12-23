#!/bin/sh
set -e

CFLAGS='-g -ggdb -O0 -Wall -Wextra -I../libdevcheck -std=gnu99'
LDFLAGS='-lrt -pthread -lncursesw -lmenuw -lm'

OBJECTS='main vis ncurses_convenience dialog_convenience'

for x in $OBJECTS
do
    gcc -c ${x}.c -o ${x}.o $CFLAGS
    OBJFILES="$OBJFILES ${x}.o"
done

gcc $OBJFILES ../libdevcheck/libdevcheck.a /usr/lib/libdialog.a -o console_visualized_ui $LDFLAGS
