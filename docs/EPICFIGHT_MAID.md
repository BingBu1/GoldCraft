# 女仆 Epic Fight 扩展的 1.21.1 移植

本地移植目标为 Minecraft **1.21.1 / NeoForge 21.1.256 / Java 21**。女仆本体和 YSM GEO 兼容层的上游提交固定在 `sources.lock.json`，公开仓库只保存适配补丁、构建与测试工具。原作者、许可及资源权利声明见 [第三方来源](../THIRD_PARTY_NOTICES.md)。

## 构建

先完成 [基础运行环境与 Mod 主实例](BUILD.md)，在 X Minecraft Launcher 的 GoldCraft 主实例安装匹配的依赖。当前测试组合为 Epic Fight 21.17.3.1、Touhou Little Maid 1.5.3、GeckoLib 4.9.3、Invincible 21.15.8.2、NightFall 3.4.0 和 YSM 2.6.5。女仆本体还需要 Avalon 21.12.6.2，当前由 NightFall 的嵌套 JAR 提供；移除 NightFall 时须另行保留兼容的独立 Avalon。NightFall 和 YSM 本身不是女仆技能本体的强制依赖；当前构建保留对应可选适配类，因此编译使用这一完整组合。

```powershell
.\tools\Prepare-Sources.ps1
python .\tools\Build-EpicFightMaid.py
```

产物为 `build/epicfight-maid/ef_tlm-1.21.1-neoforge-1.3.5-goldcraft.2.jar`。其中已嵌入 YSM GEO Compat，安装时只需要这个 JAR。`build.json` 记录实际依赖哈希及源码修订，`compile.log` 保留编译信息。上游 `libs/Nightfall-Enhance.jar` 仅供编译可选适配，既不打包也不安装。构建会检查源码提交、描述文件和新增源码清单，失败时保留已有 JAR。

适配包括 NeoForge 事件与网络注册、Epic Fight 新接口、1.21.1 数据组件与配方、客户端渲染和菜单签名。GoldCraft 捕获实体网格时使用已有的 `ModMeshCapture` 作用域，将 YSM GEO 的蒙皮顶点交给宿主导出；普通 Minecraft 绘制继续使用其原有渲染路径。YSM 玩家模型与女仆 YSM 模型所需的其他兼容扩展仍需单独验证。

## 安装与同步

当前工作区已将 `.2` 更新到主实例及 A/B/服务端；四份 JAR 哈希一致，旧版已备份并移出各 `mods` 目录，后续计划为空。客户端画面和菜单操作尚未验收。

把构建 JAR 放入 X 管理的 `sandbox/modpack-neoforge/GoldCraft-1.21.1-NeoForge/mods`，仅保留一个 `ef_tlm` 版本。然后使用现有 [统一 Mod 管理](MOD_MANAGEMENT.md)：

```powershell
.\tools\Sync-Modpack.ps1 -ValidateOnly -Loader neoforge
.\tools\Sync-Modpack.ps1 -Restart -Start -Loader neoforge
```

依赖校验和同步覆盖本机 A/B/服务端，日常只启动 B。无需分别安装女仆扩展或嵌套的 YSM GEO。JVM 需要重启；完整集群重启会按既定规则清空本轮 MC 建筑。

## 验证范围

安装到主实例前可独立测试，不改变现有服务器与主 Mod 清单：

```powershell
python .\tools\Exercise-NeoForgeCompatibility.py --modpack --extra-mod build/epicfight-maid/ef_tlm-1.21.1-neoforge-1.3.5-goldcraft.2.jar --maid-checks
```

已安装进主实例后省略 `--extra-mod`，避免重复 Mod ID。测试新版候选 JAR 时改用 `--replace-mod <工作区JAR>`；它只替换独立测试服中的同 ID Mod，保留端分配规则。`--exclude-mod <顶层Mod ID>` 可以重复使用，仅从该次测试排除指定包。所有额外依赖和替换包都参与 FML 预检；这些参数不更改主实例或三端部署。

探针使用实际 NeoForge 生产专服、真实女仆实体和 Epic Fight AI。以下三个组合各通过 29 项检查：

| 组合 | 结果 |
|---|---|
| 当前完整 Mod 集 | 技能、YSM 选择器与自主战斗通过 |
| 移除 YSM | 跳过目标不存在的 YSM guard，其他检查通过 |
| 移除 NightFall 和 YSM，保留独立 Avalon | 保留本体技能、不注册 NightFall 技能，其他检查通过 |

检查覆盖技能学习与 NBT 重载、祭坛配方与技能书组件、包编解码、所有者与距离校验、取消遗忘保留状态、正常遗忘及空快照清理，以及女仆自主攻击僵尸并保留攻击者身份。取消测试曾真实失败；修复将删除提交移到所有事件监听器执行之后，并在存档前清理技能数据。可选 Mod 测试还发现未安装 YSM 时 guard 仍被选中，`.2` 已修复该条件。移除 NightFall 时漏装 Avalon 会被依赖预检拒绝。

测试清单选择器另通过 6 项回归，包括多 ID 包、重复与冲突输入、替换保留端规则、主实例不变，以及额外依赖的实际 FML 解析。运行：`python -m unittest discover -s tests -p test_neoforge_fixture_selection.py -v`。

服务端处理器测试使用受控 `IPayloadContext` 和测试玩家，不代表实际客户端发包或菜单操作。YSM 检查验证真实 FML 列表下的选择器决定，尚未验证客户端目标类的实际 Mixin 应用，也未测试另装 `ysm_epicfight_compat` 的组合。客户端动画、GEO/YSM 模型、CS 内技能菜单与多人同步仍待实机验收。当前编译保留 19 条上游弃用警告，另有 deprecated/unchecked 提示；`.2` 重复构建哈希一致。
