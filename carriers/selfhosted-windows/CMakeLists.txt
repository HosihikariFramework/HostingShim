cmake_minimum_required(VERSION 3.20)

project(hosihikari_selfhosted_windows LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "自持态 Windows 载体只做 Windows")
endif()

# 启动序列与其余载体共用一份源码（ADR-0005）。
set(HOSIHIKARI_COMMON ${CMAKE_CURRENT_SOURCE_DIR}/../../common)

# nethost 随版本从 nuget.org 取，不随仓带二进制 —— 与 verify/ 同一个来源。
# 这里链静态库 libnethost.lib，产品不依赖 nethost.dll（与 verify/ 的注入路径一致）。
set(HOSIHIKARI_NETHOST_VERSION "10.0.13")
set(HOSIHIKARI_NETHOST_ROOT "${CMAKE_CURRENT_BINARY_DIR}/nethost")
set(HOSIHIKARI_NETHOST_NATIVE "${HOSIHIKARI_NETHOST_ROOT}/runtimes/win-x64/native")

if(NOT EXISTS "${HOSIHIKARI_NETHOST_NATIVE}/nethost.h")
    set(HOSIHIKARI_NETHOST_PACKAGE "microsoft.netcore.app.host.win-x64")
    set(HOSIHIKARI_NETHOST_URL
        "https://api.nuget.org/v3-flatcontainer/${HOSIHIKARI_NETHOST_PACKAGE}/${HOSIHIKARI_NETHOST_VERSION}/${HOSIHIKARI_NETHOST_PACKAGE}.${HOSIHIKARI_NETHOST_VERSION}.nupkg")
    message(STATUS "拉取 nethost（win-x64 ${HOSIHIKARI_NETHOST_VERSION}）")
    file(DOWNLOAD "${HOSIHIKARI_NETHOST_URL}" "${HOSIHIKARI_NETHOST_ROOT}/nethost.nupkg" SHOW_PROGRESS)
    file(ARCHIVE_EXTRACT INPUT "${HOSIHIKARI_NETHOST_ROOT}/nethost.nupkg" DESTINATION "${HOSIHIKARI_NETHOST_ROOT}")
endif()

# 被启动器注入 bedrock_server.exe 的 DLL：导出加载方要调的那一个入口（ADR-0016）。
add_library(hosihikari SHARED
    src/shim.cpp
    ${HOSIHIKARI_COMMON}/dotnet.cpp
)

target_include_directories(hosihikari PRIVATE
    src
    ${HOSIHIKARI_COMMON}
    ${HOSIHIKARI_NETHOST_NATIVE}
)

# nethost.h 用这个宏区分"链静态库"与"链 nethost.dll 的导入库"。
target_compile_definitions(hosihikari PRIVATE NETHOST_USE_AS_STATIC UNICODE _UNICODE)

# libnethost.lib 按 /MT 编；不顺带它会在链接期报 LNK2038。
set_target_properties(hosihikari PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# 绝对路径是必需的：相对路径会被 CMake 当成库名再补一次 .lib。
target_link_libraries(hosihikari PRIVATE "${HOSIHIKARI_NETHOST_NATIVE}/libnethost.lib")

# 启动器：挂起启动 bedrock_server.exe，注入垫片，调用导出入口，再放行。
add_executable(hosihikari-server src/launcher/main.cpp)
target_compile_definitions(hosihikari-server PRIVATE UNICODE _UNICODE)

if(MSVC)
    target_compile_options(hosihikari PRIVATE /utf-8 /W4)
    target_compile_options(hosihikari-server PRIVATE /utf-8 /W4)
else()
    target_compile_options(hosihikari PRIVATE -Wall -Wextra)
    target_compile_options(hosihikari-server PRIVATE -Wall -Wextra)
endif()
