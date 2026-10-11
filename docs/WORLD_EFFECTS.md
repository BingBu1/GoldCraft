# Minecraft 世界特效同步到 CS

GoldCraft 使用 Minecraft 的真实实体、粒子和材质数据，在 CS 场景中绘制女仆气泡、Epic Fight 网格与已核对的 VIX／NightFall 后处理。协议 26 已部署到本机主实例、A/B 与专服，仅启动 B，在 cs_assault、24 Bots 下完成实际配对及非零后处理。运行计数不等于画面或 FPS 验收。

## 支持范围

| 路径 | 实现 | 验证边界 |
|---|---|---|
| 女仆表情、图片、文字 | 捕获真实直接网格和文字缓冲 | 已核对 TLM 1.5.3 入口及 3186 顶点探针；实际气泡画面待验收 |
| Epic 刀光、命中、闪电、残影 | 实际几何、排序索引、混合、深度／颜色掩码 | 真实刀光 producer 和原版排序方法回归通过；游戏遮挡待验收 |
| NightFall 世界阶段 | 捕获作用域中调用核对过的 AFTER_ENTITIES 回调 | 生产字节码入口通过；技能画面待验收 |
| VIX／NightFall 后处理 | CS 场景颜色／深度、真实遮罩、priority 出队顺序和七类通道 | 七类均已配套部署；kind7黑白画面已由用户确认；原全屏色散生产参数 2420 组逐位比对通过；各技能画面待验收 |
| 附魔高光 | 原版 EQUAL、加法混合、颜色写入、动态 UV、GlintAlpha、repeat／linear | 透明孔洞、相机偏移、阴影不偏移的像素回归通过；原子帧已部署，实际附魔画面待验收 |
| 大型／动态纹理 | 4096 图集有界分块；动态上传／删除使缓存失效 | 实机已识别并完整上传 4096 粒子图集；实际 GIF 待验收 |

这些是通用绘制路径。只有已支持的 shader、布局、状态和事件阶段能够复用；任意自定义 GPU 程序不在保证范围内。未知通道拒绝整幅后处理计划并记录原因，不保留上一帧遮罩。

## 已定位的漏绘根因

- TLM 的 EntityGraphics 直接提交网格，绕过原实体顶点收集器。
- 真实 Epic 刀光可产生 U=125.00161；有限 UV 是插值量，不能按纹理索引限制到 ±16。NaN／无穷仍拒绝。
- Epic 的排序方法也处理 TRIANGLES，且分配多余索引空间；只读取实际 indexCount，保留排序及绕序。
- GoldSrc 首次 RenderView pass 为 1，SvEngine 为 0。仅规范化公开回调索引，保留宿主计数，后处理才真正执行。
- Core 模式的 polygon-mode 查询可能只写一个值；兼容模式的前后面恢复方式会在 Core 产生 GL1280。修复已通过两个真实驱动 profile。
- 原版附魔纹理是不透明紫色，依赖底图的深度和特殊混合。普通不透明贴图路径会产生紫矩形；底图／高光还必须共享相机偏移和帧身份。

## 协议和生命周期

普通效果保留原绘制顺序、深度测试／写入、混合因子、颜色掩码和透明度裁剪；不进入阴影或 GBuffer。实体底图仍参与原生光照和阴影。附魔的 view-offset 在相机空间执行，阴影几何不偏移。

大型纹理通过 type65 分块：每块最多 256 KiB，每呈现帧最多 1 MiB，完整后替换 GL 纹理。128 MiB 已发布纹理、64 MiB CPU 暂存是分别的上限，并非总峰值；原子 GPU 替换和 Java 读回另占内存。地图、代际、生命变化和断开会撤销过期暂存；普通同生命形态切换保留上传。

协议 26 的 type66 将实体底图、附魔／气泡与头像身份合成一次提交，避免独立粒子 pacing 导致底图和高光来自不同帧。纹理未就绪或入队失败不能发布半帧。底图与效果统一按 0.5 秒、双端生命代次和纹理代次判定有效；空帧、失效与重置共同清理。纹理像素仍使用现有共享缓存，不属于不可变的逐帧事务。

VIX 使用已安装 JAR 的 SHA-256 白名单资源，在 CS 真实场景快照上执行 bloom、space broken、chromatic、air disturbance、screen distortion、color dispersion、black/white impact。目标和 sampler 复用，不读回 CS 场景到 CPU。原版 bloom 过滤绑定仍有歧义，当前使用已确认的 NEAREST；雾一致性仍有限制。前六类保留 HDR 工作颜色，遮罩／金字塔使用 RGBA8。黑白冲击保留原版预处理和输出的 RGBA8 量化及清色 alpha 混合：LDR 两次绘制，HDR 两次绘制加两次复制，复用现有目标。

## 构建和同步

先完成 [基础构建](BUILD.md) 和 [女仆扩展构建](EPICFIGHT_MAID.md)，关闭相关沙盒进程后部署：

