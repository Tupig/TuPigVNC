# TuPigVNC 原生核心 DLL（C ABI，供 C# WinUI 3 / WPF 调用）
#
# 阶段 1：把原 FLTK 客户端 CConn 的连接逻辑搬进 VncSession（继承
# rfb::CConnection），GUI 无关，只依赖 src/common 下的 core/rdr/
# network/rfb/rfbclient 静态库。

add_library(TuPigVNC.Core SHARED
  VncAbi.cxx
  VncSession.cxx
)

# 音频播放后端：VncSession 会调 AudioOutput::create()，
# 工厂和 Win32 实现都不依赖 FLTK，直接编进 Core。
# 它们放在 src/vncviewer 下但只依赖 src/common，故显式列出源文件。
if(WIN32)
  target_sources(TuPigVNC.Core PRIVATE
    ${CMAKE_SOURCE_DIR}/src/vncviewer/AudioOutput.cxx
    ${CMAKE_SOURCE_DIR}/src/vncviewer/AudioOutputWin32.cxx
  )
elseif(HAVE_AUDIO)
  target_sources(TuPigVNC.Core PRIVATE
    ${CMAKE_SOURCE_DIR}/src/vncviewer/AudioOutput.cxx
    ${CMAKE_SOURCE_DIR}/src/vncviewer/AudioOutputPulse.cxx
  )
endif()

target_include_directories(TuPigVNC.Core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src/common
)

target_include_directories(TuPigVNC.Core PRIVATE
  ${CMAKE_SOURCE_DIR}/src/vncviewer
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

# MinGW 默认给 DLL 加 "lib" 前缀（libTuPigVNC.Core.dll），
# 而 C# 的 P/Invoke 和 WPF 工程都按 TuPigVNC.Core.dll 查找。
# 去掉前缀，让两套编译器产出同样的文件名。
if(MINGW)
  set_target_properties(TuPigVNC.Core PROPERTIES PREFIX "")
endif()

# MinGW 构建必须把 libstdc++ / libgcc / libwinpthread 静态链进来，
# 否则 DLL 依赖 MinGW 安装目录下的三个运行时 DLL——用户机器上不一定有，
# 出现「找不到指定的模块 (0x8007007E)」。静态链接后 DLL 自包含。
if(MINGW)
  target_link_options(TuPigVNC.Core PRIVATE
    -static
    -static-libgcc
    -static-libstdc++
  )
endif()

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
    # waveOut 音频播放（AudioOutputWin32）
    winmm
  )
else()
  target_link_libraries(TuPigVNC.Core PRIVATE pthread)
endif()
