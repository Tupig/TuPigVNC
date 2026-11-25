# Gettext support - mostly borrowed from the Licq project

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  message(FATAL_ERROR "cmake must be invoked with the top level directory")
endif()

file(STRINGS LINGUAS po_FILES)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS LINGUAS)

if (NOT GETTEXT_MSGMERGE_EXECUTABLE AND NOT GETTEXT_MSGFMT_EXECUTABLE)
  message(FATAL_ERROR "Gettext message catalog tools NOT found")
endif (NOT GETTEXT_MSGMERGE_EXECUTABLE AND NOT GETTEXT_MSGFMT_EXECUTABLE)

find_program(GETTEXT_XGETTEXT_EXECUTABLE xgettext)
if (GETTEXT_XGETTEXT_EXECUTABLE)
  # Get list of all source files
  file(GLOB po_source
    RELATIVE ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/common/core/*.h
    ${PROJECT_SOURCE_DIR}/src/common/core/*.cxx
    ${PROJECT_SOURCE_DIR}/src/common/network/*.h
    ${PROJECT_SOURCE_DIR}/src/common/network/*.cxx
    ${PROJECT_SOURCE_DIR}/src/common/rdr/*.h
    ${PROJECT_SOURCE_DIR}/src/common/rdr/*.cxx
    ${PROJECT_SOURCE_DIR}/src/common/rfb/*.h
    ${PROJECT_SOURCE_DIR}/src/common/rfb/*.cxx
    ${PROJECT_SOURCE_DIR}/platform/unix/common/*.h
    ${PROJECT_SOURCE_DIR}/platform/unix/common/*.cxx
    ${PROJECT_SOURCE_DIR}/platform/unix/tx/*.h
    ${PROJECT_SOURCE_DIR}/platform/unix/tx/*.cxx
    ${PROJECT_SOURCE_DIR}/platform/unix/vncconfig/*.h
    ${PROJECT_SOURCE_DIR}/platform/unix/vncconfig/*.cxx
    ${PROJECT_SOURCE_DIR}/platform/unix/vncpasswd/*.h
    ${PROJECT_SOURCE_DIR}/platform/unix/vncpasswd/*.cxx
    ${PROJECT_SOURCE_DIR}/platform/unix/vncserver/*.h
    ${PROJECT_SOURCE_DIR}/platform/unix/vncserver/*.c
    ${PROJECT_SOURCE_DIR}/platform/unix/w0vncserver/*.h
    ${PROJECT_SOURCE_DIR}/platform/unix/w0vncserver/*.cxx
    ${PROJECT_SOURCE_DIR}/platform/unix/w0vncserver/pipewire/*.h
    ${PROJECT_SOURCE_DIR}/platform/unix/w0vncserver/pipewire/*.cxx
    ${PROJECT_SOURCE_DIR}/platform/unix/w0vncserver/portals/*.h
    ${PROJECT_SOURCE_DIR}/platform/unix/w0vncserver/portals/*.cxx
    ${PROJECT_SOURCE_DIR}/platform/unix/w0vncserver/wayland/*.h
    ${PROJECT_SOURCE_DIR}/platform/unix/w0vncserver/wayland/*.cxx
    ${PROJECT_SOURCE_DIR}/platform/unix/w0vncserver/wayland/objects/*.h
    ${PROJECT_SOURCE_DIR}/platform/unix/w0vncserver/wayland/objects/*.cxx
    ${PROJECT_SOURCE_DIR}/platform/unix/w0vncserver/wayland/protocols/*.h
    ${PROJECT_SOURCE_DIR}/platform/unix/w0vncserver/wayland/protocols/*.cxx
    ${PROJECT_SOURCE_DIR}/platform/unix/x0vncserver/*.h
    ${PROJECT_SOURCE_DIR}/platform/unix/x0vncserver/*.cxx
    ${PROJECT_SOURCE_DIR}/platform/unix/xserver/hw/vnc/*.h
    ${PROJECT_SOURCE_DIR}/platform/unix/xserver/hw/vnc/*.c
    ${PROJECT_SOURCE_DIR}/platform/unix/xserver/hw/vnc/*.cc
    ${PROJECT_SOURCE_DIR}/src/vncviewer/*.h
    ${PROJECT_SOURCE_DIR}/src/vncviewer/*.c
    ${PROJECT_SOURCE_DIR}/src/vncviewer/*.cxx
    ${PROJECT_SOURCE_DIR}/src/vncviewer/*.m
    ${PROJECT_SOURCE_DIR}/src/vncviewer/*.mm
    ${PROJECT_SOURCE_DIR}/src/vncviewer/*.desktop.in.in
    ${PROJECT_SOURCE_DIR}/src/vncviewer/*.metainfo.xml.in
    ${PROJECT_SOURCE_DIR}/platform/windows/rfb_win32/*.h
    ${PROJECT_SOURCE_DIR}/platform/windows/rfb_win32/*.cxx
    ${PROJECT_SOURCE_DIR}/platform/windows/vncconfig/*.h
    ${PROJECT_SOURCE_DIR}/platform/windows/vncconfig/*.cxx
    ${PROJECT_SOURCE_DIR}/platform/windows/winvnc/*.h
    ${PROJECT_SOURCE_DIR}/platform/windows/winvnc/*.cxx
    ${PROJECT_SOURCE_DIR}/platform/windows/wm_hooks/*.h
    ${PROJECT_SOURCE_DIR}/platform/windows/wm_hooks/*.cxx
  )

  add_custom_target(translations_update
    ${GETTEXT_XGETTEXT_EXECUTABLE}
      "--directory=${PROJECT_SOURCE_DIR}"
      "--output=${CMAKE_CURRENT_SOURCE_DIR}/tigervnc.pot"
      --default-domain=tigervnc
      --from-code=UTF-8
      --keyword=_
      --keyword=C_:1c,2
      --keyword=N_
      --keyword=NC_:1c,2
      "--copyright-holder=TuPigVNC team and many others \(see README.rst\)"
      --msgid-bugs-address=tigervnc-devel@googlegroups.com
      --sort-by-file
      --add-location
      --add-comments=TRANSLATORS
      ${po_source}
    COMMENT "Updating tigervnc.pot"
    VERBATIM
  )
endif (GETTEXT_XGETTEXT_EXECUTABLE)

foreach(lang ${po_FILES})
  set(po  "${CMAKE_CURRENT_SOURCE_DIR}/${lang}.po")
  set(mo "${CMAKE_CURRENT_BINARY_DIR}/${lang}.mo")

  # Add command to build X.mo from X.po
  add_custom_command(OUTPUT ${mo}
    COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} --check --check-accelerators -o ${mo} ${po}
    DEPENDS ${po}
  )

  install(FILES ${mo}
    DESTINATION "${CMAKE_INSTALL_FULL_LOCALEDIR}/${lang}/LC_MESSAGES"
    RENAME tigervnc.mo
  )

  set(moFiles ${moFiles} ${mo})
endforeach(lang)

add_custom_target(translations ALL DEPENDS ${moFiles})