```powershell
.\tools\Build-Renderer.ps1
.\tools\Build-NeoForge.ps1 -Tasks build
.\tools\Build-Native.ps1 -Server
.\tools\Deploy-OptimizedNative.ps1 -BridgeOnly -WithPostEffects -Clients cs-client-a,cs-client-b
python tools/Modpack.py sync --refresh-core
python tools/Modpack.py plan --refresh-core
python tools/Test-WorldEffects.py
python tools/Test-TrailProducer.py
```

Java、CS DLL、服务端桥接、Renderer 和 shader 必须配套。后处理资源由 Extract-VixPostShaders 从用户已安装且核对过的 Mod 提取；不把第三方 JAR、shader、游戏资产或反编译源码发布到 GitHub。Mod 更新需要 JVM 重启。

## 验收边界

协议 26 完整 Java 140 项、native 40 项、生产映射入口 84 项通过；配套 Renderer 13 项、线程库 10 项通过。原生 40 项包含匹配引擎按键释放回归；Core 与 Compat 两个 profile 验证完整六类生产回放及返回后 GL0，另保留旧版 GL1280 失败证据。完整 ReGameDLL 构建、Clang 审计和公开源码补丁重放通过。

静态入口检查不执行 FML/Mixin；隐藏 GL 的真实驱动像素测试不等于实际游戏 FPS。新版实机已观察到非空 VIX 生产与 CS 非零回放，GL0、后处理失败 0、Java 特效错误为空。旧协议 25 的 GL1280 已在新版当前样本中消失；kind6 全屏色散尚需实际技能触发验收，不能用其计数为零推断成功。

继续在 cs_assault 使用单 B、32 槽 ReHLDS 和 24 SyPB Bots 验收气泡、六类通道、附魔、墙体遮挡、消失与换图清理。性能比较必须固定观察者位置／朝向。Computer Use 本轮恢复后连续捕获超时，无成功自动截图或输入；完整画面、YSM R 往返、多人及 FPS 均未宣称通过。

Java 状态包含 effectDrawSources、effectUnsupportedReasons、particleError、vixPostLastFailure 和 vixPostStageCounts；native 包含 postEffectCallbacks／NonemptyFrames／Draws／Failures，以及纹理暂存和完整上传计数。诊断计数用于定位，不能代替图像验收。

## 黑白冲击（协议29，已部署）

`efn:bw_contrast` 现在拥有独立 kind7，实际导出30项值。配置关闭、到期或 should_render=false 只跳过该效果；原始 priority 队列、fixed70投影与单调时钟保持。清色来自当前 Minecraft 主目标的CPU字段，blit乘数和retained uniform读取当前程序；访问器通过实际TinyRemapper和Sponge双命名验证。旧六类布局保持不变，协议版本保护端点匹配。

协议29整包38组／166项Java、44项native、ReGameDLL构建、166＋44项跨进程检查通过；Clang审计11组／1642编译单元／15个x86产物通过。真实producer492组／25558检查、568字节独立跨语言核对、Core/Compat336同驱动原shader参考均通过；最大分量差0.004408002，容差2.1/255，并非逐位一致。预热128七通道帧无新增分配、storage、uniform位置查询或缓冲上传。HDR额外两次复制的真实帧时间仍待测；原shader的反向smoothstep仍受驱动行为影响。

运行 `python tools/Test-BwContrast.py` 可从本机已安装的匹配Mod验证原producer和访问器。此测试不运行完整FML或游戏技能。协议29已同步A/B/server，仅启动B及两专服；5份核心JAR及165原生部署文件一致，实际FML应用清色访问器和两项NightFall修复。用户已确认“黑白冲击正常，未报错”；同期kind7累计9、native回放373/失败0/GL0。后续45秒44有效样本共享682171120字节、回放+96，出现正常死亡控制释放。Java另有1次旧八float通道参数校验失败，独立排查；这不属于黑白分支，也不能由画面确认推断所有通道无错误。资源热重载、长期稳定性和同视角FPS仍待验。

## 2026-10-11 — 协议30屏幕扭曲寿命边界

真实NightFall ScreenDistortionEffect及VIX生命周期在末tick产生NaN：age等于lifetime时仍保留，而partial使normalizedAge大于1，原强度公式对负数开平方。VixPostCapture只跳过kind5中age非负、lifetime为正且normalizedAge有限且大于1的过期阶段，保留正常时段公式、精确端点、同帧其他stage/mesh/depth reset；不放宽Stage参数校验，失败时补充实际参数。

`python tools/Test-ScreenDistortion.py`使用本机锁定4个原始class及精确生产guard/Stage，658cases/7706checks：628有限域逐bit相同、30尾tick NaN复现，非法状态不被吞，邻接stage与mesh逐byte保留。源码与范围伤害统一协议30构建，已同步master/A/B/server并重启单B与两专服；五份核心JAR及本轮11项原生部署文件哈希一致。新鲜空闲采样VIX失败0、过期计数0，尚未触发此寿命边界。此前旧日志缺具体拒绝值，不能唯一归因所有历史Stage失败；实机过期计数、画面连续性与热重载仍需验证。
