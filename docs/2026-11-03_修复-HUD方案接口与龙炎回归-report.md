# HUD 方案接口与龙炎回归

用户报告左下人物 HUD 和右侧武器 HUD 消失，龙炎普通弹药显示错误、积攒龙炎弹不显示，以及右键效果与原版差异较大。本次优先修复实际显示回归，同时重新核对龙炎的原 MP/client 行为。

## 已定位的 HUD 问题

当前 B 运行 `CSOLSupport.dll` SHA-256 `e65c7ed8acbc285a336dd059c4555412c82fa7e5d9d68d8b5b4df346f04d013c`。HUD 目录、字体、方案和图像已经实际下载到 `cstrike_downloads`，不是资源未安装到 `cstrike`；十四张基础图像尺寸与消费者合同一致。

`InitializeHudStyle` 错用引擎工厂查询 `VGUI_Scheme009`。匹配 MetaHookSv 的 `VGUI2Extension/src/BaseUI.cpp` 从已加载的 `vgui2.dll` 获取该接口，引擎工厂只提供绘制 Surface。HUD 的 `Prepare` 在方案管理器为空时直接返回，造成头像、生命护甲样式和右侧武器卡一起缺失；独立准备的金钱面板仍可显示。

对单 B PID 348 的非侵入 CDB 只读检查确认：两个 `schemes` 全局均为空，而 `additiveSurface` 为真。当前 hw SHA `9ba9a2db…` 匹配项目的 10210 目录，不能归因于非 HL25 引擎。证据：`build/csol-character-hud-regression-20261103/live-before/schemes-symbols.log`。

源码已修正 HUD 和同样受影响的复活 HUD 接口来源，并增加接口、图像和三个弹药字段的诊断。独立验证将执行完整初始化入口，使用相互分离的引擎／VGUI 工厂，不能像旧夹具一样预先填入 `schemes` 指针绕过真实故障路径。当前尚未部署修复 DLL，不能宣称画面恢复。

## 龙炎边界

当前模块已有 `predictedSecondary=Balrog11PredictedCharges`，服务端也有 `current.secondary` 发送链；不能错误归因为没有注册该字段。现代 HUD 未就绪时现有备用弹药绘制没有龙炎副弹药区域，先修复真实 HUD 初始化，再核对消息、预测和最终数字。

右键效果正在重新核对原 MP 发射、TempEmit 创建及 client 最终绘制。原 MP 的事件 `iparam1=7` 已有指令证据，不能擅自改为俯仰角；`iparam2` 原来源为武器字段，当前直接使用玩家 punchangle 的等价性仍待核实。粒子 Tick 的旧单项测试不代替创建、轨迹和最终透明混合的验收。

## 同批资源下载

上一轮 704 项预期资源，客户端 689 项实际文件与源 SHA 相同；余下 15 项为 `.mat.json`。匹配 hw 下载谓词拒绝首末点号不同的名称，正在配套迁移到 `_mat.json`，保留旧本地表读取兼容。该下载修复与 HUD 接口错误是两条独立原因。

正式远端没有更新。完整 goal、龙炎效果、人物技能崩溃及多人验证仍保持开放。

## 本地部署与验证检查点

HUD 完整初始化测试 124 项、根复活初始化 11 项通过；旧源码分别精准失败 7 和 5 项。实际运行基线的窄复活初始化另有 10 项通过。完整客户端 4 TU＋链接、201 对象与 13544 输入核验通过，DLL `d8453aef…`；引擎 2 TU＋链接及 6949 输入核验通过，DLL `8b26eba6…`。运行基线沿用此前版本的复活实现范围；main 已提交的 RetryBudget 保持，不用冻结运行源覆盖根文件。

19 文件原子部署已完成：B 的 DLL/PDB 两项、服务器引擎 DLL/PDB 两项与 15 份 canonical 材质表。旧表、本地配置及保护安装保持；没有预填 B 材质文件。专服启动后 15 项 HTTP 正文核验通过，单 B 自身下载的 15 个实际文件与源 SHA-256 全部相同。

父级复核 66 个交接、源快照、部署引用。运行中只读 CDB 看到两个 scheme 从空指针变成有效接口，HUD 标题字体句柄已经创建。窗口工具随后报告实体 Escape，键鼠测试停止；这些运行内部证据不等于最终画面、普通/蓄弹数量或右键观感通过。

证据：`build/csol-character-hud-regression-20261103/{interfaces-01,interfaces-03,deploy-01,live-after-01}`，`build/csol-material-download-20261103/handoff-final-02.json`。

## 碰撞回归继续

用户指出原火焰穿透僵尸，而移植像被弹开。原 MP `106243F9` 每次 Think 恢复实体保存的发射速度；移植遗漏该写回，使引擎碰撞裁剪后的速度继续沿用。已补候选，同时继续核对完整穿透物理链，不将速度字段修正独自视作穿透验收。原事件 `iparam2` 来自构造清零的武器字段，当前玩家纵向 punch 错用为 yaw 的双端修正也在验证中。

原粒子尺寸、透明度、扇形角度及速度有一致证据，不能为了“饱满”任意调大。后续需实测五路轨迹跨实体、碰墙销毁、共享命中去重及尾迹持续生成。
