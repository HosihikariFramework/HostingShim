cmake_minimum_required(VERSION 3.20)

project(hosihikari_selfhosted_linux LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "自持态载体只做 Linux")
endif()

# 启动序列与两个父级载体共用一份源码（ADR-0005）。
set(HOSIHIKARI_COMMON ${CMAKE_CURRENT_SOURCE_DIR}/../../common)

# nethost 随版本从 nuget.org 取，不随仓带二进制。
set(HOSIHIKARI_NETHOST_VERSION "10.0.13")
set(HOSIHIKARI_NETHOST_ROOT "${CMAKE_CURRENT_BINARY_DIR}/nethost")
set(HOSIHIKARI_NETHOST_NATIVE "${HOSIHIKARI_NETHOST_ROOT}/runtimes/linux-x64/native")

if(NOT EXISTS "${HOSIHIKARI_NETHOST_NATIVE}/nethost.h")
    set(HOSIHIKARI_NETHOST_PACKAGE "microsoft.netcore.app.host.linux-x64")
    set(HOSIHIKARI_NETHOST_URL
        "https://api.nuget.org/v3-flatcontainer/${HOSIHIKARI_NETHOST_PACKAGE}/${HOSIHIKARI_NETHOST_VERSION}/${HOSIHIKARI_NETHOST_PACKAGE}.${HOSIHIKARI_NETHOST_VERSION}.nupkg")
    message(STATUS "拉取 nethost（linux-x64 ${HOSIHIKARI_NETHOST_VERSION}）")
    file(DOWNLOAD "${HOSIHIKARI_NETHOST_URL}" "${HOSIHIKARI_NETHOST_ROOT}/nethost.nupkg" SHOW_PROGRESS)
    file(ARCHIVE_EXTRACT INPUT "${HOSIHIKARI_NETHOST_ROOT}/nethost.nupkg" DESTINATION "${HOSIHIKARI_NETHOST_ROOT}")
endif()

add_library(hosihikari SHARED
    src/interpose.cpp
    ${HOSIHIKARI_COMMON}/dotnet.cpp
)

target_include_directories(hosihikari PRIVATE
    src
    ${HOSIHIKARI_COMMON}
    ${HOSIHIKARI_NETHOST_NATIVE}
)
target_link_directories(hosihikari PRIVATE ${HOSIHIKARI_NETHOST_NATIVE})
target_link_libraries(hosihikari PRIVATE nethost ${CMAKE_DL_LIBS})

# 影子复制不会发生在自持态；$ORIGIN 就是装载目录，libnethost.so 放在旁边。
set_target_properties(hosihikari PROPERTIES
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
)

target_compile_options(hosihikari PRIVATE -Wall -Wextra)
