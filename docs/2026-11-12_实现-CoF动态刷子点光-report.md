# CoF 动态刷子点光

已补齐独立点光后端对普通移动/旋转刷子的绘制。完整客户端/Renderer 接线正在构建，未部署、未验收枪械玩法或真实性能。

## 原版证据

CoF `client.dll` 为 PE32 x86，映像基址 `10000000`，SHA-256 `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`。

| 原函数 | 结论 | 对应源码 |
| --- | --- | --- |
| `1005F570` / `1005FBD0` | 普通刷子先建立其实际变换和前向可见面，再进入动态光阶段 | 宿主提供本帧实际提交矩阵及已解析位置/角度，独立后端接收 `PointLightBrush` |
| `1005ADA0` | 光源坐标先减实体原点；旋转时依次与 forward/right/up 点乘，right 分量取负 | `native/guest/cof/client/point_light_render.cpp` 的 `PointLightBrushOrigin` |
| `1005B300` / `1005B630` / `1005B6D0` | 世界/静态刷子在先，变换刷子分别执行各光源和原多 pass；保留面朝向、严格界限及光照衰减 | 同文件 `Draw`、`Brush`、`DrawBatch` |

修改前已核原三个完整函数 1370/1618/205 字节，私证 `analysis/cof-weapons-20261111/client-evidence/moving-pointlight-before-20261112.json`。宿主 Renderer 的 `R_RotateForEntity` 含移动插值，因此直接用网络 origin/angles 重建矩阵无法证明匹配 opaque depth。新增宿主快照复用实际提交矩阵，属于移植适配，不声称原 CoF 使用同一接口。

## 验证与边界

实际 x86 C++20/O3/ThinLTO/AVX2/precise 编译通过。新增 1536 组原指令局部坐标变换逐位一致；既有纹理数据、2048 矩阵、6144 texgen 平面及 1024 gamma 原指令对照保持零字节差异。

真实隐藏 WGL/NVIDIA 4090 的 105 项检查通过，包括移动/旋转刷子、原两种衰减路径及 NV 路径、实际深度相等绘制、相机/深度/模板与纹理状态恢复、背面拒绝和明确生命周期拒绝。收据 `build/cof-point-light-moving-20261112-01/compile-receipt.json`、`differential.json`、`gl-result.json`。隐藏 GL 夹具不是游戏 FPS 或场景玩法验收。

生产代码在冷准备时建立几何和有界绘制索引；绘制复用资源及快照，不增加文件读取、堆分配或顶点上传。没有新增实机 CPU/GPU 性能采样，不宣称帧率改善。带专用 portal renderfx 70/137/184 的刷子仍明确拒绝，不能把普通旋转支持当成原 portal 分支完成。
