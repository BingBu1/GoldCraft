# CSOL 模型加载与材质效果研究

## 原生模型 JSON 动态贴图（当前检查点）

2026-10-17：修复漏读模型同名 JSON 的问题。`bindSpr` 将 SPR 帧替换到 `texIndex` 对应的现有网格贴图槽，沿用 UV、骨骼和材质混合；此前的复活枪独立挂点实现不能覆盖这条通用路径。现打开模型即可读取旁置 JSON，并从资源树/NAR 查找 SPR。用户指定工具目录已更新，原配置字节完整保留。中文由模型编写。

| Evidence（本地 analysis/csol-model-20261017 下） | Finding | Path / 边界 |
|---|---|---|
| `bindspr-loader.asm`、`bindspr-lookup.asm`、`bindspr-oracle.json` | 固定 hw.dll 的 JSON 加载和帧计算已核实；600 组原始 x86 指令仿真对照通过 | 同步模式使用模型帧减 offsetFrame 加 startFrame；非同步按 sprFPS×frameRate 累进且始终循环。end 默认末帧索引，循环采用整数取余后加 start。时钟按贴图槽保留，不因每次序列切换而重置；仿真辅助调用按持续可见资源桩化，未运行完整游戏 |
| `hlam-json-decode.json`、`hlam-json-decode-comparison.json`、`ctest-json.xml` | 675 项解析/边界检查、279 份真实模型 JSON、434 帧独立 RGBA 比对通过 | 新增普通 SPR v2 帧支持；无效动作/贴图槽跳过并提示，字段按引擎精确大小写读取 |
| `gl-json-soul01/result.json`、`gl-json-frost01/result.json` | 灵魂收割者 5 绑定、寒冰武器 7 绑定；实际 Asset Tick 自动播放、暂停/定位、全部动作、资源释放/重建、模型隔离/重载、MDL/undo 不变，RTX4090 GL0 | 动态贴图使用模型表面；不逐帧读盘/解码/上传。未列出的动作保留原贴图 |
| `gl-json-revive01/result.json` | 既有复活枪挂点/左右手/材质及 Qt 流程回归通过 | 专用挂点与通用 JSON 表面动态贴图独立 |
| `package-json-verification.json`、`user-tool-json-deployment.json` | 清空开发 PATH 的最终解压包两模型通过，12 张关键截图逐字节一致；16 份原资源 hash 不变；40 交付文件安装后 hash 一致 | 用户主动关闭旧版后更新，保留 INI，未强关窗口、未写游戏安装 |
| `hlam-source-replay-json.json`、`hlam-source-scan-json.json` | 117 文件源码补丁固定上游回放 LF hash 一致，敏感扫描无命中 | 根 index 未写，发布仍由主协调串行完成 |

固定 hw.dll SHA256：`c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783`。关键 RVA：JSON 加载 `0x3b47d0`，bindSpr 消费 `0x3b3d00`，末帧默认值 `0x3b6768`，sprite 帧选择 `0x759e20`。fadeTex 位于独立路径 `0x3b3810`。

最终 ZIP `HLAM-CSOL-zh-20261017-json.zip`：40 文件/12,095,372B，SHA256 `4659e13f635cac363f952af40fe9d0cc352d496b92090cba6d66e85116e9d286`。EXE SHA256 `9ab50924f817fa0080368fa50a7f55d1c0ae927e6bb2c5a8556504814b815430`。117 文件补丁 SHA256 `71715c18da3a827d4ea758b267900222c59cde1d7b83fe01c9e3973c23121312`。包无商业资源或私人设置；完整实现和失败审计见本地 `JSON-IMPLEMENTATION.md`。

验收边界：`fadeTex`、`bindTex`、`colorTex` 和 SPR v2 分组帧尚未实现，资源状态会提示；寒冰模型的附加淡入淡出/颜色通道不能以 bindSpr 测试替代。CSO 原游戏同姿态画面、光照/后处理、世界事件、角色编号、v20/v21、PAK/NAR 音频仍待。上述真实 GL 是查看器证据，不是 CSO 完全一致性验收。

## 自动加载与原始材质（先前检查点）

2026-10-17：根据用户“应按载入目录自动获取资源、效果仍与游戏不同”的反馈，已将查看器改为打开模型时自动发现资源、识别复活枪并绑定动作。无需逐模型选择 SPR 或填写挂点/缩放。中文界面仍由模型直接编写，未使用翻译服务。以下检查点取代历史章节中的“缺少 @004、左手/动作未实现”等状态；**CSO 完全一致性仍未验收**。

### Evidence → Finding → Path

