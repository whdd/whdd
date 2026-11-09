[![Stand With Ukraine](https://raw.githubusercontent.com/vshymanskyy/StandWithUkraine/main/banner-direct-single.svg)](https://stand-with-ukraine.pp.ua)

WHDD is HDD diagnostic and recovery tool for Linux.

Project website: http://whdd.github.io
License: GNU GPL v3
Sources, bugreports: https://github.com/whdd/whdd

To install, run:
`./build.sh && sudo make install`

Run the device-mocked ATA geometry and read-test regression tests with `make test`.
They require a C compiler and GNU-compatible linker; no disk devices are accessed.
