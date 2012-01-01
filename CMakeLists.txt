cmake_minimum_required (VERSION 2.6)

project (whdd)

find_library (NCURSESW ncursesw)
if (NCURSESW STREQUAL "NCURSESW-NOTFOUND")
    message (SEND_ERROR "-lncursesw not found!")
endif (NCURSESW STREQUAL "NCURSESW-NOTFOUND")

find_library (MENUW menuw)
if (MENUW STREQUAL "MENUW-NOTFOUND")
    message (SEND_ERROR "-lmenuw not found!")
endif (MENUW STREQUAL "MENUW-NOTFOUND")

find_library (DIALOG dialog)
if (DIALOG STREQUAL "DIALOG-NOTFOUND")
    message (SEND_ERROR "-ldialog not found!")
endif (DIALOG STREQUAL "DIALOG-NOTFOUND")

add_subdirectory (libdevcheck)
add_subdirectory (console_ui)
add_subdirectory (console_visualized_ui)
