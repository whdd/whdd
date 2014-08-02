# - Find dialog
# Find the native dialog headers and libraries.
#
#  DIALOG_INCLUDE_DIRS - where to find dialog.h, etc.
#  DIALOG_LIBRARIES    - List of libraries when using dialog.
#  DIALOG_FOUND        - True if dialog found.

#=============================================================================
# Copyright 2012 Azamat H. Hackimov <azamat.hackimov@gmail.com>
#
# Distributed under the OSI-approved BSD License (the "License");
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#=============================================================================


find_path(DIALOG_INCLUDE_DIR
	NAMES dialog.h
)
find_library(DIALOG_LIBRARY
	NAMES dialog
)

find_path(NCURSES_INCLUDE_DIR
    NAMES curses.h
)

find_library(NCURSESW_LIBRARY
	NAMES ncursesw
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(dialog DEFAULT_MSG
	DIALOG_LIBRARY
	NCURSESW_LIBRARY
	DIALOG_INCLUDE_DIR
	NCURSES_INCLUDE_DIR
)

mark_as_advanced(DIALOG_INCLUDE_DIR DIALOG_LIBRARY)

if(DIALOG_FOUND)
	set(DIALOG_LIBRARIES ${DIALOG_LIBRARY} ${NCURSES_LIBRARY} -lm)
	set(DIALOG_INCLUDE_DIRS ${DIALOG_INCLUDE_DIR} ${NCURSES_INCLUDE_DIR})
endif(DIALOG_FOUND)
