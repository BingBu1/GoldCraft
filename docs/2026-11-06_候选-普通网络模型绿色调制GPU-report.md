# 普通网络工作室模型绿色调制：独立候选与真实 GPU 对照

已完成原版 gamma 顺序补证、有限范围候选、实际 SDK 编译及 RTX 4090 新旧像素对照。候选 **8 组、366 项通过**；同一夹具的旧版 **7 组失败、1 组排除范围通过**。按协调方最新安排，先完成墙机工作，本候选独立保留，**没有合入共享 scene.cpp，没有完整客户端 DLL、部署、游戏操作或 Git 发布**。

这不是全部原版着色系统或实际怪物画面验收。候选限定普通网络非玩家独立模型的 mode0/fx0、普通受光材质；原特殊分类字段、viewmodel、附属武器、其他 renderfx/材质组合仍不扩大。

## 原版证据补充

沿用 [原颜色消费链更正](2026-11-06_更正-技能6绿色原版颜色消费链-report.md) 的三个原样本：MP `39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2`，client `f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd`，HW `c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783`。基址分别 `10000000 / 01900000 / 37100000`；下面为 VA，RVA 减相应基址。本轮结束再次核对原 DLL 与原 IDB 共六文件，SHA 全部不变。

新增证据目录：`analysis/csol-model-20261017/scenario/skill6-green-20261106/color-stage/`。

| 证据 | 已证事实 |
|---|---|
| `ida-classifier.json`、`ida-stage-bytes.json` | client `03AE90C0` 是表 slot+318，对应 HW `382FEED8→378300F0`；原谓词为实体非空且 `+D84==3 && +E58==-2`。语义字段名仍未知，不能把它称为“怪物分类器”。 |
| `ida-scalar-lighting.json` | `378EA080` 先计算照明，再按 `clamp(illumination,0,255)*4` 查 `39427520` 表并乘 `1/1023`。常量 `380195DC=255`、`380A27E8=-4`、`380ABE38≈1/1023`；负索引再从基址相减形成正表索引。 |
| `ida-gamma-producer.json`、`ida-gamma-cvars.json` | `3795E9A0–3795ECAC` 构造该 1024 项表。`3830BC24` 为 lightgamma、`3830BC40` 为 brightness；最终阶段使用 `1/gamma` 幂并缩放/截断为 0–1023。texgamma/gamma/lightgamma/brightness 名称由原 cvar 描述符直接验证。 |
| `ida-pow-import.json`、`ida-gamma-bytes.json` | `37F53DD4` 跳转 IAT `3800FEB4`，导入是 `_libm_sse2_pow_precise`。 |
| 先前 `ida-hw-consumer-378e5070.json` | `378E55A1–378E55E5` 在上述照明结果之后乘 lightColor，再乘 client 提交的 RGB/255。普通模式没有依赖强制 renderfx。 |

因此原固定管线 tint 是 gamma 处理后的调制。宿主 gamma 目标应乘 `T`；线性目标应乘 `pow(T,gamma)`。把 T 直接塞入 `pow(lightColor*lv,1/gamma)` 内，会得到过亮的错误颜色。

`stage_oracle.py` 实际执行新增原指令：14 个 IDA/PE 原字节窗口一致、24 个分类谓词组合、5 组 gamma 表及 scalar helper 组合通过。21760 次导入 pow 由 Python math.pow 提供，独立连续公式与原表最多相差一个量化单位；这是有界原指令证据，不是原游戏或 GPU。原固定/硬件 RGB 乘法先前 20 组合记录继续保留，没有改写旧证据封存清单。

## 候选与宿主范围

在写候选之前，已记录 `build/skill6-green-integration-20261106/implementation-contract.md`。冻结源来自 `build/monster-integration-20261106/client-full-02/effective-source-manifest.json`：603 个 native/csol 文件独立复制到 new/old，最后复核只有 new 的 `native/csol/client/scene.cpp` 与基线不同。

候选改动：

1. `DrawWorldWeaponPoints` 对有非零、非白 tint 的普通网络非玩家模型增加窄接管条件。否则原 v10 模型若没有特殊材质，会被 `asset->special` 门直接退回 stock，无法通过本次颜色消费者。新增接管要求普通受光材质；全零/全白普通资产继续原入口。
2. `DrawAsset` 从当前 wrapper 的 rendercolor 快照为每次绘制计算 RGB/255；三通道全零使用白。只在普通材质的 `draw.color` RGB 上乘，不改变 alpha、光照值、深度或事件顺序。
3. 复用现有 shader 的 gamma/linear color 转换，不新增全局 tint 或实体颜色缓存。wrapper 的清理、generation 与每帧复制维持原生命周期，避免绿色留给下一实体。

