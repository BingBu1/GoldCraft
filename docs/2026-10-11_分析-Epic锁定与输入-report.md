# Epic 锁定箭头与宿主相对转向

日期：2026-10-11。范围限工作区沙箱的 Minecraft 1.21.1 / NeoForge 21.1.256 / Epic Fight 21.17.3.1。用户授权修复并要求与其它聊天错开生产改动；原游戏安装只读。协议28已配套部署到单B，实机箭头与绕行尚待用户验收。

## 根因与证据

| Evidence | Finding | Path |
|---|---|---|
| E01：本地实际 Epic JAR SHA256 `8b882554cf10086398340fbdc741819ee72a801a3adce516c7f4768326a39526` 的 RenderEngine / EntityUI / TargetIndicator 方法；本项目 EmptyEntityRenderer 与 EntityExporter 跳过原生代理 | F01，n/a_re，静态已验证、高置信度：CS 原生代理不会经过 LivingEntityRenderer 的 UI 回调，故缺少箭头 | P01，callflow：EntityExporter → HostEpicIndicator → 原版 shouldDraw/draw → DirectMeshCapture → 实体原子帧 |
| E02：原 HostInput 每tick setYaw/setPitch；同一 Epic JAR 的 turnCamera/getRelativeMove | F02，n/a_re，已通过离线回归、高置信度：宿主绝对角覆盖锁定旋转，移动方向又使用玩家yaw | P02，callflow：CL_CreateMove角差 → INPUT flags3 → HostLookDelta → 原版Epic turnCamera；未消费则vanilla changeLookDirection |
| E03：`tools/Test-HostLook.py` 的实际CreateMove提取和实际Epic/MC字节码执行；本地输出 `analysis/epic-lock-presentation/production-check` | F03，n/a_re，离线已验证、高置信度：相对/绝对帧不会混淆，192组W/A/S/D向量符合原版；不代表实际引擎鼠标或画面验收 | P03，solve：原生菜单/失焦/租约失效帧清理 → 恢复后仅消费本帧角差 |

目标箭头直接使用原版纹理、UV、billboard和RenderType；没有复制Epic实现或把代理变为全局可见。权威world/slot/serial/life、存活、阵营和控制条件仍由HostEpicTargets验证。

协议28的INPUT仍为48字节；flags bit0=active、bit1=relativeLook。两端版本不匹配会拒绝握手。只有flags3的角差参与转向；菜单、失焦、控制失效和身份变化不重放旧角度。初始玩家角度沿用服务器传送。

## 复现

在已准备本项目依赖和合法Epic安装的工作区，执行：

```powershell
python tools/Test-HostLook.py
pwsh -NoProfile -File tools/Build-NeoForge.ps1
pwsh -NoProfile -File tools/Build-Native.ps1 -Server
```

独立脚本运行生产CreateMove函数和wire编码，使用CPU宿主适配器；从固定JAR临时提取3个Epic方法和vanilla向量方法。仅按实际Epic access transformer公开getInputVector，方法体未改；另验证箭头桥接所有Epic API描述符。临时字节码不发布。Java单测覆盖多输入聚合、一次消费、连续pitch、失焦和租约恢复。

## 进度与边界

- 用户已确认此前锁定选择有效，但箭头与绕行未通过。
- 2026-10-11：隔离候选测试完成；IPC发布7e300785后本聊天接管构建窗口。
- 166项Java测试通过；首次旧协议版本断言失败记录保留。
- 完整166项Java、44项native、ReGameDLL与bridge166+44通过；Clang11组1641单元15个x86产物通过。
- 真实B已配对并输出hostInputFlags=3；当前箭头API解析无错误。箭头像素、锁定/解除锁定/绕行手感等待用户复测，不能用计数代替画面。
- 原安装19572文件本轮前后内容/大小/mtime不变。
- 不死斩/次元斩伤害与Epic技能反馈空槽异常独立排队。本修复未修改伤害权限或技能范围，完整项目未完成。
