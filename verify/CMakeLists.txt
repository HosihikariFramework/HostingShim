cmake_minimum_required(VERSION 3.20)
project(hosihikari_shim_verify CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 三个目标共用 common/ 的启动序列与同一套编译设置。
function(hosihikari_shim_target name)
    target_include_directories(${name} PRIVATE ../common)

    # nethost.h 用这个宏区分"链静态库"与"链 nethost.dll 的导入库"：不定义它，
    # get_hostfxr_path 会被声明成 dllimport，链接期报 __imp_get_hostfxr_path 找不到符号。
    target_compile_definitions(${name} PRIVATE NETHOST_USE_AS_STATIC)

    # 源文件是 UTF-8（无 BOM）。不声明的话 MSVC 会按系统代码页（这里 936）读，
    # 中文注释被解码错乱后**会吃掉行边界**，报出来的行号与内容对不上 —— 查起来极其费劲。
    target_compile_options(${name} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

    if(MSVC)
        # 随 SDK 携带的 libnethost.lib 是按 /MT（静态 CRT）编的，不跟它一致就 LNK2038。
        # 这三个目标各自自成一体，静态 CRT 正合适；父级目标将来要跟父级共用 CRT（LeviLamina 的 mod 用 /MD），
        # 那时应改用 nethost.dll 加它的导入库。
        set_property(TARGET ${name} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    if(WIN32)
        # 静态库叫 libnethost.lib（1.6 MB）；同目录的 nethost.lib 只有 1.7 KB，是 nethost.dll 的导入库，
        # 链接它会得到一个运行时要求 nethost.dll 的产物。
        # 绝对路径是必需的：相对路径会被 CMake 当成库名，再补一次 .lib。
        target_link_libraries(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../common/nethost/libnethost.lib")
        target_compile_definitions(${name} PRIVATE UNICODE _UNICODE)
    else()
        # Linux 侧要的是 libnethost.a，等那条目标开工时再随仓携带。
        target_link_libraries(${name} PRIVATE ../common/nethost/libnethost.a dl)
    endif()
endfunction()

# 直接跑：走完启动序列再调托管入口。
add_executable(host-glue main.cpp ../common/dotnet.cpp)
hosihikari_shim_target(host-glue)

# 被注入：编成一个 DLL，导出加载方要调的那一个入口。
add_library(host-dll SHARED host-dll/entry.cpp ../common/dotnet.cpp)
hosihikari_shim_target(host-dll)

# 注入器：测试脚手架，只用 Win32，不碰 nethost。
if(WIN32)
    add_executable(injector injector/main.cpp)
    target_compile_options(injector PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
endif()
