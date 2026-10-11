# Minecraft 世界特效同步到 CS

女仆表情气泡、文字和 Epic Fight 的独立网格特效现在通过 GoldCraft 协议 23 导出到 CS。新版已同步到 Mod 主实例、A/B 和服务端，尚未完成真实 B 画面验收。VIX 泛光、折射等屏幕后处理仍未实现。

## 支持范围

| 路径 | 实现 | 当前验证边界 |
|---|---|---|
| 女仆表情、图片与文字气泡 | 捕获实际实体绘制中的直接网格及文字缓冲 | 已核对 TLM 1.5.3 入口；实际 CS 画面待验收 |
| Epic Fight 刀光、命中粒子与闪电 | 使用实际粒子渲染器，导出三角形和原材质 | 生产入口与离线材质检查通过；技能画面待验收 |
| Epic Fight 残影 | 保留深度预写、`EQUAL` 彩色绘制的先后顺序 | 实际 GL 像素检查通过；游戏遮挡待验收 |
| NightFall 3.4.0 残影 | 在捕获作用域内调用核对过的 `AFTER_ENTITIES` 生产回调 | 生产字节码及作用域检查通过；实际技能待验收 |
| GIF 等动态纹理 | 跟踪实际上传和删除，失效后重新同步 | 纹理生命周期测试通过；实际 GIF 待验收 |
| VIX 后处理、自定义 GPU 着色器 | 暂不支持，记录原因 | 需要 CS 场景颜色／深度及有序后处理 |

这是通用绘制路径捕获：符合已支持 shader、顶点格式和材质语义的效果可以复用，不需要按每个表情或技能硬编码。它不保证任意 Mod 的自定义着色器、事件阶段或屏幕效果都兼容。

## 为什么原来会漏掉

截图中的图片来自 TLM 的表情气泡，匹配资源 `emoji_11.png`。其 `EntityGraphics` 直接调用 `BufferUploader.drawWithShader`，不经过原有实体顶点收集器。部分 Epic Fight 粒子也使用独立网格或特殊混合材质；NightFall 则需要被宿主帧省略的世界阶段回调先生成残影。

现在 [DirectMeshCapture](../neoforge/src/client/java/dev/goldcraft/client/DirectMeshCapture.java) 在实体和粒子导出期间拦截直接提交，转换实际顶点和排序索引；[HostedEffectStages](../neoforge/src/client/java/dev/goldcraft/client/HostedEffectStages.java) 调用已经核对的 NightFall 生产回调。普通 Minecraft、HUD 与其他阶段继续使用原绘制路径。未知手动 shader 和不支持的绘制状态不会被静默替换。

[ParticleSnapshot](../neoforge/src/main/java/dev/goldcraft/bridge/ParticleSnapshot.java) 携带深度测试／写入、颜色掩码、混合因子与方程、颜色调制和透明度裁剪语义。CS 的 [render_backend](../native/client/render_backend.cpp) 按原顺序提交效果，恢复宿主 GL 状态；效果不进入阴影或 GBuffer。纹理缓存只在内容失效时复制，不在每次 draw 时读回纹理。

## 构建与安装

先完成 [基础构建](BUILD.md) 与 [女仆扩展构建](EPICFIGHT_MAID.md)。下列命令已在工作区运行；部署前关闭相关沙盒游戏和 JVM：

```powershell
.\tools\Build-NeoForge.ps1 -Tasks build
.\tools\Build-Native.ps1
.\tools\Deploy-OptimizedNative.ps1 -BridgeOnly -Clients cs-client-a,cs-client-b
python tools/Modpack.py sync --refresh-core
python tools/Modpack.py plan --refresh-core
```

协议 23 的 Java 核心、CS 桥接 DLL、服务端桥接产物与 GoldCraft shaders 必须配套更新。`BridgeOnly` 也会同步 shaders，避免新 DLL 配旧 shader。Mod 更新需要 JVM 重启；安装文件一致不等于正在运行新版。

## 可复现检查

```powershell
python tools/Test-WorldEffects.py
python tools/Test-MaidCapture.py
```

| 证据 | 结论 | 对应实现 |
|---|---|---|
| 完整 Java 构建：23 suites、102 tests，零失败／错误 | 几何、索引、纹理、清理、缓冲及动画作用域回归通过 | `neoforge/src/test` |
| 完整 native 构建：35 CTests 通过 | 协议解析、状态恢复与生产 shader 的隔离 GL 检查通过 | `tests/native` |
| 生产映射入口：39 checks 通过 | 核对实际 JAR 中的 Mixin 目标和已安装 Mod 调用链 | `Test-WorldEffects.py` |
| 女仆捕获探针：编译／探针退出 0 | 3186 顶点的三角形拓扑、材质缓存隔离及预热复用通过 | `Test-MaidCapture.py` |
| 十个第三方组件的源码补丁重放通过 | 本地补丁可从锁定上游源码重建 | `Verify-SourcePatches.py` |
| 部署哈希与最终安装计划 | 主实例／A／B／服务端核心及 addon 一致，native 11 文件一致，待同步变化 0 | `Modpack.py`／部署工具 |

生产入口检查不执行 FML/Mixin，也不产生游戏像素。隔离 GL 检查不代表实际游戏 FPS。旧的 24 项独立快照测试包含已变更的纹理和 shader 源码，本次验收以当前完整 Java 构建为准。

## 实机验收与诊断

在 `cs_assault` 使用单 B、ReHLDS 专服与 24 SyPB Bots，触发表情／文字、刀光／闪电／残影和 NightFall 技能；检查墙后遮挡、透明混合、效果结束与换图清理。再检查 YSM 第一人称双手连续帧及 R 模式往返。性能比较使用同一观察者位置和朝向。

Java 状态提供 `entityEffectDraws`、`effectShaderDraws`、`effectDrawSources`、`effectUnsupportedReasons`、`effectTextureInvalidations`、`particleError` 与 NightFall 阶段计数。VIX 明确报告 `hostedVixPostProcessingSupported=false`。这些计数用于定位漏掉的绘制路径，不能单独证明画面正确。
