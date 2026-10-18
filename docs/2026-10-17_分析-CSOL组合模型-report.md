# CSOL leapstrikegun 组合模型分析与预览

## 使用方法与结论

打开 `v_leapstrikegunex.mdl`，中文 HLAM 会自动加载 `v_leapstrikegunex_parts.mdl`，显示完整武器。普通版 `v_leapstrikegun.mdl` 使用自己的配套文件。保持 `models`、`sprites`、`shader`、`gfx` 的原目录关系；不需要手动调整附属件坐标。

“CSOL 预览 → 组合模型”提供收拢待机、展开、展开持续、收回四个动作。主体动作仍用主时间轴，附属件独立播放并跟随枪身。先打开 `_parts.mdl` 时，可用“打开完整武器”跳转到主体。关联素材导出从任一模型出发都会补齐另一份模型及其 JSON/贴图等依赖。

CSO 的组合关系来自客户端代码。两份 JSON 只包含自己的 `bindSpr` 材质绑定，不描述模型间的父子关系。客户端先画主体，再对附属模型按骨骼名称合并；相同名称的骨骼复制主体最终矩阵，不同名称的骨骼计算自己的局部动画并沿父骨骼累乘。这是运行时绘制两个模型，不会把 MDL 文件永久合成。

## 范围与样本

授权与边界见本地 `analysis/csol-model-20261017/parts-scope.md`：用户提供模型并要求组合预览，沿用指定工具目录更新授权。只分析工作区 DLL 副本；没有修改或启动原游戏，也没有改 GoldCraft 运行文件。无游戏网络操作。商业模型、DLL、反汇编和截图只留本地，不随公开源码发布。

| 样本 | SHA-256 |
|---|---|
| client.dll | f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd |
| v_leapstrikegunex.mdl | b2d9e152d024d952c8e19c5a1dcd52a2bf15f32afdc553902874f82b1482bd78 |
| v_leapstrikegunex_parts.mdl | c86833c5eb3fb90c0316fc2152235db070d9bd5968b17d18cfe5316dad8d4e14 |

EX 主体有 49 根骨骼，附属件有 11 根；只有 `root_leapstrikegunex` 同名。附属根的原始静态值与主体不相同，所以直接将两个文件放在同一原点也不足以正确组合。普通版附属件是 10 根骨骼，展开/持续/收回各 30 帧；EX 对应动作各 31 帧。

| 主体序列 | 附属件序列 |
|---|---|
| 0 idle | 0 idle |
| 1 shoot1 | 1 change_start |
| 2 shoot2 | 2 change |
| 3 draw | 3 change_end |
| 4 reload | 无对应编号 |

编号相同不表示动作对应。附属件状态来自武器逻辑，主体换弹或拔枪不能把附属件切换成同编号动作。

## Evidence → Finding → Path

所有地址均为上述 client.dll 的静态 VA，映像基址 `0x01900000`，不作为其他客户端版本的可复用运行偏移。

| E-id | source_ref | repro_command / 方法 | content_hash |
|---|---|---|---|
| E01 | `parts-models.json` | 本地 `python analysis/csol-model-20261017/inspect_parts.py`；读取 v10 骨骼、序列、事件与纹理表 | 1f020e4d193d3678505e3088634c06a58c9d1acdd100550499d20d38daf63421 |
| E02 | `parts-client-state.asm` | 在匹配 DLL 中反汇编 `0x028E9660` 起的登记与状态函数 | ce5976203862b3587459c9844ef66c5b50bf81069bc06124244197e6a55c5879 |
| E03 | `parts-studio-draw.asm` | 在匹配 DLL 中反汇编 `0x02ACB51A`，追踪附属模型第二次绘制 | 5a89876eec825ae969e57b5cbb17a520d633f49456a6d9ecebdb90c877ab3de4 |
| E04 | `parts-bone-merge.asm` | 在匹配 DLL 中反汇编 `0x02ACCA80`；导入槽 `0x02F7879C` 为 `_stricmp` | 79e7c5ef4f0e85c18fc6d06b41835fdc20d94e0c57c86a94e67896220b488d80 |
| E05 | `gl-parts-ex04/result.json`、`gl-parts-normal04/result.json` | 构建时开启 `HLAM_ENABLE_CSOL_SMOKE_TEST`，设置 `HLAM_CSOL_PARTS_OUTPUT` 为新的证据目录，然后以 `--portable` 打开相应主 MDL | n/a；逐文件 hash 随本地检查点保存 |
| E06 | `parts-export-independent.json`、`parts-normal-export-independent.json` | 逐文件重读原资源；NAR 使用独立 CSOToolLib 校验 CRC、解包并比较 SHA-256 | n/a；逐文件 hash 在回执中 |

上述相对证据路径均位于本地 `analysis/csol-model-20261017`。复现原二进制分析需要自行持有匹配样本；公开项目只保留实现补丁和本报告。

