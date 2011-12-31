#!/bin/sh
set -e

pushd libdevcheck
./build.sh
popd

pushd console_ui
./build.sh
popd

pushd console_visualized_ui
./build.sh
popd

cp -v console_visualized_ui/console_visualized_ui whdd

echo -e '\n\nYou need just whdd binary, use it\n\n'
