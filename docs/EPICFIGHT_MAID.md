# 女仆 Epic Fight 扩展的 1.21.1 移植

本地移植目标为 Minecraft **1.21.1 / NeoForge 21.1.256 / Java 21**。女仆本体和 YSM GEO 兼容层的上游提交固定在 `sources.lock.json`，公开仓库只保存适配补丁、构建与测试工具。原作者、许可及资源权利声明见 [第三方来源](../THIRD_PARTY_NOTICES.md)。

## 构建

先完成 [基础运行环境与 Mod 主实例](BUILD.md)，在 X Minecraft Launcher 的 GoldCraft 主实例安装匹配的依赖。当前测试组合为 Epic Fight 21.17.3.1、Touhou Little Maid 1.5.3、GeckoLib 4.9.3、Invincible 21.15.8.2、NightFall 3.4.0 和 YSM 2.6.5。女仆本体还需要 Avalon 21.12.6.2，当前由 NightFall 的嵌套 JAR 提供；移除 NightFall 时须另行保留兼容的独立 Avalon。NightFall 和 YSM 本身不是女仆技能本体的强制依赖；当前构建保留对应可选适配类，因此编译使用这一完整组合。

```powershell
.\tools\Prepare-Sources.ps1
python .\tools\Build-EpicFightMaid.py
```

产物为 `build/epicfight-maid/ef_tlm-1.21.1-neoforge-1.3.5-goldcraft.3.jar`。其中已嵌入 YSM GEO Compat，安装时只需要这个 JAR。`build.json` 记录实际依赖哈希及源码修订，`compile.log` 保留编译信息。上游 `libs/Nightfall-Enhance.jar` 仅供编译可选适配，既不打包也不安装。构建会检查源码提交、描述文件和新增源码清单，失败时保留已有 JAR。

适配包括 NeoForge 事件与网络注册、Epic Fight 新接口、1.21.1 数据组件与配方、客户端渲染和菜单签名。GoldCraft 捕获实体网格时使用已有的 `ModMeshCapture` 作用域，将 YSM GEO 的蒙皮顶点交给宿主导出；普通 Minecraft 绘制继续使用其原有渲染路径。YSM 玩家模型与女仆 YSM 模型所需的其他兼容扩展仍需单独验证。

## 安装与同步

当前工作区已将 `.3` 更新到主实例及 A/B/服务端；四份 JAR 哈希一致，旧版已备份并移出各 `mods` 目录。实际 B 已重新连接，客户端完整动画和菜单操作尚未验收。

2026-10-10 修复真实客户端启动时的 `Duplicate client extensions registration for ef_tlm:skillbook`：NeoForge 21.1.256 仍自动调用旧 `Item.initializeClient`，之前的事件处理器又调用了一次。现在由普通工厂创建原自定义渲染扩展，仅在 `RegisterClientExtensionsEvent` 注册。构建和完整补丁重放通过，主实例/A/B/服务端同步后，真实 B 已成功入服并显示 Mod 背包物品；技能书渲染、技能菜单及动画仍需进一步验收。

把构建 JAR 放入 X 管理的 `sandbox/modpack-neoforge/GoldCraft-1.21.1-NeoForge/mods`，仅保留一个 `ef_tlm` 版本。然后使用现有 [统一 Mod 管理](MOD_MANAGEMENT.md)：

```powershell
.\tools\Sync-Modpack.ps1 -ValidateOnly -Loader neoforge
.\tools\Sync-Modpack.ps1 -Restart -Start -Loader neoforge
```

依赖校验和同步覆盖本机 A/B/服务端，日常只启动 B。无需分别安装女仆扩展或嵌套的 YSM GEO。JVM 需要重启；完整集群重启会按既定规则清空本轮 MC 建筑。

## YSM 玩家与 EpicYSM

