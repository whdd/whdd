cmake_minimum_required(VERSION 2.8.4)
include(ExternalProject)
project(ncurses)
set(INSTALL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/install")
ExternalProject_Add(ncurses
    URL http://ftp.gnu.org/pub/gnu/ncurses/ncurses-5.9.tar.gz
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND ./configure --enable-widec --without-gpm --prefix=${INSTALL_DIR} --with-terminfo-dirs=/usr/share/terminfo:/lib/terminfo --with-termlib --with-shared
    BUILD_COMMAND make
    INSTALL_COMMAND make install && cd ${INSTALL_DIR}/lib && ln -sfv libtinfow.so libtinfo.so
    )
