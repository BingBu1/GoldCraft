# Minecraft 发光轮廓

共用轮廓合成器已经实现并通过原版着色器逐像素对照。当前阶段尚未接入实体导出、协议和宿主场景回调，也未实现原生 CS 身体的权威发光状态；游戏里的发光效果仍未完成。最新联合候选继续使用枪镜阶段的版本，没有因此更新运行 DLL。

## 实现

`native/client/entity_outline.cpp` 接收当前宿主视角下绘制的实体轮廓遮罩，在独立 RGBA8 目标中完成五邻域边缘检测、横向和纵向半像素线性箱式模糊、原版末尾 blit，再合成到宿主场景。实现独立表达算法；Minecraft 的着色器文件仅由测试工具从本地依赖提取，不发布。

边缘检测和末尾 blit 都保留原版 SRC_ALPHA / ONE_MINUS_SRC_ALPHA 混合。每个中间目标保持 RGBA8 夹限与量化；最后一次 blit 不能简单省略，它会再次改变颜色与 alpha。最终场景 alpha 保持原值，线性目标使用调用方提供的实际宿主 gamma 指数转换轮廓 RGB。原版世界合成的独立 alpha 因子为 ZERO / ONE，Framebuffer 绘制也关闭 alpha 写入。

几何回调必须使用所提供的真实宿主矩阵与 viewport，输出队伍颜色并保留纹理 alpha 裁剪。私有遮罩不挂宿主深度，因此不会被墙壁遮挡；后续接线不能以普通 GlowShell、包围框或 HUD 假轮廓代替实际模型。

目标内存上限为 256 MiB，包含 resize 同时保留的新旧资源。稳定尺寸复用三个纹理；实测预热后不增加 C++ 分配或纹理 storage 调用。只有完整图像处理成功才复制回宿主颜色的 viewport 范围，保持宿主深度、模板、其他 MRT 和范围外像素。GL 状态按作用域恢复，禁止跨 context 删除资源。不保留借来的宿主描述符和附件句柄。

## 验证

证据在本地 `build/entity-outline-20261018`。生产 GoldCraft 目标与独立 GPU 测试均使用既定 x86 Clang / C++20 / O3 / ThinLTO / AVX2 / 精确浮点构建。组件尚无运行调用方，因此不能用 DLL 构建成功表示功能已接通。

`gpu02/result.json` 固定测试二进制 SHA，原版十份资源逐项 SHA 和管线 JSON 校验。测试直接编译原版 `sobel.vsh`、`blur.vsh`、`blit.vsh` 与对应 fragment shader，在真实 GL core/compat 两种上下文中对照新实现。原版四遍处理在独立 FBO 执行，合成预期再保留宿主 alpha。

两种上下文共 32 组尺寸/存储/颜色空间组合，1,799,505 项像素分量及状态检查通过，包含奇数横竖屏、1×1、子 viewport、彩色/空遮罩、重复帧、RGBA8/HDR、深度/模板/MRT 保持、完整 GL 状态恢复、回调拒绝与 GL 错误、分配失败后的旧代恢复、跨 context 拒绝和无 context 空对象析构。比较容差为 RGBA8 约一阶量化、RGBA16F 0.0011；不是宣称所有浮点输出逐位相同。

```powershell
python -X utf8 tools/Test-EntityOutline.py --output build/entity-outline-check --exe build/native-x86/Release/goldcraft_entity_outline_tests.exe
```

测试使用合成几何遮罩，尚未覆盖实际 MC 模型、透明纹理采集、隐身实体、服务器发光权限或真人画面，也不是 FPS 测量。

## 下一步

接入真实 `hasOutline` / `getAffectedOutline` 和队伍颜色，使用明确可选能力传递与普通实体同一生命/同一帧的轮廓，保持旧解析器严格。随后接入原生 CS 身体的权威状态、真实模型遮罩和 PVS 可见性，以及两种形态的死亡/到期/断链/换图清理。完成编译与离线验证后才加入同一批联合实机。