| F-id | severity | evidence_ids | confidence | location | status |
|---|---|---|---|---|---|
| F01：显式附属模型登记 | n/a_re | E01,E02 | high | `0x028E9700` 选择普通/EX 配套路径；`0x028E9660` 写入扩展实体槽 | validated（静态） |
| F02：动作时间独立 | n/a_re | E01,E02,E04 | high | `0x028E97C0` 写独立序列/帧率/起始时间；`0x02ACCA80` 消费 | validated（静态及查看器） |
| F03：同名骨骼合并 | n/a_re | E03,E04,E05 | high | 第二次 studio draw，骨骼名不区分大小写；匹配复制矩阵，未匹配沿局部层级计算 | validated（静态及查看器） |
| F04：自包含导出 | n/a_re | E05,E06 | high | 两份 MDL 与各自资源/JSON 一并导出 | validated（查看器/独立文件核验） |

P01，`path_type=callflow`：武器类型选择配套路径 → 装备时登记附属模型，初始序列 0 → 武器状态更新附属序列 0/1/2/3 及独立时钟 → 主体 studio 绘制并保存骨骼 → 附属模型按名称合并骨骼后再次绘制 → 两套 JSON 分别选择自己的 SPR 帧。引擎实体字段与反编译中的参数名称只作本样本研究定位；实现不加载商业 DLL，也不在查看器中执行其代码。

## 实现与验证

实现位于独立 `CSOLParts` 模块、预览资源生命周期与骨骼变换器。按明确武器注册表自动配对，并验证动作/根骨骼结构；不对任意 `_parts` 文件做通配合并。骨骼映射在载入时建立，绘制时不做目录搜索、读文件或上传纹理。附属模型不进入主体 MDL 序列化或 undo；重新载入、材质重建、离线打开及缺失/截断文件均走相同检查路径。

- 编译：Clang 22.1.7，x86/C++20、O3/ThinLTO/AVX2、precise FP，独立 maxJobs2；通过。
- 自动格式检查：698 项通过。
- 数值验证：普通版与 EX 各 360 组姿态，包括全部主动作、全部附属动作及镜像；参考公式为 `主体根 × inverse(附属根) × 附属骨骼`，最大矩阵分量误差约 `5.72e-6`。
- 实际 RTX4090 OpenGL/Qt：自动组合、取消合并负对照、主暂停、独立动作、换弹不重置附属时钟、时间轴定位、循环/末帧保持、GPU 释放/重建、中文动作/帧定位/配置/完整武器按钮、MDL/undo 不变均通过。
- 动态贴图：EX 170 帧、普通版 171 帧，共 341 帧与 Pillow 独立 RGBA 解码一致。
- EX 导出 183 文件/67,080,645 字节，普通版 184 文件/72,163,188 字节，缺失均为 0；每组 73 个 NAR 条目独立重读核验。EX 从附属件出发导出也得到 183 文件并含主体。
- 两组导出树在清空补充游戏目录后重新打开，组合画面与原资源同姿态图像 hash 一致。
- 既有复活枪、普通灵魂收割者的扩展事件、两把 JSON 示例武器的实际 GL/Qt 回归通过；最终产物及包核验见领域检查点。
- 最终 40 文件交付包在清空开发 PATH 后通过实际 GL/Qt 测试；与构建目录比较的 17 张图中 16 张逐像素相同。连续播放的 `parts-loop.png` 有 7/614400 像素差异，最大单通道差 1，实际审阅无可见变化。原因未唯一确定；原严格位级检查的失败回执保留，另存逐像素审阅记录，不宣称全部连续图位级一致。
- 已按用户授权更新指定工具目录的 40 个文件，逐文件 hash 与受测解压包相同，原 INI 字节保持，旧版已备份；没有强制关闭用户进程。部署是文件级核验，没有额外在用户目录运行自动输入测试。

交付 ZIP SHA-256：`46df761acbd448568bf3210b808f82295e40db0fa66479ed1c89123c707104c9`；EXE SHA-256：`b9e7a63ca7ca2e9a2850bac4a6fa0087cad357a81fbf5668eff203cafe919f6c`。固定上游 `4df74a58a50438b8f4b974e04ab9f24fdfcbb811` 上的累计 129 文件补丁已独立回放核验，补丁 SHA-256 为 `8eb7beb40c3ee875149c5e7bb729c2446acea32c69ac2e6d85de5e49e8ccd9a1`。本地证据：`package-parts-verification-reviewed.json`、`user-tool-parts-deployment.json`、`hlam-source-replay-parts.json`；不发布商业资源或私人配置。

时间线：2026-10-17 完成模型/JSON 静态检查 → 定位客户端登记、独立动作与骨骼合并 → 实现预览/导出/中文控件 → 实际 GL 和离线核验 → 独立资源比对及交付检查。早期测试将普通版误按 EX 的骨骼数/帧数断言，已按各模型实际数据修正；`gl-parts-normal02` 实际输入为 EX，不能用作普通版证据。正式普通版证据为 `gl-parts-normal04`。

## 验收边界

本次完成这两组模型的组合预览；没有完成 CSO 同相机、同姿态、同光照的逐帧游戏对照。附属动作可独立选择，尚未模拟完整充能/跳跃/落地状态机、世界特效或附属件声音事件。仍不得宣称完整 CSO 游戏表现完全一致。更广泛材质通道与其他武器逻辑沿用原领域未完成清单。
