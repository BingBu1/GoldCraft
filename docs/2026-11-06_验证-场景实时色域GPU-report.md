# 场景实时色域 GPU 对照

父代理修正后的不透明预绘制通过六组真实 GPU 回归，共 **121 项检查**；相同夹具对旧缓存实现六组均失败。两版均执行生产 `scene.cpp` shader 和绘制函数，RTX 4090 实际像素读回确认差异，GL 错误码为 0。本轮只修改测试与报告，没有修改生产源、部署、操作游戏进程或写 Git。

## 范围与来源

依据父代理修改前的 `build/monster-integration-20261106/opaque-gamma-plan.json`，本轮验证的是宿主 Renderer 接口适配，不增加 CSOL 行为。`IMetaRenderer::IsDrawGammaBlendEnabled() const` 返回当前 `r_draw_gammablend`；`R_PreRenderView` 根据当前目标设置它，post-scene flags 读取同一状态。

冻结源来自 `client-full-01/effective-source-manifest.json`，没有使用当前工作树中尚未合入的资源刷新。旧 scene SHA-256 为 `db62ee9cbeb3e8319a00b0ad7084fd6fb91942cb52814771443490320ee4b5c3`。新冻结源仅应用三个语义变化：去掉 `lastPostSceneGamma`；预绘制读取实时接口；删除 post-scene 的旧缓存赋值。新 scene SHA-256 为 `8205f996eec72e18e84520c431056ebab6bfd89d2a6449d8deb8dc276610bebf`。测试冻结保留了基线的旧注释，父生产源另外修正该注释，未改变被验证的语义。

## 夹具和结果

公开变更位于 `tests/csol-scene/scene.cpp` 和 `renderer_fixture.py`。新增 `--opaque-gamma 0..5` 窄模式，复用隐藏 OpenGL 3.3 core 上下文、生产网格上传及 shader。Renderer/引擎/资产是受控接口；语音、武器左右手和独立粒子系统保留为不在本回归范围内的边界。没有把它们的测试替身当作游戏行为验收。

材质使用全亮不透明中灰纹素 128，显示 gamma 和纹理 gamma 均为 2。线性目标预期 RGB 为 64，gamma 目标为 128。测试故意让保存 cvar 或历史 post-scene 状态与当前目标相反。每组还检查深度写入、复用捕获光照，以及后续 post-scene 没有重复绘制不透明网格。

| 场景 | 当前目标 / 保存值或历史 | 旧 RGB | 新 RGB / 期望 |
| --- | --- | --- | --- |
| 首帧线性 | 线性 / 保存值 1 | 128 | 64 / 64 |
| 首帧 gamma | gamma / 保存值 0 | 64 | 128 / 128 |
| 历史 gamma | 线性 / 历史 gamma | 128 | 64 / 64 |
| 历史线性 | gamma / 历史线性 | 64 | 128 / 128 |
| 即时切换 | 线性→gamma | 64→64 | 64→128 / 64→128 |
| 新会话 | 历史 gamma→ClearScene→线性 | 128 | 64 / 64 |

新会话组实际调用生产 `ClearScene`，重建 Program 并重接夹具 Renderer；它是同一个隐藏 GL 上下文中的生命周期回归。六组分别启动新进程，彼此不共享历史值。新版本五组各 18 检查，切换组 31，共 121；旧版本各在像素断言退出 1，切换组首帧正确、第二帧失败。

## Evidence → Finding → Path

- **E1**：父修改计划及 Renderer 接口/实现，身份记在 `opaque-gamma-plan.json`。**F1（源码已确认）**：上一帧全局缓存不能表达当前目标；实时接口与原 post-scene 标志同源。
- **E2**：`opaque-gamma-review-01/new02/result.json`，SHA-256 `168cdae8a5a423753d19151349a154e4de09102a04454ef61342c14510efb580`。**F2（GPU 已验证）**：新来源在六组状态中均产生正确像素和深度。
- **E3**：`opaque-gamma-review-01/old01/result.json`，SHA-256 `4425f8b91c391a91f54617ce7726fa929abd61e5e18c8bac009d212b079aad7d`。**F3（GPU 已验证）**：相同测试可以检出旧实现的六种错误，不只是当前源码自证。
- **P1（callflow）**：受控目标/历史→生产 `DrawModel` 队列→`DrawOwnedOpaque`→`Camera` 选择色域→生产 shader→GPU 像素/深度读回→`DrawOwnedWorld` 检查不重复绘制。E2/E3 覆盖该路径。

两版各记录 606 输入、555 实际包含依赖，结束时零漂移。首次 `new01` 六个 TU 编译成功，链接因旧测试缺少新增 `MeleeHandScope` 构造/析构及 `EmotionVoiceStudioEvent` 外部边界失败；失败原件保留。补测试边界后 `new02` 与 `old01` 各只重编主测试 TU，五个辅助对象复用前逐源/实际包含头核 SHA。编译和 lld 链接均限制单 job，保持 x86、C++20、O3/ThinLTO/AVX2、precise 浮点。

复现脚本为 `build/monster-integration-20261106/opaque-gamma-review-01/run.py`。在保留本地冻结证据和 SDK 的工作区，可用尚不存在的输出名执行，例如 `python build/monster-integration-20261106/opaque-gamma-review-01/run.py new03 new01`，旧对照将首参数换成 `old02`；后参数复用已核五对象。脚本、原件及构建产物不随公开源码发布。

本结果关闭的是上一帧色域缓存的组件缺口，替代前份客户端交接报告 F5 的“像素未验”状态。完整 DLL、真实游戏主视图/水面/门户/阴影及多人仍由父代理后续验证；这里没有运行完整旧场景/准星矩阵，也不重复计入其历史通过数。共享状态更新由父代理应用，领域交接已写入本报告及 `opaque-gamma-review-01/handoff.json`。

## 精确公开版本及合批

父代理将公开测试收窄为本次色域模式，排除工作树中独立的旧声音/地图特效测试。该精确版本在 `new-public03` 再次通过六组121项；`old-public02` 使用同一夹具仍六组失败。未把新旧两轮相同检查累加为覆盖数量。完整客户端最终 SHA-256 为 `2b4a647d3f287bf7eeefc0ea308c7402080820445c86124113d6afa5352f48a6`，完整链接、PDB匹配和实际输入来源已核，本地单B原子更新已完成。正常退出存盘及本阶段真实游戏画面仍须另验；服务端自爆验证见集成报告。
