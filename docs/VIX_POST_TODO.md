# VIX / Epic 后处理待接入

- [x] MetaHookSv 场景回调：在 `R_RenderScene()` 后、CS viewmodel 前提供真实颜色/深度和矩阵。
- [x] GoldCraft GPU 快照：复用纹理/FBO，保存 CS 场景颜色、深度和 stencil，不读回 CPU。
- [x] Epic Fight 普通刀光、命中、闪电及女仆网格走协议 23。
- [ ] VIX `PostParticleRenderType`：导出真实遮罩顶点、顶点格式、纹理修订和深度策略。
- [ ] VIX 有序通道：保留 priority 队列实际出队顺序，支持 bloom、space broken、chromatic、air disturbance 和屏幕效果。
- [ ] CS 端按白名单 shader 语义执行 ping-pong 后处理，未识别通道必须清空并上报 unsupported。
- [ ] 用真实 CS 场景颜色/深度验证墙体遮挡、重叠顺序、空帧、resize、map/disconnect/form 切换和失败清理。

当前状态：Renderer 与快照离线测试通过；VIX 后处理仍未部署或实机验收。不要将普通 Epic 网格导出误认为泛光/折射已同步。
