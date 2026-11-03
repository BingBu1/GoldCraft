# SKULL-2 双模式行为与 SDK 接入审查

2026-11-03。本轮确认原 SKULL-2 的单/双枪状态机、62项属性、三种客户端事件，以及现有 ReGameDLL/MetaHook 接入限制；没有修改生产武器代码、编译、部署或启动游戏。详细合同位于 `analysis/csol-model-20261017/skull-balrog11-20261102/skull2-review-20261103/contract.md`，范围位于同目录 `scope.md`。

## 证据 → 发现 → 实现路径

| Evidence | Finding | Path |
|---|---|---|
| 原 MP `10F837E0`、`10F82F80`、`10F82EE0`；43组受控原指令 | 3秒模式切换，仅now>deadline提交；收枪取消并回来源模式 | 独立实体状态机；服务器权威及client预测使用同一语义 |
| 原 MP `10F82B80`、HW record构造及实际SDK`iMaxClip` | A7/B14，pending均查询7；API启用时SDK已有每实例ItemInfo | 只更新实例容量；禁止变更全局武器注册表污染其他玩家 |
| 属性构造 `10F7F910..10F828D0` 的62项原值 | A/B普通54/45、生化78/68、灾变195/185；射速.24/.16但两者都是半自动 | 自动模式伤害分配；保留模式、射击门和三类伤害 |
| MP A/B Fire与client三handler | 服务端A弹道27/B弹道1，client视觉均27；方向事件字段及左右动画不相同 | 分离服务器弹道和三种client事件，不能复用错误的参数布局 |
| MP原表v22C/v230、HW record+48..+58 | 普通怪分派true/false，目录击退350/250/300/100/.6 | 沿现有普通怪精确类分派；不将另一套属性击退当成普通怪路径 |
| 安装的专服delta、SDK GetWeaponData及真实编译参数 | fuser4未传输；m_iWeaponState为7位；REGAMEDLL_API已启用 | 可用已有计时字段传专用切换倒计时，但必须补量化/等值边界测试 |

## 原版关键行为

噬血狂袭SKULL-2 的原ID为312、物品ID828，副武器，原价1550，使用与SKULL-1共享的`50AZ`，普通备弹35，买弹70/7。当前common已存在相关模型、HUD及声音，不需要重复搜索归档包。

原武器Reset初始双枪，A/B切换各3秒。状态提交A→B至多从备弹补7发，B→A将clip超过7的部分退回备弹且不在此处钳上限。Holster发生在pending时取消转换。GetMaxClip只看B位，所以pending B→A时虽然旧clip可超过7，查询容量已是7；必须保留转换时机和弹药守恒。

两种模式都是半自动。单枪伤害较高、间隔.24秒，双枪间隔.16秒。A开火后idle1秒，B2秒；真正Idle再次等待30秒。双枪左右动画由剩余clip选择7/9或10/12，未用模型中另外两条候选动画；A事件始终序列3，不能按模型名称自行增加空弹动作。

原服务器与client视觉弹道类型、距离和事件字段存在差异；具体值和地址均写入合同。SKULL2的普通怪击退分派也不同于SKULL7，普通怪击杀回调则是已核空函数。实现代理需要按精确武器类接入这些差异。

## 验证级别

- 静态：IDA有效字节逐段与匹配原PE核同；模块、原IDB与研究副本PE的SHA/file-ID/链接数保持。研究副本IDB可自动保存。有效段数及排除的失败/截断查询以`evidence.json`为准。
- 原指令受控执行：62个原属性；43组模式切换/容量/取消/等值截止边界。基础类、目录和动画API处显式spy，这不是完整SDK或游戏结果。
- SDK只读审查：核实际`CSPlayerItem`实例字段、`iMaxClip`、Spawn初始化、GetWeaponData、已安装delta，以及已存在的服务端编译提案宏。没有本轮源码编译。
- 运行时：本轮没有专服、图形客户端、多人、网络或渲染验证，也没有永久部署。

本轮明确未决：原v1BC强制双枪补弹回调的生命周期调用方、原默认7发在完整拾取链中的最终结果，以及A抛壳消费。Fire/视觉只有静态证据；不能把43组状态机测试算作完整武器交付。

## 可复核入口

在工作区运行以下研究脚本（需要Python、Unicorn和用户本地原样本；不发布原DLL/反汇编资源）：

```powershell
python analysis/csol-model-20261017/skull-balrog11-20261102/skull2-review-20261103/capture_properties.py
python analysis/csol-model-20261017/skull-balrog11-20261102/skull2-review-20261103/state_oracle.py
python analysis/csol-model-20261017/skull-balrog11-20261102/skull2-review-20261103/verify_evidence.py
```

`before.json`记录研究前身份，`evidence.json`记录原件保持、字节核对、原指令结果和SDK输入SHA，`manifest.json`记录本轮研究产物。原属性构造器spy的上游脚本SHA也在回执中。失败的过期会话查询、工具截断预览均保留但被明确排除；完整工具输出单独核验。

下一步由父实现代理按合同编写每实例双模式武器，先完成未决的拾取/回调链，再验证双玩家容量隔离、原严格截止比较、原事件随机/弹壳/动画、三模式伤害与普通怪分派。只有源码、整DLL、专服和图形多人分别具备证据后，才可宣称SKULL-2可交付。