| Evidence（本地 analysis/csol-model-20261017 下） | Finding | 实现路径及边界 |
|---|---|---|
| `nar-all-materials.json`、`nar-original-004.txt` | `shader/@004.txt` 实际在 `dstrike.nar`，type 0 / env 4 / mulBright 0.5 / addBright 0；env 4 为 wm，六张面也在基础 NAR | 自动搜索模型祖先、同级 common/locale、基础挂载目录及 Data；找不到任意位置的安装时可一次设置补充游戏目录。不能从孤立 MDL 推导任意磁盘安装位置 |
| `hlam-auto-action-chain.json`，client SHA 同下表 | `cl_righthand` 默认 1 选择普通 SPR，0 选择 `_left`；同一玩家条件选择特效 1/2 与序列 +6；拔枪注册 1 秒到期后转待机；weapon+0x174 为换弹标志 | 自动缓存八组左右手/普通与复活/待机与拔枪资源；序列 1/7 隐藏圆环，12 暂不绘制。其他武器只自动应用已知通用材质/手部，不猜测同名特效绑定 |
| `nar-independent-expected-auto.json`、`nar-native-comparison-auto.json` | 原生 NAR 读取与独立工具的 41 资源、10,560,074 字节 SHA 全一致，覆盖 raw/encoded/encoded+compressed；含两个 `#512512` 外部手部 BMP | 原创只读索引/解码器，CRC、长度、边界及缓存限制；不执行商业 DLL、不整包解压；未实现 PAK 直接读取 |
| `ctest-auto-final03.xml`、`nar-path-negative.log` | 648 项解析/目录/损坏数据检查通过；旧路径实现确实失败，修正 URL 的 DecodedMode/FullyDecoded 后含 `#`、`%` 文件名通过 | 防止外部 BMP 读取异常连带阻断角色手部。原手部、男女网格选择继续只覆盖预览，不写 MDL |
| `gl-auto05/result.json`、`package-auto-smoke01/result.json` | RTX4090 GL0；首次自动载入原始 @004/wm、八组特效、拔枪转待机、换弹隐藏、资源重建、Qt角色/配置/帧定位/重载及 MDL/undo/禁用还原通过 | 最终 ZIP 解压后清空开发 PATH，直接打开用户提供的原模型路径通过；这是真实查看器绘制，不能替代 CSO 同姿态画面对照 |
| `package-auto-verification.json` | 五张关键 GL 截图与编译输出逐字节一致，原模型 SHA 未变 | Qt 主窗 grab 不含原生 GL 子窗口；看模型效果应使用 auto-right/left 等实际 FBO 图像 |
| `hlam-source-replay-auto.json`、`hlam-source-scan-auto.json` | 114 文件文本补丁回放至固定上游后 LF 内容一致，私人路径/token/binary patch 扫描零命中 | 根 Git index 未写；商业资源、反编译结果、依赖和二进制不进入源码发布 |

动作静态证据使用同一固定 client.dll，本文继续以 RVA 表示位置：cvar 注册 `0x11581aa`，状态更新 `0x8f87f0`，序列选择 `0x8f89d0`，idle `0x8f98d0`，draw `0x8f9af0`，换弹补弹/清除标志 `0xcd1b88`，时间常量 `0x16b2bdc`。SPR 的 attachment5、千分比尺寸、加色和覆盖队列沿用下节已核实的引擎证据。`mulBright` 虽为 0.5，但实际 CSO_ENV shader 注释掉了该乘法，因此未擅自降低预览亮度。

目录搜索先使用模型所在松散资源树，之后相邻目录/已记忆游戏目录，最后只读查询 NAR。包内 `cstrike_chn` 前缀按原样保留。fstrike、estrike、dstrike、cstrike_chn、cstrike、valve 顺序是查看器确定性回退策略，尚未证明所有游戏版本都按此顺序挂载。每帧只选择已缓存贴图，不做目录扫描、资源解码或 GPU 上传。松散模型声音事件加入自动搜索；NAR 声音尚未接入。

最终界面为“预览 / 高级设置 / 资源状态”三页，角色选择立即更新，自动模式禁用手动特效参数。报告关联资源用于诊断；光束、爆炸等未核实的事件不会自动叠在枪身。

本轮失败记录保留：首次本机配置误写 `HLAM.ini`，实际便携文件应为 `Half-Life Asset Manager.ini`，新增原始材质断言因此抓到失败；修正配置后，实际基础包暴露了带 `#` 路径解码问题，补充负例和修复；中间成功的解码或截图未冒充完整通过。最终以 gl-auto05 和 package-auto-smoke01 为准。

交付：`HLAM-CSOL-zh-20261017-auto.zip`，40 文件，12,067,356 字节，SHA256 `37f067440a535350fd6450a28e0365124a5af1860e5bf4d7f88dab73cb11ba2e`。源码补丁 SHA256 `3d4b9fd86c528da3391ae8f6f9fd0aeaf76d965ebfa02fc3d6e26628fb145285`。新增 bzip2 许可，动态依赖取当前构建输出，ZIP 不含私人目录设置或商业素材。本地启动入口单独保存用户已提供的游戏目录；公开干净包只在目录无法推导时需设置一次。

剩余：CSO 同姿态/时间/相机画面对照、游戏光照及后处理、完整世界特效与事件、加密角色编号映射、v20/v21、原始 PAK 读取和 NAR 音频。通用构建脚本仍只有语法验证，本机固定 Clang22 x86/C++20 环境已实编译；不能扩大为干净机器复现。当前完成的是本轮自动化与已确认表现路径，整体一致性要求继续保留。

