# VGUI 字体与纯字体方案定点更新

2026-11-05。源码、完整 DLL、真实 GDI/SDK 组件和 DLL 工厂 ABI 已通过。**尚未部署或进行游戏画面验收。** 本项是服务器资源 SHA 更新的宿主缓存适配，不修改 CSOL 原字号、颜色、布局或游戏逻辑。

## 证据与实现

匹配来源为 `sources.lock.json` 指向的 `VGUI2Extension`，提交 `da438b6eaa1ba0d616cc8ac65a31420aa5bb11bd`。开始时磁盘部署、dist 和既有构建 DLL 同 SHA `aa4dfa44cdbb7f29756fa15169d81065ff30fe9edb39c2c9c9e255fb3afd0efe`。本轮只读进程检查返回 WinError31，因此这里仅确认磁盘身份，不声明新的内存模块核对。

修改前设计与源身份：`build/vgui-resource-refresh-20261105/DESIGN.md`、`before-identity.json`；初始消费者审查：`build/skull3-audit-20261105/resource-consumer-review/REPORT.md`。不依赖未经核实的 CSOL 默认值，也不以本项宿主适配冒充原版客户端实现。

| 证据 | 结论 | 实现路径（VGUI2Extension 根目录下） |
| --- | --- | --- |
| 原 `LoadSchemeFromFileEx` 文件名缓存、`ReloadFontGlyphs` 不重读 KV；旧对象反例两项失败 | 同名新版 scheme 仍读取旧配方；不能用全局 `ReloadSchemes` 替代资源重读 | `src/Scheme2.cpp`、`src/ResourceRefresh.cpp` |
| 原 `AddGlyphSetToFont` 拒绝非空 HFont；原生字体按 family/指标复用 | 更新须先退休旧范围/原生对象，再重建同一 HFont；菜单配方变化须显式 Stage | `src/FontManager.cpp`、`include/Interface/VGUI/IResourceRefresh.h` |
| 字形缓存以 HFont/字符为键，多个字体共享页 | 只失效目标字形，复用空矩形，保留其他字体像素与纹理 ID | `src/FontTextureCache.cpp` |
| 原析构先删除仍选入 DC 的字体/位图 | 先释放 DC，再释放 GDI 对象；真实旧句柄失效检查已通过 | `src/Win32Font.cpp` |
| Surface 的字体文件注册原本不记录次数，退出按重试循环删除 | 精确记录并释放本模块次数；更新后路径改变也能清理，不移除别人的注册 | `src/FontManager.cpp`、`src/SurfaceHook.cpp` |
| `GetBorder` 向面板返回原始指针 | 不支持有 Border 的 scheme；不替换未知面板对象或全局比例基准 | `src/ResourceRefresh.cpp` |
| 真正 `AddFontMemResourceEx` 成功，但原枚举门导致两个实际字形检查失败 | 内存字体不枚举；未枚举时以 CreateFont/GetTextFace 核对实际 family，仍拒绝静默系统替代 | `src/Win32Font.cpp`；`memory-old01/test.log` |

## 接口与生命周期

新增 `VGUI_ResourceRefresh_001`，继承匹配 SDK 的 `IBaseInterface`。原 Scheme2、Surface2 等接口 vtable 不变。调用只在同一个 UI 线程的非绘制资源边界进行。

1. `BeginUpdate` 复制目标 family、相对字体路径和可选 scheme；收集共享 family 的句柄，刷新前提交已排队文本，清目标字形/原生对象，卸载本模块持有的文件字体注册。空请求适用于首次入服尚无 owner 资源。
2. 调用方移除自己持有的内存字体、切换经过验证的 VFS，再安装新内存字体。菜单配置改变时，沿各 owner 已有公式调用 `StageFont` 更新被选中的非 scheme HFont 配方。
3. `CommitUpdate` 重读纯字体 scheme，并构造全部候选字体，全部成功后才发布。保持 HFont、HScheme 和 alias 数值；scheme 自己的 alias 配方以文件为准。
4. 失败保持暂停。调用方先恢复旧 VFS 和私有字体注册，再 `AbortUpdate`，使用原配方恢复。Begin 若已经开始释放后失败，会留下非零 token，同样必须 Abort；不能把失败当成功解锁。
5. 断线撤 overlay 后使用 `CommitRemoval`：不要求 CSOL 文件存在；恢复 base 中可用的普通共享字体，把 CSOL scheme/缺字库句柄保留为休眠状态。下次 Begin 必须携带相同 scheme/family，Commit 复活原句柄。取消一次重入仍保持原来的休眠状态。

