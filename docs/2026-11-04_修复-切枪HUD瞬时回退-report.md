# 切枪时弹药短暂回退到黄色旧样式

用户截图中的 K3 弹药在切枪瞬间变成黄色。修复已经通过独立 SDK 回归，并合入 HUD 层级／死亡 HUD 客户端构建；实际部署和画面验证另行记录，不能以编译替代。

## 证据与原因

E-01：生产 `HudStyleFrame` 提前调用 `PrepareSelection`，而 `CurWeapon`／预测可以在随后更新当前武器。旧 `BeginHudStyle` 只比较当帧武器名称与此前准备的 `weaponName`；失配时关闭 `weaponVisible`。`WeaponRedraw` 随后执行备用弹药绘制，K3 模块的备用颜色为 `(255,160,0)`。这能解释截图中的黄色 `150 | 200`，不应全部归因于原生 HUD 精灵拦截失效。

E-02：`build/csol-hud-switch-20261104/sdk02/result.json` 保存独立回归与输入身份。工作副本和实际部署旧源码加本次四行修复各通过 1039 项；HEAD `c4c3b79` 和部署旧源码各失败 208 项。旧版在 37 次切枪首绘出现错误的绘制归属判断；这不是对 37 张实际游戏黄色像素的统计。结果文件 SHA-256 为 `4ce028d9bda868b65d1eb6df097fd5678d281c70343dfa6d1b6dff6730763ba3`。

F-01（源码与 SDK 已验证）：资源选择与武器状态跨了两个时点。调用路径为 `HUD_Frame → 武器消息/预测 → WeaponRedraw → BeginHudStyle → 备用弹药绘制`，不是游戏资源缺失。

修复在 `BeginHudStyle` 捕获当前武器后、判定绘制归属之前重新准备选择。资源完整时同次绘制直接使用新武器图标、标题和弹量；稳定帧不重复读图。确实缺失资源、禁用样式、死亡／隐藏和观察者场景保留原有回退规则。

## 回归范围

真实 HUD 初始化、资源消费者和 Paint 使用匹配 MetaHook/VGUI SDK。测试覆盖 K3、黑龙炮、普通 M4 和刀互切，同帧快速切回、第一帧数字／颜色、隐藏、真缺图和下载恢复。武器消息和绘图接口是受控边界，未在该夹具执行完整旧弹药渲染器或真实 GPU。

复现入口为 `tests/csol-hud-style/run_switch.py`。参数需要本机已验 SDK proposal、实际部署 HUD 源与一个新的 build 输出目录；示例参数和值记录在 `build/csol-hud-switch-20261104/sdk02/jobs.json` 和 `test-handoff.json`，游戏资源与构建环境不随公开仓库分发。

## 客户端合批

与 [灾变 HUD 层级／死亡 HUD](2026-11-04_修复-灾变HUD层级与死亡HUD-report.md) 合并。完整构建 `client-hud-full-01` 通过 15 TU＋链接、203 对象来源和 x86 DLL/PDB 配对检查，13789 输入保持、未知依赖为 0。客户端 DLL SHA-256：`6d8ad418c861be6fe45b143de442f590741c7968832d4ddf4ec79846c42a4155`。

构建保持已部署音效修复及 v7 购买菜单，仅套用已审 HUD 改动。服务端 restart 优化、缺失 BOSS 血条资源、v8 菜单和当前 7 键技能卡在 Preparing 的问题不因本客户端构建而视为完成。

## 本地部署与实机采样

已原子替换单 B 的 CSOLSupport DLL/PDB 两项，专服与资源未改；部署回执 `build/csol-hud-switch-20261104/deploy-client-01/receipt.json`。B 使用新 DLL 入服穷途末路。七次实际槽位与左键确认切换保存到 `live-01/switches.json`：K3 三帧显示149/200、USP12/24、刀仅武器卡、飞刀蓝30、黑龙炮蓝20，图像与客户端诊断一致。独立审查重新核对全部原始 BMP SHA，记录于 `visual-review-01/result.json`。

这是原生引擎测试输入与真实游戏渲染采样，不是每帧录像；首帧所有权缺口由 SDK 正旧对照覆盖。第八张尝试打开购买菜单时实际未保持显示，诊断 visible=0，不能作为菜单层级验收。死亡／受控复活另存 `live-01/death-respawn.json`；死亡画面无血量、护甲和弹药，测试探针复活后重新显示100/0与K3 150/200，不代表自然复活流程已验收。人物／购买／选图全部遮挡组合与普通观察者画面仍待。
