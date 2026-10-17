# CSOL 模型扩展事件与资源导出

`v_soulreaper` 的 Draw 缺少一个由模型事件触发的挂点 SPR。它与同名 JSON 中替换枪身贴图的 `bindSpr` 是两条独立通道。此次新增通用 `#` 事件解析/预览、中文事件说明，以及保留资源相对目录的关联素材导出。

## 范围与证据

授权范围来自用户要求分析其本机 CSOL 模型/DLL、修改 Half-Life Asset Manager、汉化并更新指定工具。仅静态分析已复制模块，修改和构建工作区的查看器；不修改游戏客户端或受保护的 Steam 安装，不连接游戏服务端。商业 DLL、资源和逆向数据库不随源码发布。

本地 scope：`analysis/csol-model-20261017/events-scope.md`；持续记录与时间线：同目录 `EVENTS-EXPORT-IMPLEMENTATION.md`。

固定 client.dll SHA256 为 `f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd`；hw.dll 为 `c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783`。以下 VA 仅适用于这两份模块，不是通用跨版本地址。

| Evidence | 来源与复现 | Finding / Path |
|---|---|---|
| E-01 | 本地 `analysis/csol-model-20261017/event-soulreaper-fixture.json`；按 `StudioModelFileFormat.hpp` 的 v10 序列表读取模型 | 模型 SHA256 `afd13dbd80e4ab583fb03262282879509e6ce609c79a7d5777762a119e92d6b3`；drawA 为序列 3，帧 5，事件 5061；SPR 362 有 35 帧 |
| E-02 | `event-client-dispatch.asm`；IDA 打开固定副本，反汇编 client VA `02a41500`，检查 `02a41557` 的 # 分支 | # 分支先于普通事件 switch。初始挂点 `(eventId-5000)/10`，effects table +14c 调用扩展解析器 |
| E-03 | `event-parser.asm`、`event-parameter-jumptable.json`、`event-resource-loader.asm`；hw VA `3785a7a0`、`378569a0` | I/E 从真实跳表区别；I362 直接格式化为 sprites/muzzleflash362.spr，无须查询资源编号表 |
| E-04 | `event-sprite-render.asm`；hw VA `378859a0`，关键分支 `3788642e`、`37886591` | D1 使用 SRC_ALPHA / ONE_MINUS_SRC_ALPHA；effects40 翻转 U 坐标 |
| E-05 | `event-hand-and-api-chain.txt`；构造器 `37a21ce0`、getter `37a33580`、写入器 `37a5d190`，cvar 字符串 `38097b08` | H1 依据 cl_righthand，在左手时翻转。effects table `383074f8+14c` 的指针为 `3785a7a0` |

E-02 至 E-05 的内容身份由固定模块 SHA256 和 VA 约束；本地原始证据的独立 hash 随领域交接清单保存。复现需要用户持有相同版本模块，不能从公开源码仓库取得商业样本。

F-01（n/a_re，high，validated，E-01–E-03）：此前查看器处理声音事件和 bindSpr，漏掉扩展挂点事件，因此 JSON 正常仍不会显示 Draw。路径为 MDL 事件→HUD # 分支→R_MuzzleFlash2→数字 SPR→temp entity→跟随挂点→sprite renderer。

F-02（n/a_re，high，validated，E-03–E-05）：Draw 的 `S0.05` 是实数比例，不能沿用复活枪专用接口的千分比整数；`U` 进入覆盖队列，`D1` 使用透明混合，`H1` 遵循左右手。原始反编译的局部变量推导存在错误，参数对应以跳表和原始指令为准。

## Draw 的参数

原事件：`#I362 S0.05 R0 F0 P30 T999 A1 L0 U1 O0 X6 D1 H1`。

- `I362` 加载 `sprites/muzzleflash362.spr`，256×256 像素、35 帧。
- `S0.05` 得到宽高各 12.8 模型单位；`X6` 持续跟随挂点 6（模型 bone20）。
- `F0` 从第 0 帧开始；`P30` 为每秒 30 帧；`A1/L0/T999` 将寿命设为总帧数除以 FPS，约 1.167 秒。
- `R0` 不随机旋转；`U` 选择第一人称覆盖队列；`D1` 透明混合；`H1` 在左手时水平翻转；此版本的 `O` 读取后未使用。

同一模型还引用 muzzleflash356–361，包含同帧双挂点事件。按武器名称模糊搜索会漏掉这些数字命名的资源。

## 实现