用户安装的 [EpicYSM 1.1.3](https://github.com/Argorice/EpicYSM) 已通过统一管理同步到 A/B/服务端，四份主实例／运行实例 JAR 哈希一致。它负责 YSM 玩家与 Epic Fight 的兼容；女仆扩展内嵌的 YSM GEO Compat 负责另一条女仆渲染路径，不能互相替代。

GoldCraft 原来的保护逻辑会跳过所有非原版玩家 renderer，连已经兼容 YSM 的 EpicYSM 也被拦下。现在只在实际 Epic Fight renderer 为已核实的 `EpicYsmPlayerRenderer` 时放行，其余不兼容类型保留保护。同时保存 YSM 2.6.5 包装材质的原始 RenderType，导出时正确读取纹理、透明混合和深度写入，不在绘制热路径增加反射扫描或 GL 状态查询。

此前构建及 56 项 JUnit 通过；生产 JAR 的 Mixin 目标、构造器描述符和 Epic Fight 字段已独立复核。真实 B 日志确认酒狐模型转换及兼容 renderer 接管，CS 第三人称可见该模型，三角形网格导出无错误、无发送失败，GL error 为 0。连续画面还记录了剑的抬起与收回，但有人为输入重叠，不能作为完整连招或固定视角性能验收。

按 R 回到普通模式后停住的源码原因是缺失 YSM 世界动画上下文：GoldCraft 托管画面取消了原世界渲染，独立实体导出没有执行 YSM 的世界标志、动画提交和收尾。YSM 普通控制器因此在初始化后不再推进，Epic Fight 的独立姿态路径仍可运行。现补齐相同调用顺序，并在失败或嵌套时恢复原始标志、等待已提交任务；使用缓存 MethodHandle，不在每帧查找反射方法。保留 R 的原本模式切换及 YSM 优化设置。

新版完整构建通过 59 项 JUnit，覆盖正常、嵌套和部分失败后的清理，并已同步到主实例/A/B/服务端。B 已成功连接，但电脑控制在首次窗口清单调用报告物理 Escape，未继续发送游戏输入。普通模式待机、行走、挥动、R 往返及焦点恢复仍待实机验证；也不据此宣布透明遮挡、不可读模型或多人已兼容。

## 女仆 Molang 动画

`.3` 支持 `ysm.bone_rot('名称').x/y/z` 与常量 `math.pi`。每个动画器读取上次完整提交的局部旋转，初始值为绑定姿态，按女仆本体的 X/Y 负角度、Z 正角度约定换算；仅在整次组合成功后发布，避免读到半完成姿态。相同 UUID 对应新的实体对象时重新建立状态。

运行 `python tools/Test-YsmMolang.py`：72 项检查通过。测试从本地女仆 JAR 生成模型输入，公开仓库不包含模型资源。真实动画时间轴仅设置移动速度，即可得到主骨骼及依赖骨骼的非零旋转；没有预填预期姿态。新版实际 NeoForge 专服另通过 29 项女仆检查。

缺失骨骼仍按本体的空值语义返回数值 0，并限次记录诊断。zhiban 缺少 `FLeftM1`，winefox 女仆资源缺少七个引用骨骼，属于资源本身的边界；不声称所有女仆模型已修复。完整客户端动画与多人验证仍未完成。

## 验证范围

安装到主实例前可独立测试，不改变现有服务器与主 Mod 清单：

```powershell
python .\tools\Exercise-NeoForgeCompatibility.py --modpack --replace-mod build/epicfight-maid/ef_tlm-1.21.1-neoforge-1.3.5-goldcraft.3.jar --maid-checks
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

服务端处理器测试使用受控 `IPayloadContext` 和测试玩家，不代表实际客户端发包或菜单操作。YSM 检查验证真实 FML 列表下的选择器决定；未测试另装 `ysm_epicfight_compat` 的组合。客户端动画、GEO/YSM 模型、CS 内技能菜单与多人同步仍待实机验收。编译仍保留上游 deprecated/unchecked 提示。
