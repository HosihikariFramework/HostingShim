// 验证「原生 → 托管」这一跳 —— ADR-0005 的第一刀。
//
// 它不需要 BDS：一个普通 exe 走完启动序列，再调托管入口。这一跳通了，
// 四个载体剩下的只是各自把它接到自己的入口上（父级子类 / 符号拦截）。
//
// 每一步单独调用而不是走 StartRuntime，是为了让失败能归因到具体哪一步。

#include <cstdio>
#include <string>

#include "dotnet.h"

namespace {

// char_t 在 Windows 是 wchar_t、Linux 是 char（ADR-0005 的跨平台差异之一）。
// 这个验证程序只处理 ASCII 路径，所以按字节转换就够。
#if defined(_WIN32)
std::wstring AsCharT(const std::string& text) {
    return std::wstring(text.begin(), text.end());
}

std::string AsNarrow(const char_t* text) {
    std::string result;
    for (; *text != 0; ++text) {
        result.push_back(static_cast<char>(*text));
    }
    return result;
}
#else
std::string AsCharT(const std::string& text) {
    return text;
}

std::string AsNarrow(const char_t* text) {
    return std::string(text);
}
#endif

bool Report(const char* step, int code) {
    if (code == 0) {
        std::printf("PASS  %s\n", step);
        return true;
    }

    std::printf("FAIL  %s  （0x%08X）\n", step, static_cast<unsigned>(code));
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("用法: host-glue <托管侧输出目录>\n");
        return 2;
    }

    std::string directory = argv[1];
    if (!directory.empty() && directory.back() != '/' && directory.back() != '\\') {
        directory.push_back('/');
    }

    const std::wstring unused = {};  // 占位，避免不同标准库下的窄化告警
    (void)unused;

    const auto configPath = AsCharT(directory + "Hosihikari.PluginManager.runtimeconfig.json");
    const auto assemblyPath = AsCharT(directory + "Hosihikari.PluginManager.dll");
    const auto typeName = AsCharT("Hosihikari.NativeHostingEntry, Hosihikari.PluginManager");
    const auto methodName = AsCharT("Start");

    std::printf("=== 原生宿主：%s ===\n", AsNarrow(assemblyPath.c_str()).c_str());

    int code = hosihikari::shim::LoadHostfxr();
    if (!Report("定位并加载 hostfxr", code)) {
        return 1;
    }

    code = hosihikari::shim::InitializeRuntime(configPath.c_str(), assemblyPath.c_str());
    if (!Report("以 runtimeconfig 初始化运行时", code)) {
        return 1;
    }

    component_entry_point_fn entry = nullptr;
    code = hosihikari::shim::GetManagedEntry(assemblyPath.c_str(), typeName.c_str(), methodName.c_str(), &entry);
    if (!Report("载入程序集并取托管入口 NativeHostingEntry.Start", code)) {
        return 1;
    }

    const int managed = entry(nullptr, 0);
    if (!Report("调托管入口", managed)) {
        return 1;
    }

    std::printf("=== 缝合完成 ===\n");
    return 0;
}