`CSOLEvents` 负责参数和中文说明；`CSOLPreview` 在载入时解码/上传缓存，在模型时间轴上维护独立的多事件寿命。暂停、定位、循环和非循环末帧继续消耗寿命；切换预览动作清理旧效果。绘制复用当前骨骼挂点和相机轴，不在每帧查盘、解码或上传。

`CSOLExport` 复用资源查找优先级，收集模型/T 模型/序列组、旁置 JSON、编号 SPR、声音事件、材质和环境六面图、角色手部及同武器命名素材/HUD 引用。NAR 资源解出后统一放入 `common` 资源树；不平铺覆盖同名文件。新建目录并生成 `resources.json` 的来源、用途、SHA256 和缺失清单。

## 验证边界与进度

2026-10-17：先确认模型事件和原 DLL 调用链，随后实现事件及导出候选；独立 Clang22 x86/C++20/O3/ThinLTO/AVX2/precise 构建使用最多 2 jobs。新增格式/路径回归与真实 GL 驱动已写入，执行结果将在交付检查点补齐；本段不作为测试通过或已部署证据。

后续已执行的验证如下，替代上一段的候选状态；部署与最终包见独立交付检查点。

| Evidence | 验证结果 | 限制 |
|---|---|---|
| E-06 `hlam-events-build{02,04}.log`、`ctest-events.xml`、`events-formats-test.log` | 最终构建、698 项解析/导出路径检查通过；涵盖编号 SPR、JSON、声音、外部 T 模型/序列组、禁止覆盖和路径逃逸 | 第一次增量构建遗漏 PKG_CONFIG 导致依赖下载 404；恢复完整既有环境后缓存恢复并构建通过，未改依赖版本 |
| E-07 `gl-events-soul03/result.json` 与 PNG | 原模型 9 事件/7 数字 SPR/7 JSON 绑定自动加载；第 4/5 帧边界、Tick、暂停、循环、结束后到期、双挂点、H 镜像、GPU 重建、中文说明/导出按钮，GL0 | 无 X 的世界位置和 E 角色状态边界仍在；中文窄面板改成摘要，完整参数在悬停提示 |
| E-08 `events-export-independent.json` | 197 文件、95,584,893 字节与原资源逐项 hash 一致；其中 73 个 NAR 文件由独立 CSOToolLib 解出核对；缺失清单为空 | 是本模型及关联资源的静态依赖，不是对任意 DLL 动态引用的完备性证明 |
| E-09 `gl-events-soul03/draw-offline.png` | 清空补充游戏目录，导出模型自动重开；事件与 JSON 资源全部来自导出树；Draw 与原资源截图 hash 相同 | 截图来自 RTX4090 查看器，不是 CSO 游戏 |
| E-10 `gl-events-json-{soul01,frost01}`、`gl-events-revive02` | 两个既有 JSON 模型和复活枪全部回归通过，模型字节/undo 未改 | revive01 runner 误用空 auto-fixture，改回已核实 revivegun fixture 后通过；失败记录保留 |

F-03（n/a_re，high，validated，E-06–E-10）：缺失的 Draw 已接入查看器，导出按同样的查找顺序生成可独立重开的资源树。P-02（solve）：加载模型→预载事件/JSON→实际时间轴和 GL 绘制→依赖导出→独立原资源 hash→离线重开同帧画面比较。

最终累计补丁为 123 文件，回放/敏感扫描通过，SHA256 `ec5b5ecb2b2efcb459ff4b575f8c663d002033a8987cd00b1bf46d70e829b8d1`。便携 ZIP SHA256 `8d54fc9e11c596654c55eac3196e95af892da2c0e0df004d261bea2ba3620b74`，EXE `788036981564616b9e3223a64a3adec2861cba90b54898b37eb0e49bc13f19fa`。最终解压包清空开发 PATH 后通过同一真实 GL/Qt 测试；12 图与构建版相同，连续动画到期图有 226/614400 像素差异，双方均验证事件已到期，未声明该图位级一致。用户指定工具已更新 40 文件并逐 hash 核对，INI 保持，无强关用户进程。清理旧备份被执行策略拦截，暂保留三份，不影响部署。证据为 `package-events-verification.json`、`user-tool-events-deployment.json`；共享记录与发布交主协调。

原游戏同姿态/同相机画面对照仍未完成，不能宣称所有效果与 CSO 完全一致。事件 SPR 当前支持 type2；E 备用编号依赖游戏角色状态，独立预览采用 I 并提示。无 X 的效果保留首次绘制位置，未复现完整世界模拟；手动预览参数和未保存编辑不写入素材导出。仅静态可发现依赖与命名关联不能穷尽 DLL 在运行时生成的全部资源。既有 fadeTex/bindTex/colorTex、v20/v21 等边界保持。
