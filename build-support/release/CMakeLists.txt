if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  message(FATAL_ERROR "cmake must be invoked with the top level directory")
endif()

#
# Windows installer (Inno Setup)
#

if(WIN32)

if(CMAKE_SIZEOF_VOID_P MATCHES 8)
  set(INST_SUFFIX _x64)
  set(INST_DEFS -DWIN64)
endif()

# WPF 发布目录（dotnet publish -o 的输出）。
set(TUPIGVNC_PUBLISH_DIR "${CMAKE_BINARY_DIR}/publish" CACHE PATH
    "TuPigVNC.Wpf 的 dotnet publish 输出目录")

# @ONLY 确保只替换 @VAR@ 形式，避免 Inno Setup 自身的 ${...} 语法被误替换
configure_file(tigervnc.iss.in tigervnc.iss @ONLY)

# 安装包文件名固定为 TuPigVNC_x64.exe，版本号记录在文件属性
# （VS_VERSION_INFO）与 Release 标签中，便于用户稳定识别与分享。
# 安装脚本打包整个 WPF 发布目录，不能只看 .iss 的时间决定是否重打包。
add_custom_target(installer
  COMMAND iscc -o. ${INST_DEFS} -F${CMAKE_PROJECT_NAME}${INST_SUFFIX} tigervnc.iss
  BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}${INST_SUFFIX}.exe"
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  VERBATIM)

if(BUILD_WINVNC)
  configure_file(winvnc.iss.in winvnc.iss @ONLY)

  add_custom_command(OUTPUT ${CMAKE_PROJECT_NAME}${INST_SUFFIX}_winvnc.exe
    COMMAND iscc -o. ${INST_DEFS} -F${CMAKE_PROJECT_NAME}${INST_SUFFIX}_winvnc winvnc.iss
    DEPENDS winvnc4 wm_hooks vncconfig winvnc.iss)
  add_custom_target(winvnc_installer DEPENDS ${CMAKE_PROJECT_NAME}${INST_SUFFIX}_winvnc.exe)
endif()

endif() # WIN32


#
# Mac DMG
#

if(APPLE)

configure_file(makemacapp.in makemacapp)
configure_file(Info.plist.in Info.plist)

add_custom_command(OUTPUT TuPigVNC-${VERSION}.dmg
  COMMAND sh makemacapp
  DEPENDS vncviewer makemacapp Info.plist)
add_custom_target(dmg DEPENDS TuPigVNC-${VERSION}.dmg)

endif() # APPLE


#
# Binary tarball
#

if(UNIX)

configure_file(maketarball.in maketarball)

set(PACKAGE_FILE ${CMAKE_PROJECT_NAME}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}-${VERSION}.tar.gz)
add_custom_command(OUTPUT ${PACKAGE_FILE}
  COMMAND bash maketarball
  DEPENDS maketarball vncviewer vncpasswd vncconfig)

add_custom_target(tarball DEPENDS ${PACKAGE_FILE})

endif() #UNIX

#
# Common
#

install(FILES ${CMAKE_SOURCE_DIR}/LICENCE.TXT DESTINATION ${CMAKE_INSTALL_FULL_DOCDIR})
install(FILES ${CMAKE_SOURCE_DIR}/doc/README.rst DESTINATION ${CMAKE_INSTALL_FULL_DOCDIR})
