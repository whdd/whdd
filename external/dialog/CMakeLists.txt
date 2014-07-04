cmake_minimum_required(VERSION 2.8.4)
include(ExternalProject)
project(dialog)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../ncurses ${CMAKE_CURRENT_SOURCE_DIR}/../ncurses)
set(CURSES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ncurses/install")
ExternalProject_Add(dialog
  DEPENDS ncurses
  URL ftp://invisible-island.net/dialog/dialog-1.2-20140219.tgz
  BUILD_IN_SOURCE 1
  CONFIGURE_COMMAND bash -c "LD_LIBRARY_PATH=${CURSES_DIR}/lib LDFLAGS=\"-ltinfo -L${CURSES_DIR}/lib\" ./configure --with-curses-dir=${CURSES_DIR} --with-ncursesw --enable-widec --disable-rc-file"
  INSTALL_COMMAND echo Fake install
)
