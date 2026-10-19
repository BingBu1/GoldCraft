# NightFall HSV 后处理通道

2026-10-19：补齐 `efn:hsv_filter` 的采集、协商、传输和原生回放源码。当前仅完成静态检查，没有运行验证用例、链接生产 DLL、制作 JAR/更新包或部署。真实技能画面和性能仍待累计批次统一验收。

## 故障证据与结论

| 证据 | 观察 | 结论与实现路径 |
|---|---|---|
| E1，既有游玩状态文件，SHA-256 `8421f36cff3abd1f3b1976c5b5f07bb63dd8827b67306df0fefe606a9eff55fd` | VIX 共176013帧、累计失败1040次；最后一次为 `Unsupported VIX pipeline efn:hsv_filter` | F1：原 `spec` 不支持此管线，抛出后返回整个空图；新增独立 kind8。1040是全部失败数，不能全部归因 HSV，也不是本轮实时采样 |
| E2，锁定 NightFall 管线和 shaderpass 的静态字节码 | priority101；先 blit 到临时目标，再以 Hue_W=(1,.97)、实际 `getAlpha()` 做 HSV | F2：必须保留两次绘制、实际 alpha 和 blit 的 ColorModulate；实现从原方法采集，不重写动画公式 |
| E3，VIX TargetManager、PostParticleRenderType、PostPassBase 与 Minecraft Framebuffer 字节码；原两份 shader JSON | 临时目标使用 TextureTarget；颜色格式32856/RGBA8；两次绘制均 ONE/ZERO 替换 | F3：HDR宿主也要保留原两次RGBA8取整；共用现有mask/ping目标，无额外全屏纹理。清色对全屏替换结果无贡献，HSV不使用黑白冲击的alpha混合 |
| E4，协议30握手源码及角色检查 | 客户端旧允许集合不含位3；未知位导致关闭握手；全部32位已分配 | F4：位3按已认证角色区分客户端HSV与服务端status，扩展既有客户端逐级降级；只有双方确认后发送/接收kind8，不改协议头或服务端含义 |

调用路径：原 `ScreenEffectEngine.OnRender` 决定 `shouldPost` 和入队 → 原优先队列出队 → `getAlpha` 和CPU uniform采集 → 原子 `PostEffectPlan` → 同一连接代际发送 → 原生协商检查与解码 → 固定哈希shader回放 → 整图成功才提交宿主颜色。

旧 `Post-effect stage bounds` 的屏幕扭曲到期保护已经存在。E1中到期跳过计数为3，但旧日志没有全部失败参数，仍不能唯一解释历史拒绝。

## 实现约定

- kind8为无mask阶段，8个float依次是 Hue_W的两个分量、Alpha、预复制ColorModulate四分量、保留零；沿用有限值/绝对值不超过一百万的边界。原kind1–7布局不变。
- 继续使用真实 `shouldPost`、原队列顺序和 `getAlpha()`。不重复调用距离判定、不重置其保留alpha、不把未参与原handler的type枚举用于新分支。
- `HSV_POST_CAP=8`仅表示client→host_client的能力；server→host_server上的同一位仍表示status。两端查询、发送和消费保留角色与代际检查。旧Java不请求此能力，新host不主动回送。
- 新Java遇到旧host拒绝时先撤回HSV，再按原顺序撤回outline/view。凭据、坐标约定和其他能力检查保持。旧host连接成功后只省略HSV，累加 `vixPostHsvUnsupportedStages`；其他阶段/私有mask继续交付。未知的其他效果仍拒绝整图。
- LDR两次绘制；HDR两次绘制加两次复制，保持临时与最终RGBA8精度。原生预热绘制路径没有新增CPU容器、纹理存储或uniform位置查询；此为源码结构结论，尚无本轮运行分配/帧时间证据。
- 本机提取清单增加 `hsv_filter.fsh`，共11个fragment和1个reference vertex。新DLL必须配套资源，否则现有严格初始化会拒绝后处理；下一累计包须包含此文件。原版权shader/JAR/字节码不发布。

## 固定输入与复核方法

| 输入 | SHA-256 |
|---|---|
| NightFall-1.21.1-Neoforge-3.4.0.jar | `ec66e0b42a0f8561ebbf57af356dfd237be170297cc5e5e712de0bec34a7fa4c` |
| 内嵌VIX1.0.9 | `0d6d3aee5030abcae0247ef2d7ccc8267a2a56513d0009941a107ed36f4f6aca` |
| HsvFilterEffect.class | `6b990ff25163e948fe0eb9d17cbb819e493d0d71072943bd40ad7004b8e8d2a8` |
| HsvFilterEffect$HsvFilter_Pipeline.class | `94541709417ea3456b33a41f62cb7fc7fd46b79fa9356eab9cf65028b1c623c4` |
| HsvFilter shaderpass class | `1556774011a14235421a204b65bcf02fdc4b58a792fd85f584c9eaaf31cf2c91` |
| hsv_filter.fsh | `9b9b3a4393980a6ab50da58235c1d16de1a4921cbcfa917459ab0334bf6518a2` |
| hsv_filter.json | `733232038f66266adbf8d663ffe49003ecac4c045e96dd5e262228bac005a967` |
| 当前映射Framebuffer.class | `5c41695b33c4dceb4ff928f2e59c37ed5deea2d63b6ac96496d91277ac535cec` |

从仓库根目录、已安装匹配Mod且有JDK21的环境中，可仅反汇编检查（不执行类）：

```powershell
$taskJar = 'sandbox/neoforge-cs-client-b/mods/NightFall-1.21.1-Neoforge-3.4.0.jar'
javap -p -c -classpath $taskJar 'com.hm.efn.client.screeneffect.HsvFilterEffect$HsvFilter_Pipeline'
javap -p -c -classpath $taskJar 'com.hm.efn.client.screeneffect.HsvFilterEffect'
javap -p -c -classpath $taskJar 'com.hm.efn.client.shaderpasses.HsvFilter'
```

VIX检查使用该JAR内 `META-INF/jarjar/vix-1.0.9.jar`；反汇编类为 `com.guhao.vix.client.targets.TargetManager`、`com.guhao.vix.client.pipeline.PostParticleRenderType` 和 `com.guhao.vix.client.shaderpasses.PostPassBase`。原始输出和选取的历史状态保存在本机 `build/post-hsv-20261019`，不进入公开仓库。

## 验证状态与下一步

冻结发布基线加24个明确覆盖文件，共758份输入。Clang x86 C++20 `/Zs`检查4个生产单元及3份原生验证源码通过；Java仅执行 `compileJava compileTestJava`，通过。两条上游C++警告和三条既有Java弃用警告保留。12项资源SHA一致，提取工具仅做Python AST检查和输入核对，没有运行提取/部署。

已写未执行的验证源码涵盖：kind8布局与保留位、mask拒绝、未协商拒绝、角色隔离、旧端能力逐级降级、旧代际拒绝、断线恢复、原shader独立两目标像素对照、LDR/HDR、透明度/颜色乘数、连续滤色、窗口尺寸和局部viewport，以及八阶段预热检查。

后续集中执行原producer/FML与新旧端互通、真实GL像素/状态恢复/失败原子性、资源缺失与重载、混合技能、实际CS画面和固定视角帧时间。源代码/typecheck不替代这些验收；完整GoldCraft目标保持未完成。用户旧更新包未安装，下一CS客户端/服务端必须累计覆盖既有修复，MC整合包由用户管理。
