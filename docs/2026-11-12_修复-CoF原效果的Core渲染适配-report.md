# CoF 原效果的 Core 渲染适配

实际 B 使用 OpenGL 4.4 Core，原兼容 GL 弹孔和点光后端无法初始化，连带使 Glock 购买准入失败。本轮增加 Core 后端，保留原几何、纹理构建、颜色计算、可见性和顺序。实际 B 已确认弹孔资源、地图和点光全部准备完成；完整购买仍在处理另一个原生鼠标钩子初始化问题，不宣称整枪完成。

## Scope 与 Evidence

范围承接 [完整 CoF Goal](COF_GOAL.md)，只适配宿主图形接口。修改前合同在 `analysis/cof-weapons-20261111/client-evidence/core-render-20261112/before.json`；原 client SHA-256 为 `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`。原还原组件和证据入口见 [动态刷子点光记录](2026-11-12_实现-CoF动态刷子点光-report.md)。

| E-id | source_ref | content_hash／复现 |
| --- | --- | --- |
| E-001 | `native/client/render_backend.cpp` 的实际 profile 探测、Renderer `gl_rmain.cpp` 的 4.4 Core 创建、B 当前 profile 状态 | 本地运行收据；固定管线 MAX_TEXTURE_UNITS、矩阵栈、属性栈和 GL_POLYGON 在 Core 不可用，不能以兼容上下文通过冒充实际 B 支持。 |
| E-002 | 原 client 的 shot `10059450 → 10057DA0/100586C0` 与点光原组件 | `tests/cryoffear-custom-decal` 2476 原指令对照；点光 2048 矩阵、1024 gamma、1536 刷子变换、6144 texgen 及四类纹理输出零字节差异。私有原字节和模块 SHA 由原 runner 复核；原文不分发。 |
| E-003 | `build/cof-core-render-20261112-05/receipt.json` | before/after 全依赖 SHA 一致；`tests/cryoffear-core-render/run.py` 可复现真实兼容及 Core GPU 对照。106 检查通过；静态和移动／旋转刷子两条点光路径 RGB 差异均不超过 1/255，弹孔 RGB 一致，深度不变且无 GL 错误。 |
| E-004 | `build/cof-live-runtime-20261112-01/core-resources.log` 与 `gates.log` | 匹配当前 DLL/PDB 的只读调试：资源 Prepared、audio/particles Applied、Studio/decals true、CoreProfile true、pointPrepared/pointSceneReady true。完整运行日志私有，不能把某个运行地址套用到新进程。 |

## Findings 与 Path

F-001：`n/a_re / other / validated / high`，依据 E-001/E-003。失败来自宿主 GL 接口不兼容。`native/guest/cof/client/core_gl.hpp` 提供冷绑定的 Core API、真实状态保存和自有程序／VAO／VBO。未修改原版规则、散布、寿命或效果数量。

F-002：`n/a_re / reverse_algo / validated / high`，依据 E-002/E-003。`custom_decal.cpp` 保留已证裁剪、UV、mip、1024 标记环和乘色混合，仅用自有 Core 着色器绘制相同凸多边形。地图准备在冷路径完成，变化标记几何在绘制前准备，Draw 不读文件、不分配、不上传几何。

F-003：`n/a_re / reverse_algo / validated / high`，依据 E-002/E-003。`point_light_render.cpp` 保留原 CPU 的 float/x87 颜色、立方图与衰减纹理、PVS、刷子次序和两套混合顺序，Core shader 表达原 DOT3_RGBA 及纹理组合。默认 3D 路与 alpha 缓冲回放均有真实 GPU 对照；不声称着色器和固定管线逐硬件像素位相同。GL framebuffer alpha 能力来自实际附件查询，没有虚构固定值。

P-001，`path_type=callflow`：原组件输出（E-002）→ 实际 Core profile（E-001）→ 真实 Renderer opaque frame 的矩阵、深度、PVS 与刷子矩阵 → 自有 Core pass → 原颜色空间混合 → 实际 B 准备状态（E-004）。未经授权矩阵的 Core 绘制仍拒绝，未通过跳过门禁报告 ready。

`guest_cof_scene.cpp` 从真实 `MetaRendererOpaqueFrame` 传递矩阵；Core 路不再调用固定管线矩阵栈。主图形线程、上下文和生命周期保护继续存在，绘制回调无文件加载或材质解析。

## 构建、部署和边界

完整 x86 客户端 `4dfa443d8ec4f23fa2d0dd58030ba1e0aff81f975dc538c68a34e0a047a758fa` 已构建并本地部署，构建收据 `build/cof-client-checkpoint-20261112-core-02/receipt.json` 的源输入无漂移。后续冷鼠标诊断另建候选，不混淆对应 PDB。

时间线：实机只读定位 DecalFailed → 确认实际 Core context → 修改前记录接口映射 → 保留原组件并增加 Core 路 → 原指令／兼容 GPU 回归 → Core／兼容静态及变换刷子对照 → 完整构建、本地部署和实际准备状态核验。早期默认 framebuffer 查询了不存在的 back attachment，修正为真实 draw attachment 后通过，失败记录保留。

此轮 GPU 夹具不是游戏 FPS 或性能采样；实际武器画面、射击音效、多人、图形压力和完整购买／机瞄／推搡仍待。另有 v12 菜单资源配套不齐，已生成含 Glock 的 v4 JSON 和 105 引用资源，实际 HTTP 广告待匹配服务端更新。完整 Goal 保持 active。