## SPR 悬空纠正（先前检查点，当前状态以上文为准）

用户指出旧预览的 SPR 不在枪身上。旧 GL 冒烟测试指定了模型原点加 `(20,0,3)` 的临时位置；该截图只能证明动画解码/绘制成功，不能作为复活枪效果还原证据。现已移除这两个临时摆放点，加入原客户端和引擎支持的复活枪预设。

E-ATTACH-01：本地 `analysis/csol-model-20261017/hlam-attachment-chain.json`，SHA256 `8c870c6dc2be14be097f0f3e93f3b5b530004d1f297eb1a372b9715bdbcf2090`。由 `capture_attachment_evidence.py` 对固定哈希 DLL/MDL 副本读取生成，包含原始字节反汇编、接口指针、浮点缩放常量及全部模型挂点。脚本及商业样本仅保留本地，第三方复核需持有同版本样本。此前 `revive-idle.json` / `revive-draw.json` 提供客户端调用参数。

F-ATTACH-01（n/a_re，静态已验证，置信度高，证据 E-ATTACH-01）：接口槽 `+0x348` 转到 hw RVA `0x969b10`；创建操作转到 `0x75e060`。传入挂点为 0-based 5，对应模型骨骼 24 `sprite_attach01`，局部原点 `(0,0,0)`。引擎把挂点加一存入实体字段，绘制查找 `0x7596d0` 再映射至附件数组，不能在预览中额外减一。

F-ATTACH-02（n/a_re，静态已验证，置信度高，证据 E-ATTACH-01）：28/40 是除以 1000 的缩放值；对本样本的 256² SPR 得到宽高 7.168/10.24。idle1 为 20 FPS，idle2/draw1/draw2 为 30 FPS。SPR 类型 2 使用相机基向量，rendermode 5 使用 ONE/ONE 加色；参数 flags 9 选入队列 3，`0x7ed640` 绘制前关闭深度测试，renderamt 为 250。不能拿带 alpha 的加色或世界场景深度遮挡替代该特定预设。

P-ATTACH-01（callflow）：client `Initialize` 复制接口表 → 引擎表槽 155 getter → 引擎对象槽 `+0x348` → 附着 SPR 注册 → 当前动作变换 `sprite_attach01` → SPR 相机平面/缩放/加色绘制。模型内只有挂点，武器代码补齐外部资源与参数，所以普通 HLMV 单独加载 MDL 无法得知完整效果。

E-ATTACH-02：`gl-attachment02/result.json`，SHA256 `f7c51093b105a37c7f28502b91b25a94fc0cc8801c0256da25a9eafc5897774e`。RTX 4090 实际 OpenGL 测试通过：四个 SPR 预设，待机帧 0/84/168、拔枪帧 0/10.5/21，附着绘制与独立取得的当前骨骼挂点参考绘制一致；GL 错误 0，MDL 序列化/undo 不变，资源释放重建、禁用还原和 Qt 配置/角色/切换/重载均通过。截图 `preview-frame12.png`、`attached-draw-21.png` 可见效果已在枪身圆环附近。`ctest-attachment.xml` 的解析器回归通过（606 项内部检查）。

复现：用启用 `HLAM_ENABLE_CSOL_SMOKE_TEST` 的构建，在工作区副本设置 `HLAM_CSOL_SMOKE_ROOT` 指向含 models/sprites/resources 的样本目录、`HLAM_CSOL_SMOKE_OUTPUT` 指向新的空证据目录，运行 `HLAM.exe --portable <模型副本路径>`。测试自动退出并写 result.json；输出目录不可复用。

时间线：用户指出悬空 → 追查 getter/vtable/引擎注册及绘制 → 增加匹配骨骼和 SPR 名的预设 → 两次专项构建 → GL/Qt 与解析器回归通过。尚未进行 CSO 原游戏同姿态像素对照；动作状态切换、左手资源、缺失 `@004`、角色编号表、v20/v21 仍未完成，整体一致性目标保持开放。

交付复核：最终包 `HLAM-CSOL-zh-20261017-attachment-r2.zip`，12,018,178 字节，SHA256 `073fe9b0fd3d01bd31a461f64741997508a1425b695ea1267b20108adf55b43b`。39项文件无游戏素材，109文件源码补丁回放 LF 规范化内容一致；敏感路径/token/binary patch 扫描零命中。最终解压包清除开发 PATH 后，`package-attachment-smoke-r2/result.json` 全部通过，三张关键渲染截图与 `gl-attachment02` 逐字节一致。测试增加专用实例名，避免把测试样本转发到用户打开的编辑器；最终编译输出独立目录，未替换已打开的程序。`ctest-attachment-final.xml` 回归通过。此前包的坐标与交付哈希由本检查点替代。

