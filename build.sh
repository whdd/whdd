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
echo "Consider using with some nice .dialogrc, at last my Gentoo's libdialog defaults are ugly.
The one shipped in console_visualized_ui/recommended_.dialogrc is fine.
Place it in ~/.dialogrc, or /etc/dialogrc, or set its path with 'export DIALOGRC=...'
"
