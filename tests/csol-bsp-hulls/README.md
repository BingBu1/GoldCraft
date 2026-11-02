# CSOL BSP hull 回归

`main.cpp` 使用真实 ReHLDS SDK 数据结构。`run.py` 从父任务已冻结的 `model.cpp/world.cpp/mathlib.cpp` 按完整函数提取实际 loader、hull 选择和 clipnode 查询方法，并提取 `Mod_LoadBrushModel_internal` 尾部实际子模型复制循环。生成的第三方方法体只放在build目录，不重新作为项目源发布。

明确替代边界只有 Hunk 内存分配、模型索引/名称注册和测试文件读取；真实 BSP 平面、clipnode、model headnode 由生产方法加载和查询。未启动完整引擎，不代替真实STEP移动、旋转brush或逐Boss战斗。

首轮 `build/csol-bsp-hulls-20261102/attempt-01`：x86 Clang/C++20/O3/ThinLTO/AVX2/precise，编译3.87秒、链接1.14秒、运行0.35秒，2683检查/0失败；直接登记输入前后身份不变。测试消费最终 `csol_bsp_hulls.h` SHA256 `ec029100408045b3ada8c348c0e00801ba84f74d5a15fec3a68446289a567485`。

覆盖实际8张BSP：`zs_deadend`、`zs_allout1/2/3`、`zs_electron2`、`zs_multiboss`、`cs_assault`、`de_dust`；以及从原始appearance目录取得的18种大Boss bbox。包括真实entity schema与纯文本/注释/缺项/损坏的区分、默认和五图例外尺寸、旧ReHLDS尺寸及仅居中的SOLID对照、inline model共享/独立headnode、平移brush查询、普通图cvar行为和复用模型记录时的profile复位。

未来大实体仍由已有通用bbox宽度选择hull2；此生产入口不读取怪物类型、Boss ID或怪物模型名。18种已知bbox是实测样本，不表示已逐Boss验证玩法或所有地图碰撞。

重现先用 `run.py --prepare <新的build目录>` 固定输入，确认唯一CPU窗口后用 `--run <该目录>`。准备和执行分离，任何失败保留，禁止覆盖已有attempt。当前runner固定2026-11-02已核收的父候选；新引擎版本必须先更新冻结来源与预期header身份。详细结果见[领域报告](../../docs/2026-11-02_验证-CSOL通用大体型BSP碰撞-report.md)。


公开副本内置同一 Windows 命令行拆分函数，避免依赖未公开的其他构建脚本；可通过 `GOLDCRAFT_CSOL_MAPS` 指定合法取得的五张例外 BSP 所在目录，默认使用工作区专服地图目录。此路径适配和入口加载单独检查，未重新执行上述引擎回归。

这是固定候选的证据重放工具。准备前需要对应的 `engine-candidate-01/source`、`csol-hd-http-unified-20261031/source`、`engine-baseline-lookup.json` 及其指向的编译器/环境/SDK文件，另需上述八张地图。生成数据和原游戏地图不随仓库分发；新的独立构建须生成本机匹配的元数据和冻结来源，不能直接复用记录中的个人机器路径。`--help` 可在公开检出中独立加载。
