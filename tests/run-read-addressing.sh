#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 /path/to/patched/whdd-sources" >&2
    exit 2
fi

source_dir=$(CDPATH= cd -- "$1" && pwd -P)
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
printf '#define WHDD_VERSION "test"\n' > "$work_dir/version.h"

"${CC:-cc}" ${CFLAGS:-} -std=gnu99 -D_GNU_SOURCE -UNDEBUG -ffunction-sections \
    -fdata-sections -I"$work_dir" -I"$source_dir" -I"$source_dir/libdevcheck" \
    "$test_dir/read-addressing.c" "$source_dir/libdevcheck/ata.c" \
    "$source_dir/libdevcheck/scsi.c" "$source_dir/libdevcheck/utils.c" \
    "$source_dir/libdevcheck/read_test.c" -Wl,--gc-sections \
    -Wl,--wrap=open,--wrap=open64,--wrap=close,--wrap=read,--wrap=ioctl \
    -Wl,--wrap=lseek,--wrap=lseek64 -o "$work_dir/read-addressing"

${RUNNER:-} "$work_dir/read-addressing"
