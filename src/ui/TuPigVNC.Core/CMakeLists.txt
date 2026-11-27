# TuPigVNC 原生核心 DLL（C ABI，供 C# WinUI 3 / WPF 调用）
#
# 阶段 1：把原 FLTK 客户端 CConn 的连接逻辑搬进 VncSession（继承
# rfb::CConnection），GUI 无关，只依赖 src/common 下的 core/rdr/
# network/rfb/rfbclient 静态库。

add_library(TuPigVNC.Core SHARED
  VncAbi.cxx
  VncSession.cxx
)

target_include_directories(TuPigVNC.Core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src/common
)

# 导出宏：只有构建 DLL 时定义
target_compile_definitions(TuPigVNC.Core PRIVATE
  TUPIGVNC_CORE_EXPORTS
  _CRT_SECURE_NO_WARNINGS
)

# 静态 CRT 与主工程保持一致（/MT），避免 C# 侧 self-contained 时 CRT 冲突
set_target_properties(TuPigVNC.Core PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded"
  OUTPUT_NAME "TuPigVNC.Core"
)

# 复用 src/common 已有的静态库；rfbclient 依赖 rfb/rdr/network/core，
# 这里全部显式列出，保证链接顺序与依赖解析。
target_link_libraries(TuPigVNC.Core PRIVATE
  rfbclient
  rfb
  network
  rdr
  core
)

# 第三方运行库（来自 vcpkg 静态三元组）
target_link_libraries(TuPigVNC.Core PRIVATE
  ${JPEG_LIBRARIES}
  ${ZLIB_LIBRARIES}
  ${PIXMAN_LIBRARIES}
)

if(NETTLE_FOUND)
  target_link_libraries(TuPigVNC.Core PRIVATE
    ${HOGWEED_LIBRARIES}
    ${NETTLE_LIBRARIES}
  )
endif()

if(WIN32)
  target_link_libraries(TuPigVNC.Core PRIVATE
    ws2_32 winhttp bcrypt
    ole32 mfplat mfuuid wmcodecdspuuid
  )
else()
  target_link_libraries(TuPigVNC.Core PRIVATE pthread)
endif()
