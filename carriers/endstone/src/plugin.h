#pragma once

#include "endstone/plugin/plugin.h"

namespace hosihikari::shim::endstone_carrier {

/// 载体目标：Endstone（ADR-0005 的第二个目标）。
///
/// 它做三件事：
///   1. 把 CLR 托管进 BDS 进程 —— 与 LeviLamina 载体共用 common/ 的启动序列；
///   2. 把自己注册成 Endstone 的一个插件加载器（ADR-0003），此后子插件由 Endstone 调度；
///   3. 发起一轮自己的加载 —— 父级在调我们之前已经收完了候选，见 onLoad。
///
/// 插件的元数据由 ENDSTONE_PLUGIN 宏给出（见 plugin.cpp）。
class HosihikariPlugin : public endstone::Plugin {
public:
    void onLoad() override;
    void onEnable() override;
    void onDisable() override;
};

}  // namespace hosihikari::shim::endstone_carrier
