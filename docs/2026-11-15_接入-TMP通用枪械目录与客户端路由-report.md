# TMP 通用枪械目录与客户端路由

2026-11-15。TMP 的目录／协议和实际客户端消费者已接，尚未开放购买或部署。VP70 保留用户“测试可用”的验收。完整25个库存身份、变体、多人及性能范围不变，见 [CoF Goal](COF_GOAL.md)。

## Evidence → Finding → Path

原MP hl.dll SHA-256：0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63；原client.dll SHA-256：d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00。均为x86、基址0x10000000，下列为VA。只分析工作区副本；本轮先重核完整样本SHA，再复用修改前IDA证据。

| Evidence | Finding（均为高可信、已接源码） | 实际路径 |
| --- | --- | --- |
| E-01：analysis/cof-weapons-20261111/tmp-integration-20261115/firearm-directory-before.json；既有resource/map证据 | F-01：原TMP身份14，Info 0x1007E220的30/300，宿主900独立；不能把自动枪扩进Pistols | native/guest/cof/firearm_identity.hpp、catalog.hpp、scenario_damage.*、admission.hpp |
| E-02：client-directory-before.json、client-consumers-before.json；原HUD窗口1735字节、SHA 55e20eb50a9693eb4a94ccdac625816413247f082b8a808c7ec369b3dac6d4cb与PE核同 | F-02：0x1001925D的原14弹匣分支继续至0x1001952D公共准星尾部，保留aim/telescope/enable/hidden条件 | native/guest/cof/client/crosshair.*及实际guest_cof_crosshair.cpp |
| E-03：同目录client-studio-before.json、muzzle-before.json | F-03：TMP本地枪口0x1004BDEC、世界枪口0x100902BC，独立TMPMuzzle；五个Studio5004本地声音及5006原弹壳 | resource_catalog.hpp、client/resource_groups.hpp、CSOL客户端guest_cof_resources/effects/muzzle/particles/input/viewmodel.cpp |

P-01（callflow）：预缓存完成 → 显式资源分组 → 真实本地PCM／Aurora／Studio冷准备 → 持续匹配连接、资源代际和当前武器 → 原TMP消费者；断线、VideoInit和所有权退役保留清理边界。传输只新增已证原14，不扩大其他原武器准入。

## 宿主适配

通用Firearm目录覆盖四手枪与TMP；Pistols和旧接口保持四枪。固定数组、位掩码及直接查表，不在绘制中分配目录。伤害schema v2严格要求显式五枪，旧v1仍严格四枪且不赋予TMP配置。失败重载保持旧值／可用位，回退v1清除TMP位。当前服务器JSON未改，实际Host／购买尚未开放。

TMP价格2000、弹药批次30／价格30，规划灾变弹伤55／推击120／穿透0，为用户授权自定义平衡，非原CoF值；规划未写入部署配置。新通用准入解析仅接收0..31，缺掩码仍只表示Glock；旧手枪解析仍拒绝第16位。消息版本、长度、重放和地图／生命／库存实例验证保持。TMP的CSO HUD闭包／实际购买准入尚未接，不据组件准备发布购买能力。

## 验证层级

| 检查 | 结果与build回执 |
| --- | --- |
| 身份／伤害schema／能力 | 239检查通过：cof-tmp-firearm-directory-20261115-02 |
| 实际协议／表示服务 | 198905检查通过：cof-tmp-client-protocol-20261115-01 |
| 原准星指令对照 | 1448场景零差，含原14：cof-tmp-crosshair-original-20261115-01 |
| 实际SDK准星接收／绘制 | 1061检查通过：cof-tmp-crosshair-sdk-20261115-01 |
| 实际Studio／真实XAudio2 PCM | 288检查通过：cof-tmp-effects-sdk-20261115-01 |
| 实际五枪枪口／真实P模型 | 222检查通过：cof-tmp-muzzle-bridge-20261115-03 |
| 实际Aurora／消息／模型路由 | 217检查通过：cof-tmp-particles-sdk-20261115-02 |
| 实际五枪左右手RAII | 1418检查通过：cof-tmp-viewmodel-sdk-20261115-01 |

冷资源owner及输入／准入SDK终态下节补记。未完整构建新DLL、未部署TMP、未验游戏画面／听感／多人／实机CPU和帧时间。SDK验证零热模型／资源查询不等于真实游戏性能通过。

失败保留：枪口01／02因过期附件校验拒绝，夹具更新为当前驻留前缀插入并逐字节还原原文件后03过；粒子01在消息冲突负例后未复原夹具导致异常退出，02修正清理过。冷owner01缺独立左轮事件服务链接，后续明确控制为不提供该服务，未放松生产门禁。

## 模型对话更新的部署核对

用户询问后重新只读核查实际进程／内存CodeView，回执build/cof-model-deployment-audit-20261115-01/receipt.json。B PID22556实载GoldCraft 59498c0b…、ParallelDownload 00affa36…、CSOLSupport 6cfbf341…，内存与磁盘匹配；六个加载／下载配套文件逐SHA匹配模型对话部署计划。专服PID11196实载MP e63c0724…。未因VP70覆盖加载更新。

当前B真实状态：manifestVersion4、校验3360/3360、VFS打开6720次、leased0、172角色模型延迟登记、lazyPlayers1、错误为空。本轮未重启／改连／改游戏文件，正式服务器未更新。后续整合必须保留这套GoldCraft／下载器／符号目录配套。

## 下一步和共享文档交接

继续实际服务器TMP Host、主武器槽角色重绑、购买／弹药、原库存卡片和弹匣HUD素材、CSO手臂及P握姿，再完整构建、独立冷服／全HUD闭包、单B／多人／性能。目录与组件不计整枪完成。

PROJECT_STATE／PROGRESS待同步：TMP通用目录和客户端路由已接，分层验证如上，尚未注册购买／部署；模型对话六文件与172模型延迟登记已只读核实保留。GOAL保留25身份／变体全范围；TODO继续TMP实际Host／HUD购买及整枪、其余武器、手模／握姿、多人／性能。按用户要求不发非打包跨聊，精确领域交接在本报告落盘。

冷资源终态：build/cof-tmp-client-cold-sdk-20261115-02，1317检查零失败，真实19份WAV／XAudio2、TMP五声准备与热回放无资源查询、断线退役通过。左轮独立壳声事件服务在本夹具明确控制为缺失，不代表实际生产服务已验。输入01新增断言误置于代际清空之后，10404检查1失败；修正夹具顺序后继续02，生产未改。

输入／准入最终02：build/cof-tmp-client-input-sdk-20261115-02，10404检查零失败。包含TMP额外机瞄输入和原四枪购买能力不被提前扩大，实际CSO菜单／HUD解析器同跑。

复测示例（项目根Git Bash，输出须为新目录）：

```bash
python tests/cryoffear-host-admission/run.py --environment build/csol-season2-trace-20261109/compiler-env.json --commands build/resource-bridge-20261108/csol/compile_commands.json --presentation sandbox/cs-server/Half-Life/cstrike --output build/cof-tmp-input-recheck
```