只支持当前 manifest 允许的纯字体 CSOLHudScheme：不能增加/删除 alias，不能改变 CustomFontFiles 的键/路径集合、比例基准或引入 Border。owner 普通菜单 JSON 的 font family、字号、字重由 Stage 接收，不在 VGUI 侧猜值。更新中无关文件字体仍可注册；已选择的路径/句柄受到隔离。

## 验证

最终生产候选：`build/vgui-resource-refresh-20261105/full05/build/VGUI2Extension.dll`。

| 层级 | 结果 |
| --- | --- |
| 完整编译与链接 | full02 全部 130 个对象构建成功；最终 full05 编译 8 个相关 TU，复用 122 个已核对象并完整链接。537 个源码输入、冻结副本、复用对象均未变化 |
| 原回归 | 3 项 CTest 通过：语言、IME、菜单 ABI |
| 实际对象与 GDI | `tests05`：324 检查、0 失败。含新旧 TTF 同 family/同配方不同字形、scheme 字号重读、40 轮不增句柄/字形页/原生对象、无关字体/共享页保持、私有内存字体、外部注册次数、失败/Abort、撤 overlay 与重入 |
| 最终 DLL ABI | `abi01`：真正 LoadLibrary 工厂接口 10 检查、0 失败；包含新接口全部调用槽及原 Scheme2/Surface2 工厂 |
| 原源码反例 | `old01` 链接保留原构建对象；3 检查中 2 个预期失败，确认旧缓存不重读 scheme/原生字体 |
| 私有内存字体反例 | `memory-old01`：321 检查中 2 个失败，仅对应原枚举门造成的无字形与无法更新；修复后这两项均通过 |
| 发布补丁 | 完整 VGUI patch 包含 19 路径，其中 4 个新增文件；保留已有菜单改动。针对固定 upstream apply/check、逐文件内容比较通过 |
| 部署与实机 | 未部署，未操作单 B/专服。真实 HTTP 门禁、OpenGL 字形上传、菜单组合及连接/断线画面仍由父任务合批验证 |

最终 DLL SHA-256：`26c00c4d5bade2db0bfcbc9141dd1ec74bc9499920cd12a3f64524f40d112265`。PDB SHA-256：`a768b29541ad0e8bb32defcb5f9e6dbea8e64554a65d1d1758824316434d5283`。x86 PE 与 PDB GUID 同为 `{C7C59D18-5F32-2981-4C4C-44205044422E}`、Age 1。

patch SHA-256：`68fe1cdd063ae3928fb884a5c6afce2fd90e32639cf84ce60863a9ea9f4b5115`。完整回执见 `final-inputs.json`、`patch-validation.json`、`tests05/result.json`、`abi01/run.log` 和 `handoff.json`。

保留的中间失败：首次 SDK 编译缺少 pinned CUtlVector 所需的 `swap` 声明；full01 的 CTest 程序路径未指定；tests02 的 3 个请求字号/实际 GDI 高度混淆和 3 个休眠文件注册缺口；full04 增量脚本以字符串后缀误选 EngineSurfaceHook，导致重复符号。已分别修复并保存原日志，没有把这些失败算作通过。

## 父任务后续

配合 CSOLSupport 的 Before/After/Cancel/ResetToBase、父 VFS 回滚和引擎/Renderer 退休接口合批验证。不要单独开启 v4 却缺任一资源 owner。发布只包含 patch、测试源码与本文；原模块、TTF、PDB/DLL、运行日志和本机编译环境均保留在 build。

建议 TODO 保留：完成入服 SHA 更新的全 owner 合批构建与单 B 实机，覆盖保留旧资源升级、失败取消、断线回普通服和再次入服。VGUI 本阶段组件通过不能关闭整体实机验收。