日期：2026-10-17。状态：已实现 HLAM 中文 CSOL 预览，解析器、独立解码比对及真实 GL/Qt 集成测试通过。用户指出旧截图 SPR 悬空；已查明旧测试使用临时坐标，现从引擎确认复活枪挂点和缩放并修正源码，专项构建、四种预设及六个动画姿态的真实 GL 验证已通过。尚未完成与 CSO 游戏画面的完全一致验收。

用户要求以提供的 CSO 客户端为准，正确加载模型并还原动态贴图等效果，最终效果须与 CSO 一致。现已提供 `v_revivegun.mdl`，本轮回答其中间炫彩为何在 HLMV 中缺失，以及不同角色如何使用不同手部贴图。本领域负责模型、序列、材质和效果触发链；完整 GoldCraft 目标与共享构建、部署和发布继续由主协调管理。

Scope：仅分析用户指定的本地 DLL 副本、模型及最小配套资源；无网络取样、游戏修改或运行时注入。样本复制、解析、反汇编与解码预览均在忽略的 `analysis/csol-model-20261017/` 内。提取资源树与安装 DLL 的资源版本配对未独立核实；资源名匹配不能代替实际加载验收。

## 样本与证据边界

只复制四个必要模块到工作区 `analysis/csol-model-20261017/samples/`。原游戏目录只读，所有 IDA 数据库、反汇编和反编译输出均留在忽略的 `analysis/` 中，不属于源码发布内容。

| 模块 | 字节数 | SHA-256 |
|---|---:|---|
| client.dll | 33145824 | f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd |
| hw.dll | 20220896 | c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783 |
| mp.dll | 34789344 | 39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2 |
| FileSystem_Nar.dll | 1365984 | e78fe49d146d10aa6df28c84d3599cc44126bdeee0c23fdcd89a58d5644815cc |

四个文件均为 PE32 / x86（Machine 0x14c）。PE 时间戳均落在 2026-09-18；时间戳不能证明产品版本，文件版本字段为空。本轮身份以完整文件哈希固定，不套用其他 CSO、CS1.6 或 ReHLDS 的地址。

本文地址全部为模块 RVA。IDA 中显示的首选加载基址分别为 hw `0x37100000`、client `0x01900000`，实际运行时可重定位；不得将本文 RVA 当成跨版本固定地址。

## 已确认的加载链

### F-01：版本号存在真实分支，改头部数字不足以适配

- 证据：E-02。
- 位置：hw RVA `0x7b0870`。
- 静态结论：函数从模型头 `+4` 读取版本，区分 10、20、21。20/21 分别向 hw RVA `0x99efa0` 传入模式 0/1，之后还有逐子模型处理路径。其他版本落入清空头部、写入 `bogus` 名称等处理。
- 限制：尚未还原两个模式的完整处理、全部字段和顶点数据，也没有真实模型加载实验。不能将 20/21 直接改成 10 后宣称兼容。

### F-02：MDL 可能需要外部序列文件

- 证据：E-03。
- 位置：hw RVA `0x77f7e0`，格式串使用点 `0x77fa91`。
- 静态结论：模型加载路径检查 `.mdl` 后缀，在相应分支裁去扩展名，再用 `%s%d.seq` 构造从 1 开始的序列资源名；后续存在拼接和序列偏移重整路径。
- 限制：这是条件分支，不代表每个模型都依赖 `.seq`。须对用户模型核对依赖、序列数、事件和偏移，不能只看主 MDL 能否显示。

### F-03：CSO Studio 接口不能按普通 SDK 直接解释

- 证据：E-04。
- 位置：hw RVA `0x731110`；client 导出 `HUD_GetStudioModelInterface` RVA `0x114ab90`。
- 静态结论：引擎传入接口表；客户端先调用一项前置回调，再验证接口版本 1，并以 `rep movsd` 复制 55 个 DWORD，即 220 字节。其输出接口包含模型与玩家绘制回调。
- 限制：接口版本同为 1 不表示布局相同。候选表已记录实际 55 项，未逐项验证的槽位保持未命名。前置回调可能改变参数，后续还需核对这一层。不可照抄普通 `engine_studio_api_t` 的槽位和 ABI。

## 已确认的材质规则

### F-04：`@` 名称关联独立 shader 描述文件

- 证据：E-05、E-06。
- 位置：hw RVA `0x7e57ef`、`0x9ccc60`。
- 静态结论：已分析的绘制分支检查贴图名第 0 或第 3 字节是否为 `@`，从该位置取得四字节键，并打开 `shader/四字符.txt`。描述加载器将 `@00a` 归一为 `@000`；首行按 `%s %f` 读取，后续按 `%d %s %f` 读取，保存类型与按索引排列的浮点参数。另一模型渲染函数也调用相同描述加载器。
- 限制：参数索引的实际含义、时间基准及最终 shader 运算尚未全部恢复。不能将这些参数预先命名为滚动速度、亮度或动画帧率。

### F-05：`$` 名称影响绘制状态