宿主 SDK 没有原 +D84/+E58 布局。候选以 world wrapper 的 source 是否为当前 `GetEntityByIndex(index)`、非玩家、非附属模型及 mode0/fx0 来界定本阶段的宿主所有权范围，**不宣称这个宿主条件等价于原特殊分类器**。特殊原字段的完整语义仍是更广还原范围的缺口。

候选不改变服务器 fx、rendercolor 或技能 6 行为，不硬编码绿色或模型名，也没有新增绿色皮肤。材质范围允许普通平面着色/遮罩旗标，排除 fullbright、additive、environment、外部 special/fade/common-color 组合。原三个普通怪模型的材质核查仍见前报告；本 GPU 夹具本次没有加载实际 MDL，也没有新增遮罩 alpha discard 专项。

## 编译与 GPU 证据

运行目录 `build/skill6-green-integration-20261106/new/` 和 `old/` 各自有 `result.json`、compile/link/case 日志与输入哈希。生产 scene.cpp 通过实际 SDK 编译；六对象中一个测试/scene TU 编译、五 helper 对象经源文件及真实 include 日志逐项 SHA 核对后复用。链接为单 job x86 Clang C++20/O3/ThinLTO/AVX2/precise，lld `threads:1 / opt:lldltojobs=1`。两组各 607 输入、555 实际依赖，终态再次核验零漂移。

第一次编译发现 SDK 不提供 `STUDIO_NF_MASKED` 名称、夹具变量未加 namespace；只修为工程已有的 64 位标志写法和夹具命名后通过。初次错误日志保留为 `new/compile-initial-errors.log` 与 `result-initial-errors.json`，不掩盖失败。

真实隐藏 OpenGL 3.3 上下文运行 GPU 为 `NVIDIA GeForce RTX 4090/PCIe/SSE2`。灰纹理 128、ambient144/shade48、非白 lightColor、非零 Lambert 项；数值预期由原顺序的独立公式计算，不读取生产 uniform 回填答案。

| 场景 | 候选 | 旧版 |
|---|---:|---:|
| opaque gamma / linear：绿白相邻、下一帧全零、部分零通道 | 2×53 项通过 | 2 组漏乘颜色失败 |
| post-scene gamma / linear，同样三次实体颜色 | 2×53 项通过 | 2 组漏乘颜色失败 |
| gamma→linear→post-gamma，保存 cvar 与目标故意相反 | 53 项通过 | 失败 |
| ClearScene、新会话零色、间隔后 wrapper 复用 | 53 项通过 | 失败 |
| 非 special 普通模型接管及白/零/非网络/模式/fx/特材拒绝 | 18 项通过 | 接管失败 |
| fullbright、special、非网络、其他 fx、附属武器排除 | 30 项通过 | 30 项通过 |

关键像素：

| 目标 | 候选绿色 | 同场白色 | 旧版所谓绿色 |
|---|---|---|---|
| gamma | `20,67,30` | `65,75,84` | `65,75,84` |
| linear | `2,18,4` | `17,22,28` | `17,22,28` |

同时验证 captured lightColor 不变、绘制时不重算光照、GL depth/stencil 状态恢复、当前实体/model/header 恢复、post-scene 不重复覆盖 opaque、相邻白色不串绿。

## 精确交接与后续

| 交接物 | SHA-256 |
|---|---|
| `build/skill6-green-integration-20261106/handoff.json` | `60cc188dda30cb9ebb62d2b9754c47bfb0148266617519109bfb6275eb71b7fb` |
| `candidate.patch` | `89baf3010a4a6d428a42043a30f82c18647f4e4676a63d0ccde096b2813fb3bf` |
| `new/source/native/csol/client/scene.cpp` | `9bfe976323c0dd5037ea8aba09ab4729a8e35fb7982c14147e0df20216ff2049` |

协调方接收后先复核原证与窄范围，墙机优先事项完成后再安排共享源码合并、完整 client DLL 和真实单 B 绿/白怪相邻画面。还需原模型实际加载、遮罩边缘、换图/重连、普通服务器与多人回归；没有实际固定视角测量前不作性能结论。

共享文档精确建议（由协调方应用，子代理未直接修改四文档）：

- PROJECT_STATE / PROGRESS：`2026-11-06 普通网络怪绿色独立候选已过新增原IDA14窗口/24分类/5gamma组、实际SDK编链及RTX4090八组366项；旧版七组失败。只改独立scene候选，未合并/完整DLL/部署/实机；墙机优先，后续再整合。见本日普通网络模型绿色调制GPU报告。`
- TODO：保留绿色未完成项，追加 `独立候选与GPU新旧对照已交接；待父核、共享合并/完整DLL及真实怪物绿白相邻/生命周期/多人验收，墙机当前优先。`
- GOAL：`绿色候选原指令/SDK/GPU通过不等于原模型实际画面、特殊材质全覆盖或完整目标验收；完整goal继续active。`
