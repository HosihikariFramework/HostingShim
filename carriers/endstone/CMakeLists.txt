cmake_minimum_required(VERSION 3.20)

project(hosihikari LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
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

# endstone_add_plugin 会把产物名前缀成 endstone_<target>，并只链 SDK 那个 INTERFACE 库。
endstone_add_plugin(hosihikari
    src/plugin.cpp
    src/loader.cpp
    src/managed_plugin.cpp
    ${HOSIHIKARI_COMMON}/dotnet.cpp
)

target_include_directories(hosihikari PRIVATE
    src
    ${HOSIHIKARI_COMMON}
    ${HOSIHIKARI_NETHOST_NATIVE}
)
target_link_directories(hosihikari PRIVATE ${HOSIHIKARI_NETHOST_NATIVE})
target_link_libraries(hosihikari PRIVATE nethost nlohmann_json::nlohmann_json)
target_compile_definitions(hosihikari PRIVATE NOMINMAX UNICODE _UNICODE)

# 源文件是 UTF-8 无 BOM，中文注释要显式声明编码（ADR-0005）。
if(MSVC)
    target_compile_options(hosihikari PRIVATE /utf-8 /W4)
endif()