- 证据：E-07。
- 位置：hw RVA `0x7b0d00`、`0x7e584a`、`0x7e5908`。
- 静态结论：加载器除材质 flags 的 `0x30` 位外，还检查以 `$` 开头且第 2 字节为 `a/b` 的材质。绘制路径读取名称第 1/2 字节，后续分支涉及剔除、深度写入及 `0/a/b` 混合选择；`b` 分支继续检查第 3 字节。对应 GL 常量和调用位置已留证，所有动态函数指针的初始化映射仍需逐个核实。
- 限制：不能丢弃材质原名或将所有特殊材质统一设为 Additive。上述分支本身也不证明存在贴图逐帧动画。

### F-06：还需配套 shader、材质和武器状态证据

- 证据：E-01、E-08。
- `hw.dll` 存在 `shader/mdlrender.fs/.vs`、`shader/deferredshading/mdlrender.frag/.vert` 和模型后处理路径，还存在 Base/Normal/Specular/Emissive 等材质资源键。字符串与代码引用只确认这些入口存在，尚不确认当前运行选择了哪条渲染后端。
- `mp.dll` 的导出表有 747 项名称包含 `weapon_`，可用于用户模型对应武器的服务端状态调查；这不是 747 个独立武器已完成分析的结论。客户端表现、服务器状态触发和引擎材质绘制必须分别核对。
- 资源目录存在 NAR 归档，本轮只读检查了三个归档的 64 字节文件头，均以 `NAR` 开头。未复制整个资源库，未提取 shader 或模型。`FileSystem_Nar.dll` 已准备为资源定位参考。

## 与现有 MetaHook 路径的关系

本轮实读当前工作区 `external/MetaHookSv-20261007/Plugins/Renderer`：

- `src/gl_studio.cpp` 的 `R_StudioLoadExternalFile_TextureLoad` 保存替换贴图的帧数与帧率。
- `assets/svencoop/renderer/shader/studio_shader.frag.glsl` 的 `SampleDiffuseTexture` 使用时间、帧率和帧数选择 `sampler2DArray` 层。

这是现有实现的源码证据（E-09），可供后续适配使用；它不证明 CSO 的命名规则、参数、变色、UV 运动或后处理已兼容。本轮未改 Renderer，也未构建或部署。

## 复活枪和角色手部：样本专项结论

以下 F-07 至 F-10 的 severity 均为 `n/a_re`，status 为 `validated-static`；它表示已核实所述静态证据，不表示完成游戏视觉验收。

### F-07：这支枪已有男女两套手网格，skin 表切的是灯光

- Evidence：E-10；confidence：high；location：`v_revivegun.mdl` 的 texture、bodypart、skin 表。
- 模型 SHA-256：`66ce4a245f2887e304f6ca96a99a9115357912485d480464009960181106f73e`，1,213,824 字节，IDST version 10，80 骨骼、13 序列、8 attachments、8 材质、5 bodyparts。
- `hands` bodypart 含 `CSO_Hand_Male_L_2009` 和 `CSO_Hand_Female_L_2009` 两个 submodel，分别使用 `#512512CSO_HighQ_Hand__Long_512.BMP` 和 `#512512CSO_Girl_Hand_long.bmp`。
- 两个手材质在文件中宽高都是 **4×1**，不能把这些像素当成真实手部贴图。加载器 hw RVA `0x7b0540` 起识别 `#` 后六位数字，重设纹理宽高；这里对应 512×512，并进入外部资源路径。
- 两个 skin family 为 `[0,1,2,3,4,5,6]` 和 `[0,1,7,3,4,5,6]`，只把 `$0b_light_blue.bmp` 换为 `$0b_light_yellow.bmp`。男女手的选择属于 bodygroup，与这两套灯光 skin 不同。
- hw RVA `0x7ebac0` 的子模型选择仍使用 `body / base % nummodels`。本轮未完整追踪角色状态写入 viewmodel.body 的所有调用者。

### F-08：中间炫彩主体来自客户端加载的外部动画 SPR

- Evidence：E-11、E-12；confidence：high；location：client RVA `0x8f98d0`、`0x8f9af0`。
- 解码后的原始 SPR 帧直接包含彩色能量丝线与旋转光环，MDL 内的两张 `revivegun_ef` 贴图则是蓝绿色圆弧，不能单独提供前者。
- 实际资源：`ef_revivegun_idle1.spr` 为 45 帧，`idle2` 为 47 帧，`draw1` 和 `draw2` 各 31 帧，均 256×256。四文件都是 **IDSP version 3**，40 字节 SPR 头后连续保存 DDS 图像；本样本各帧使用 DXT5，每帧 DDS 头及数据共 65,664 字节。逐帧解析恰好到文件末尾。
- client 的待机入口按状态选择 idle1/idle2，按左右手设置选择普通或 `_left` 文件；切出入口同样选择 draw1/draw2 及左右手版本。资源加载后传入同一个扩展接口虚函数槽 `+0x348`。
- 因而，普通 HLMV 只打开 MDL 时，既没有 CSO 的武器状态逻辑来创建这些特效，也不自动加载这些外部 SPR。仅修改 MDL 贴图的 Additive 标志不能补出缺失的 45/47 帧动画。
- 尚未恢复该扩展接口的完整参数 ABI、锚点/朝向、混合状态、实际时间基准和状态切换收尾。调用中出现的常量不能未经核实命名为 attachment、尺寸或帧率。解码 GIF 使用固定 50 ms/帧，**仅供内容识别，不代表 CSO 播放速度**。

