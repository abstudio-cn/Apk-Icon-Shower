cmake_minimum_required(VERSION 3.20)
project(apkIconShower LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# VS 直接打开文件夹时（无 -D 参数）也能命中静态 Qt
if(NOT DEFINED Qt6_DIR AND NOT CMAKE_PREFIX_PATH)
  set(CMAKE_PREFIX_PATH "C:/Qt/6.9.3-static-msvc2022_64")
endif()
if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_CONFIGURATION_TYPES "Release;RelWithDebInfo" CACHE STRING "" FORCE)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_options(/utf-8 /permissive-)
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

# ---------------------------------------------------------------- 核心库（无 Qt）
add_library(apkcore STATIC
  src/apkcore/miniz.c
  src/apkcore/ZipReader.cpp
  src/apkcore/Axml.cpp
  src/apkcore/Arsc.cpp
  src/apkcore/ApkInfo.cpp
  src/apkcore/WicIcon.cpp
  src/apkcore/AssocRegistry.cpp
)
target_include_directories(apkcore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/apkcore")
target_link_libraries(apkcore PUBLIC windowscodecs shlwapi ole32 oleaut32 advapi32 user32 gdi32 shell32)

# ---------------------------------------------------------------- 命令行自检工具
add_executable(apkicon_dump tools/apkicon_dump.cpp)
target_link_libraries(apkicon_dump PRIVATE apkcore)

add_executable(shellext_test tools/shellext_test.cpp)
target_link_libraries(shellext_test PRIVATE apkcore)

add_executable(shell_icon_check tools/shell_icon_check.cpp)
target_link_libraries(shell_icon_check PRIVATE apkcore shell32)

# 同一进程内查询多个文件的图标（定位“不同文件同一图标”问题）
add_executable(icon_multi tools/icon_multi.cpp)
target_link_libraries(icon_multi PRIVATE shell32 ole32)


# ---------------------------------------------------------------- Shell 扩展 DLL
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/shellext/dllmain.cpp")
add_library(ApkIconShowerShell SHARED
  src/shellext/dllmain.cpp
  src/shellext/ApkIconExtractor.cpp
  src/shellext/RegShellExt.cpp
  src/shellext/ApkIconShowerShell.def
)
target_include_directories(ApkIconShowerShell PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/shellext")
target_link_libraries(ApkIconShowerShell PRIVATE apkcore)
set_target_properties(ApkIconShowerShell PROPERTIES OUTPUT_NAME "ApkIconShowerShell" PREFIX "")
endif()

# ---------------------------------------------------------------- Qt GUI
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/app/main.cpp")
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_executable(ApkIconShower WIN32
  src/app/main.cpp
  src/app/AppCore.cpp
  src/app/OpenWithWindow.cpp
  src/app/Theme.cpp
  resources/app.qrc
  resources/app.rc
)
target_include_directories(ApkIconShower PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src/app"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/apkcore"
)
target_link_libraries(ApkIconShower PRIVATE apkcore Qt6::Widgets Qt6::Gui Qt6::Core dwmapi)
endif()
