# 托管窗口的组合键、轮盘与枪械输入

2026-10-17 修复已按用户授权部署至本地 CS B，并同步 XMCL 母本及 A/B/server。两个客户端正常退出后已重启，沿用原公网双服，已重新配对并持续收取 HUD/实体数据；未向玩家游戏发送自动测试按键。公网服务端尚未更新。

实际 YSM 2.6.5 和 TACZ 1.1.8-hotfix-r7 都先检查 Minecraft 窗口焦点及鼠标捕获。CS 转发了按键和 NeoForge 回调，但后台 Minecraft 窗口的物理状态不满足条件，因此 ALT+Y、Z 轮盘、TACZ 开火/瞄准会被模组拒绝。已核对母本绑定：YSM 模型菜单为 ALT+Y，动画轮盘为 Z；该轮盘由按键切换。TACZ 开火还在每 tick 检查其自定义按键状态，不能仅伪造一次点击。

## 共用焦点语义

`HostFocus` 为已取得控制权的配对实例提供逻辑焦点。它依赖现有 250 毫秒输入时限，退出控制、失焦或不再收到有效输入时不会维持游戏操作。Minecraft 屏幕或 overlay 打开时，逻辑鼠标捕获为 false。CS 打开原生 GameUI（ESC）也撤销输入活跃标志，控制所属形态仍由服务器决定。

两个标准 Minecraft getter 暴露这一状态，保留真实 Keyboard/Mouse 转发和已存在的 InputUtil 物理按键表；没有逐个改写模组按键，也没有给 LWJGL 安装全局 Mixin。真实 Minecraft 窗口获得焦点时恢复原路径。物理焦点读取不使用被改写的 getter，避免递归。

逻辑捕获不会真正锁住后台 Minecraft 光标。后台 Mouse 的真实光标增量和 tick 被隔离、清零；转向和菜单坐标仍由 CS 输入通道负责，避免重复转向或抢夺前台鼠标。既有屏幕/失焦释放、世界/生命/序号校验继续有效，原生 CS 绑定保持。

## 自动验证

`tools/Test-HostControls.py --mods <本地Mod目录> --production-jar <正式Core JAR>` 不启动游戏、不发送窗口输入。它验证：

- 实际 Sponge 对固定 Minecraft 开发类的三个目标、六个注入点完成变换，288 个方法通过字节码校验；正式 Mojang 映射 JAR 对三个真实生产目标亦通过，280 个方法通过校验。
- 运行实际 TACZ/YSM 门控、YSM 组合键匹配、NeoForge KeyModifier 与原版 Screen 修饰符字节码。生产 HostKeys/HostFocus 分发通过 316 项检查，包括 ALT+Y/Z、96 次鼠标按放循环、重复/释放、菜单/overlay、输入到期、旧世界/生命/序号、重置和独立窗口回退。
- 实际生产 CreateMove 适配器验证原生 GameUI 阻断输入，150 帧转向、低帧率控制保持与其他已有生命周期检查通过。真实匹配 GoldSrc Key_Event 回归及菜单路由两项通过。
- 精确候选从公开基线加本轮改动独立构建，330 项 JUnit 通过。未完成的 MC 生命上限后半接线没有混入候选。

输入测试的 OS、网络及 Keyboard/Mouse 接收边界由观察适配器代替，不是玩家完整游玩或实际模组动画验收。证据位于 `build/host-input-20261017/{controls-final,native01,native-tests.xml,candidate-build01.log}`。33 项联合候选为 `build/candidates/combined-input-projectile-20261017/manifest.json`，本地部署与重启证据为同目录下的 `deployment.json`、`restart.json`、`deployment-verification.json`（均位于 `build/host-input-20261017`）。本地双服原先未运行，仅更新文件；公网二进制未改动，误封与枪械服务端修复须应用配套服务器包。

TACZ 的服务端伤害修复另见 [枪械伤害](PROJECTILE_DAMAGE.md)，移动指令误封另见 [限流计数](MOVE_COMMAND_RATE.md)。
