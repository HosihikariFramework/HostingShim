cmake_minimum_required(VERSION 3.20)

project(hosihikari LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Change this to the Endstone version you are targeting (ADR-0005: 每个载体用它所属生态的构建).
set(ENDSTONE_API_VERSION "0.11" CACHE STRING "Endstone API version")

include(FetchContent)

# SOURCE_SUBDIR 只取 SDK 那一层：服务端本体用不着，也不该在载体构建里编。
FetchContent_Declare(
    endstone
    GIT_REPOSITORY https://github.com/EndstoneMC/endstone.git
    GIT_TAG v${ENDSTONE_API_VERSION}
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR include
)
FetchContent_MakeAvailable(endstone)

# 清单是 JSON。Endstone 的 SDK 不带解析器，而我们要从清单里取名字之外的东西
# （版本、依赖），所以自己拉一个 header-only 的。
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# 启动序列与 LeviLamina 载体共用一份源码（ADR-0005）。
set(HOSIHIKARI_COMMON ${CMAKE_CURRENT_SOURCE_DIR}/../../common)

# nethost 随版本从 nuget.org 取，不随仓带二进制 —— 与 verify/ 同一个来源。
# 运行包按平台分开；两边都是 x64，RID 决定包名与 native 目录。
set(HOSIHIKARI_NETHOST_VERSION "10.0.13")
set(HOSIHIKARI_NETHOST_ROOT "${CMAKE_CURRENT_BINARY_DIR}/nethost")

if(WIN32)
    set(HOSIHIKARI_NETHOST_RID "win-x64")
    set(HOSIHIKARI_NETHOST_PACKAGE "microsoft.netcore.app.host.win-x64")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(HOSIHIKARI_NETHOST_RID "linux-x64")
    set(HOSIHIKARI_NETHOST_PACKAGE "microsoft.netcore.app.host.linux-x64")
else()
    message(FATAL_ERROR "不支持的载体平台：${CMAKE_SYSTEM_NAME}")
endif()

set(HOSIHIKARI_NETHOST_NATIVE "${HOSIHIKARI_NETHOST_ROOT}/runtimes/${HOSIHIKARI_NETHOST_RID}/native")

if(NOT EXISTS "${HOSIHIKARI_NETHOST_NATIVE}/nethost.h")
    set(HOSIHIKARI_NETHOST_URL
        "https://api.nuget.org/v3-flatcontainer/${HOSIHIKARI_NETHOST_PACKAGE}/${HOSIHIKARI_NETHOST_VERSION}/${HOSIHIKARI_NETHOST_PACKAGE}.${HOSIHIKARI_NETHOST_VERSION}.nupkg")
    message(STATUS "拉取 nethost（${HOSIHIKARI_NETHOST_RID} ${HOSIHIKARI_NETHOST_VERSION}）")
    file(DOWNLOAD "${HOSIHIKARI_NETHOST_URL}" "${HOSIHIKARI_NETHOST_ROOT}/nethost.nupkg" SHOW_PROGRESS)
    file(ARCHIVE_EXTRACT INPUT "${HOSIHIKARI_NETHOST_ROOT}/nethost.nupkg" DESTINATION "${HOSIHIKARI_NETHOST_ROOT}")
endif()

# endstone_add_plugin 会把产物名前缀成 endstone_<target>，并只链 SDK 那个 INTERFACE 库。
endstone_add_plugin(hosihikari
    src/plugin.cpp
    src/loader.cpp
    src/managed_plugin.cpp
    ${HOSIHIKARI_COMMON}/dotnet.cpp
)

# 上游的 endstone_add_plugin 会把产物名前缀成 endstone_<target>。那个前缀只服务于它的一条
# 诊断（文件以 endstone_ 开头却没有 init_endstone_plugin 时提示"是不是忘了 ENDSTONE_PLUGIN"），
# 而我们有那个宏。去掉前缀，两个载体的产物同名。
set_target_properties(hosihikari PROPERTIES PREFIX "")

target_include_directories(hosihikari PRIVATE
    src
    ${HOSIHIKARI_COMMON}
    ${HOSIHIKARI_NETHOST_NATIVE}
)
target_link_directories(hosihikari PRIVATE ${HOSIHIKARI_NETHOST_NATIVE})
target_link_libraries(hosihikari PRIVATE nethost nlohmann_json::nlohmann_json ${CMAKE_DL_LIBS})

# Linux 上 libnethost.so 与插件同住 plugins/。Endstone 会把插件影子复制到
# plugins/.local/ 再 dlopen，所以 $ORIGIN/.. 也要在搜索路径里 —— 影子那份靠它回到 plugins/。
if(UNIX AND NOT APPLE)
    set_target_properties(hosihikari PROPERTIES
        BUILD_RPATH "$ORIGIN;$ORIGIN/.."
        INSTALL_RPATH "$ORIGIN;$ORIGIN/..")
    # Endstone reload 会 dlclose 影子副本；CLR 还在这段代码上跑，不能让它消失。
    target_link_options(hosihikari PRIVATE "-Wl,-z,nodelete")
endif()

if(WIN32)
    target_compile_definitions(hosihikari PRIVATE NOMINMAX UNICODE _UNICODE)
endif()

# 源文件是 UTF-8 无 BOM，中文注释要显式声明编码（ADR-0005）。
if(MSVC)
    target_compile_options(hosihikari PRIVATE /utf-8 /W4)
else()
    target_compile_options(hosihikari PRIVATE -Wall -Wextra)
endif()