### F-09：角色手部使用材质类别识别、角色外观编号及外部纹理替换

- Evidence：E-10、E-13、E-14；confidence：high（替换机制及角色表）、medium（尚未解码的配置映射）；location：hw RVA `0x9712a0`、`0x9719f0`、`0x971ab0`、`0x8f5d10`。
- hw RVA `0x121f7b8` 起的五项表明确关联标准材质名与文件后缀：

| 原模型材质名（不含 `#宽高` 前缀） | 外部手型后缀 |
|---|---|
| `cso_highq_hand__long_512.bmp` | `MaleLong.tga` |
| `cso_highq_hand_512.bmp` | `MaleShort.tga` |
| `cso_girl_hand_long.bmp` | `FemaleLong.tga` |
| `cso_girl_hand.bmp` | `FemaleShort.tga` |
| `hand.bmp` | `MaleOrg.tga` |

- `0x9712a0` 从当前模型纹理表取名；若首字节是 `#`，比较时跳过七个字符，与上述五种名称匹配。随后查询当前玩家相关数据和角色表，取得外观编号及是否继续使用玩家选择的标志。
- 角色查询通过已验证 vtable `+0x54` 落到 `0x8f5d10`，在 168 项角色表中按角色编号查找，返回记录 `+24` 的手部外观编号与 `+28` 的标志。例如 `buffclasschoijiyoon` 的角色编号 88 对应有符号外观编号 -12、标志 0；`buffclass26s1tr` 的角色编号 164 对应 -71、标志 0。完整表仅保存在本地证据，不公开复制。
- 外观编号进入配置/缓存查询；路径构造代码使用外观名称、下划线和 `FemaleLong.tga` 等后缀，再从 `gfx/tattoo/` 读取。该目录实际包含角色专用手套和袖子贴图，名字叫 tattoo 并不意味着只能显示纹身。
- 已复制并检查三张 512×512 原始 TGA：默认 `FeMaleLong.tga`、`buffclasschoijiyoon_FemaleLong.tga`、`buffclass26s1tr_femalelong.tga`；它们在对应 UV 布局上具有不同手套、袖口和服饰图案。
- 还有可选的基础图与替代图组合分支、缓存和其他手型回退；最终返回纹理句柄，在 texture setup 中优先使用。并非把角色贴图永久写回每个武器 MDL。
- 限制：已定位并复制 `gfx/tattoo/tattoo.cso`，其内容不是明文，本轮未还原其解码及全部编号到名称映射。手部替换机制、角色表和实际文件已闭合到同一类资源，尚未动态验证所有角色/武器/观战组合。

### F-10：枪身环境材质与中央动画是两条不同的证据链

- Evidence：E-05/06、E-10/15；confidence：high（文件内容与命名）、medium（具体运行路径）；location：hw RVA `0x9ccc60` 及配套 shader。
- 两张枪身材质名是 `$20@004_revivegun01_v.bmp`、`$20@004_revivegun02_v.bmp`，会命中第 3 字节 `@` 的描述符入口。
- 提取树中的 `mdlrender.vs/.fs` 确实包含 cubemap 环境映射：CSO_ENV 分支用视线与法线的反射方向采样；CSO_GLOW 分支另行叠加环境颜色。它们可以解释枪体的额外材质效果，但不能代替 F-08 的独立动画资源。
- 当前提供的提取树只找到 `@008.txt`，没有 `@004.txt`；不能用 008 的环境编号和亮度替代 004，也不能据此宣布枪身参数完全恢复。

普通 CS 1.6 的差别：GoldSrc 已支持 skin family 和 bodygroup，但原版 CS 没有这套通用的“角色编号→手部资源→逐网格纹理替换”规则。服务端可通过换预制 viewmodel 等方式实现部分变化；要共享同一武器模型并实现 CSO 式角色手部，须补客户端材质/角色选择逻辑、资源加载和生命周期管理。MetaHook 可作为实现入口，当前仍是研究结果。

## Evidence 索引

本地证据根目录：`analysis/csol-model-20261017/`。`checkpoint.json` 记录最终复核结果，`evidence-hashes.json` 固定本轮证据文件哈希。反编译存在栈分析警告及若干失败，关键结论以保存的指令证据为准；自动生成的函数原型不能当成已验证签名。

