-- 载体目标：LeviLamina。按 ADR-0005，每个目标用它所属生态的构建 —— 这个用 xmake，
-- 因为 LeviLamina 只发布 xmake 包，而它的 mod 要链接那个包提供的导入库。

add_rules("mode.debug", "mode.release")

add_repositories("liteldev-repo https://github.com/LiteLDev/xmake-repo.git")
add_requires("levilamina")

-- LeviLamina 的 mod 用 /MD，随 SDK 的 libnethost.lib 是 /MT，两者不能混（LNK2038）。
-- 与父级共用 CRT 是必须的：我们要把 std::string、shared_ptr 递过去（ADR-0005）。
set_runtimes("MD")

target("HosihikariShim")
    set_kind("shared")
    -- LeviLamina 的头文件用了 std::to_underlying，那是 C++23 才进的标准库。
    set_languages("c++23")
    set_basename("Hosihikari.Shim.LeviLamina")

    add_cxflags("/utf-8", "/W4")
    add_defines("NOMINMAX", "UNICODE", "_UNICODE")

    add_files("src/*.cpp")
    add_files("../../common/dotnet.cpp")

    add_includedirs("src", "../../common")

    add_packages("levilamina")

    -- nethost 随版本从 nuget.org 取，不随仓带二进制（与 verify/ 下的 CMake 同一个来源）。
    -- 它所在的包是 platform 类型，不能当普通包引用，所以直接下 .nupkg（zip）解压；
    -- tar 就是 Windows 自带的 bsdtar，认 zip。
    --
    -- 整段写在 on_load 里，是因为 xmake.lua 的**文件作用域**和钩子拿到的是两套环境：
    -- 文件作用域那张 os 表里没有 mkdir，而 http 模块只在钩子这边 import 得到。
    on_load(function(target)
        import("net.http")

        local version = "10.0.13"
        local rid = is_plat("windows") and "win-x64" or "linux-x64"
        local package = "microsoft.netcore.app.host." .. rid
        local root = path.join(os.projectdir(), "build", "nethost")
        local native = path.join(root, "runtimes", rid, "native")

        if not os.isfile(path.join(native, "nethost.h")) then
            local url = "https://api.nuget.org/v3-flatcontainer/" .. package .. "/" .. version ..
                            "/" .. package .. "." .. version .. ".nupkg"
            local nupkg = path.join(root, "nethost.nupkg")

            cprint("${bright}拉取 nethost${clear}（" .. rid .. " " .. version .. "）")
            os.mkdir(root)
            http.download(url, nupkg)
            os.vrunv("tar", {"-xf", nupkg, "-C", root})
        end

        target:add("includedirs", native)
        target:add("linkdirs", native)
        -- nethost.lib 是 1.7 KB 的导入库，libnethost.lib 才是静态库。链接前者，
        -- 运行期依赖 nethost.dll，由 BDS 的同目录提供。
        target:add("links", "nethost")
    end)