| ID | 本地原始证据 | 已验证层级 |
|---|---|---|
| E-01 | `samples.json`、四份 `*.dll.pe.json`、`inspect_pe.py` | 源文件/副本 SHA-256、PE32、导入/导出与候选字符串 |
| E-02 | `csol_load_version_asm.json`、`csol_confirm_asm_0.json` | 版本比较、模式 0/1 的实际指令 |
| E-03 | `csol_confirm_asm_1.json`、`csol_seq_load_asm.json` | `.mdl` 检查、`%s%d.seq` 构造与读取调用 |
| E-04 | `csol_api_registration_asm.json`、`csol_client_api_copy_asm.json`、`engine-studio-api-candidates.json` | 注册表地址、前置回调和 55 DWORD 复制 |
| E-05 | `csol_legacy_draw_asm_0.json`、`csol_legacy_draw_asm_1.json` | 从贴图名取得四字节键并调用加载器 |
| E-06 | `csol_shader_descriptor_asm.json` | 配置路径、归一规则、按行解析；另有模型渲染调用证据 |
| E-07 | `csol_texture_classify_asm.json`、`csol_texture_asm_0.json` | `$` 分类和绘制状态条件 |
| E-08 | `hw.dll.pe.json`、`mp.dll.pe.json`、`archive-headers.json` | shader/材质入口候选、武器导出和归档头；非完整功能验证 |
| E-09 | `renderer-source-snapshot.json` | 已读取的 MetaHook 源文件哈希和对应符号 |
| E-10 | `revivegun/manifest.json`、`revivegun/model.json`、`inspect_revivegun.py`、`external-header.json`、`external-texture-loader-asm.json`、`hand-body-selection.json` | 模型源/副本 hash、表解析、`#` 宽高解析及 bodygroup 选择 |
| E-11 | `revivegun/sprite-manifest.json`、`revivegun/sprites.json`、`preview_revivegun.py` | 四个 SPR 的源/副本 hash、IDSP v3/DDS 帧边界及原帧解码 |
| E-12 | `revive-idle.json`、`revive-draw.json` | 资源名、状态/左右手分支及扩展接口调用的完整指令页 |
| E-13 | `csol_hand_manager_asm.json`、`hand-tail.json`、`hand-cache.json`、`hand-path-asm.json`、`hand-class-map-asm.json`、`revivegun/hand-map.json`、`revivegun/character-table.json` | 五类手材质、角色表查询、外观缓存与 TGA 路径构造 |
| E-14 | `revivegun/hand-manifest.json`、`collect_hand_evidence.py`、`hand-config.json` | 三张角色手部 TGA 及 tattoo.cso 固定；配置函数仅前半段可信，后段 IDA 解码/函数边界异常，未据其声明映射完成 |
| E-15 | `revivegun/manifest.json`、`revivegun/shader/`、`revivegun/resources/common/shader/env.txt` | 原 shader 文本和参数入口；缺 004 描述符，不代表运行已选用此后端 |

可复现入口（从项目根目录执行，仅针对本地副本）：

```powershell
python analysis/csol-model-20261017/inspect_pe.py
python analysis/csol-model-20261017/find_refs.py hw.dll 0x37accc60
python analysis/csol-model-20261017/inspect_revivegun.py
# 以下两项需要 Pillow；脚本内的原始样本根路径须匹配本机授权资源位置。
python analysis/csol-model-20261017/preview_revivegun.py
python analysis/csol-model-20261017/collect_hand_evidence.py
```

原始输入与输出的 content_hash 见 `revivegun/{manifest,sprite-manifest,hand-manifest}.json` 及 `revivegun/checkpoint.json` 对应的证据清单；这些路径与工具脚本均为本地复现资料，不随公开报告发布。离线复现需要合法持有相同输入文件。

IDA 使用独立会话 `csol-hw-20261017` 与 `csol-client-20261017`。数据库保存在样本副本旁。报告未附带商业模块、资源或反编译实现；其他机器复现需要自行持有相同哈希样本。

## Path：下一步沿实际样本闭合证据

| 步骤 | 关联证据/结论 | 需要完成的工作 |
|---|---|---|
| 确认主模型及依赖 | E-01/02/03 → F-01/02 | 固定用户模型哈希，核对版本、纹理、骨骼、网格、skin/bodygroup、外部序列和事件 |
| 匹配材质资源 | E-05/06/07/08 → F-04/05/06 | 保留原名，取得实际被引用的 shader 描述、shader 与纹理；恢复参数含义与时间函数 |
| 匹配武器状态 | E-01/08 → F-06 | 从对应 `mp.dll` 导出和 client 表现入口核对待机、射击、换弹、变形、技能、状态重置 |
| 验证接口适配 | E-04/09 → F-03 | 按真实结构与调用约定接入 MetaHook；先确认加载和状态语义，再实现绘制 |
| 对照画面与生命周期 | 全部 | 同模型/动作/时间/相机/渲染设置对照，覆盖透明遮挡、深度、阴影、第一/第三人称、掉落、换图与缓存释放；测实际帧时间 |

## 验收与交接

本轮完成了 DLL/模型/配套最小资源固定、MDL 表解析、四组 SPR 原始帧解码、武器状态调用及角色手部资源替换链的静态核对。未启动 CSO 或沙盒游戏，未运行模型加载/GL/实机测试，未修改生产源码。动态效果的完整行为以及“与 CSO 完全一致”均未验收。

P-01（callflow）：复活枪状态 → idle/draw 和左右手资源选择（E-12/F-08）→ IDSP v3/DDS 帧内容（E-11）→ 扩展绘制接口；下一步核实完整 ABI、挂点、时间、混合和清理语义。

P-02（callflow）：MDL 手网格/材质类别（E-10/F-07）→ 角色表外观编号（E-13/F-09）→ 配置名称/手型后缀 → 外部 TGA/缓存 → 当前绘制纹理；下一步恢复完整配置映射、角色 body 写入与观战/换人/换图行为。

Timeline：先固定四 DLL 并定位通用模型/材质入口；收到样本后确认 MDL v10、男女 bodygroup 及灯光 skin；继而解码外部彩色 SPR 并追到 client 状态入口；最后核实手部类别表、角色表和 TGA 路径。图像是资源内容预览，不是游戏复现截图。

涉及共享 Renderer 构建、部署和 Git 发布时由主协调统一安排。首轮共享文档请求见 `HANDOFF.md`，本次取代过时“尚未收到模型”状态的精确增补见 `HANDOFF-revivegun-hands.md`；应用确认见 `handoff-status.json`。

## HLAM 中文实现与验证增补（2026-10-17）

以上“未运行模型加载/GL测试”描述的是先前静态研究阶段。随后已在上游 `4df74a58a50438b8f4b974e04ab9f24fdfcbb811` 的隔离副本实现并构建中文 CSOL 预览版；没有修改或启动 CSO 游戏客户端。

实现内容：材质名 `$` 的状态/混合规则、`#` 的 UV 尺寸、外部角色 TGA、type 0 cubemap、SPR v3 内嵌 DXT1/3/5 动画，以及中文资源/手部/附着/状态面板和独立 JSON 配置。手部子模型选择只覆盖绘制，不改普通预览的 bodygroup。图像解码、资源搜索和 GPU 上传不放在每帧绘制循环中。中文菜单、提示、对话框和模型控制说明由模型直接编写，未调用翻译服务；资源标识、QC 指令和版权声明保留。

| Evidence | 实际验证与 Finding | Path / 边界 |
|---|---|---|
| `hlam-build-attempt04/06/07.log` | Clang 22.1.7 x86、C++20、O3/ThinLTO/AVX2/precise FP 完整构建成功；修正 Qt5/STL、fmt、OpenAL 及 libnyquist CRT 兼容 | 现有机器构建通过；独立构建脚本只有语法检查，尚无干净机器复现 |
| `ctest-final.xml`、`hlam-decode.json` | 606 项材料/格式/截断/非法范围检查通过 | 支持范围为已验证的 v10 与对应资源格式 |
| `hlam-decode-comparison.json` | 全部 154 帧 SPR 与 3 张手部 TGA 的 RGBA SHA-256 和 Pillow 一致；修正 RGB565 位扩展差异 | 证明解码数据一致，不证明游戏混合/时序一致 |
| `gl-smoke03/result.json`、同目录 PNG | RTX4090 上真实 GL 绘制：帧变化、手部、三次释放重建、禁用恢复、MDL 序列化及 undo 不变；GL 错误 0 | `@008` 是明确标记的合成材料测试，效果摆放也是测试参数，不替代原模型缺失的 `@004` |
| `gl-smoke03/ui-*.png` 与结果 JSON | 真实 Qt 控件执行配置往返、角色切换、帧定位、多模型隔离和重载资源；中文布局无截断 | 截图来自 Qt 自身。桌面 Computer Use 捕获超时，未取得人工鼠标操作证据；Qt 主窗截图不包含原生 GL 子窗口内容 |
| `hlam-source-replay.json`、`hlam-source-scan.json` | 108 文件源码补丁在固定上游回放后 LF 规范化内容一致；仅文本补丁，敏感模式扫描无命中 | 9 个新增文件受 Git autocrlf 影响，未把换行差异说成逐字节一致 |
| `hlam-delivery.json`、`package-smoke01/result.json` | 39 文件便携 ZIP、CRC及逐文件 hash 核验；清空开发依赖 PATH 后从 ZIP 解压副本运行全部 GL/Qt 检查成功 | 包含程序、动态依赖、许可、中文说明和源码补丁；没有游戏 DLL、MDL/TGA/SPR、IDA 数据库或凭据 |

原始样本 SHA-256 保持 `66ce4a245f2887e304f6ca96a99a9115357912485d480464009960181106f73e`。源码改动以 `patches/halflife-asset-manager-csol-zh.patch` 保存，上游副本和商业资源均不纳入根仓库发布。共享连续性文档及源码提交仍由主协调应用精确交接。

本节为首个实现阶段的历史记录；当时的 `@004` 缺失、左手与动作待办已由本文顶部的自动加载检查点更新。加密角色表编号映射、v20/v21、完整事件及 CSO 游戏同视角对照仍未完成，不能宣称已满足“与 CSO 完全一致”。
