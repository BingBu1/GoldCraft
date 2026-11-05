# SKULL3 客户端实现前合同

2026-11-05 子代理负责 SKULL3 客户端、受命补齐的默认配对标题、资源收集器及对应验证。完整构建只按父审核的冻结选中集执行；不部署、操作界面、写 Git 索引或修改共享状态文档。

原 client.dll SHA-256 f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd，base 01900000。本次先重新从该 PE 逐段校验 build/skull3-client-audit-20261105/final-evidence-verification.json 的 63 个字节范围，全部 SHA 相符，再开始实现。

证据→实现：025B6770/025B6B50/025B6F10→三事件、本地动画、body2、双持右枪 boolean 末弹分支；02A440D0/02A43FE0→弹壳参数和旋转倍率；02567EF0→单层烟雾及随机顺序；0278C740/0278F620→独立实体换弹1/14与2.3/3.3秒；0278C8C0→单持90/64变焦且无独立缩放声；0278E770/02791480→先增加shots并更新accuracy后判空弹、双持左位翻转、两攻击时钟；0278C810/0278F6B0→idle0/6/7和20/60秒。原客户端虚函数025CEE70直接ret1Ch，不计算KickBack。

宿主适配：沿用 ActiveWeaponSlot 传当前实体及 vuser4[1] 共备弹，每次从 authoritative/prediction input 读取该实体，禁止以同一clip缓存代替两实体。父指定 fuser1=accuracy，fuser2=DecreaseShotsFired-now，fuser3=shots/128，m_iWeaponState bit0=left、bit1=delayFire、bit2=direction。Deploy 使用父服务端已执行的结果（clip/weaponanim/nextAttack），不在每帧重复转换装弹。两个独立 descriptor 给不同准星参数，只有单持 descriptor 注册处理两ID的预测回调，避免双调。

## 实现后补证和宿主边界

共用原客户端 `025D1B10..025D2128` 已完整取得 IDA 374 条指令并比对原 PE。新增证据在 `build/skull3-audit-20261105/client-frame/proof.json`，另含虚表与材质查找消费者；世界回调证据在 `client-world/proof.json`。客户端松键 `025D1FE1..025D202B` 与 MP `115436EE..115437AE` **不同**：delayFire 已置位时清除它，shots<16 则加4；没有MP的 .4/.0225 定时衰减，也没有归零时重置accuracy。原142/143 metadata flags=0x201B，因此v200/v204的0x400/0x800门均不成立。客户端只保留fuser2传输时钟，服务端继续执行自己的原逻辑。

A v2C4=`0278C980` 返回1，D v2C4=`0278F780` 返回0，因此双持在分发前屏蔽右键；同时按左右键仍射击，只有右键仍可待机/自动换弹。v2BC=`01E37260` 返回0，不在攻击或冷却分支额外播Idle。换弹完成保留dual左右位，不复制SKULL2的奇偶重排。

type28世界/BSP消费者 `02569450`→`0256C0F0`→`02568CF0` 的两段视觉推进使用power70、limit4000；整数材质衰减、两层I材质烟、具体声音/贴花/streak由原字节和 `bullet-oracle-03.json` 确认。原 `02569FCF` 只检查零移动距离，fraction=1仍可能产生该次世界效果，不能照普通宿主代码提前break。枪口与子弹发射点分别保留原28/17远端站立高度差。

墙烟callback `0256F750` 的6/7帧门、风值.075至5、Y后X随机翻向，及通用碰撞投影`0256EFE0`已直接执行原指令对比。方向初值1来自原.data，风值0来自BSS；这些风变量不会在普通切枪Reset时清空。墙烟不受只控制枪口烟的gunsmoke开关影响。

明确宿主适配：世界材质先按原规则剥离前缀/截16字符，再调用宿主真实 `HUD_PlayerMoveTexture` 材质消费者；宿主资源表与其原生长度规则需在完整地图实测继续确认。世界/BSP允许原贴花/烟/火星；普通studio实体不在本枪重复造血或火星，由统一权威 `CsoMonFX` 消费。原model+1F8分类、model.flags的CSOL扩展标记、装备散布修正/曳光皮肤尚无完整宿主映射，不猜测分类或把hitgroup8统一当盾牌。

## 验证范围

实际x86 SDK、C++20/O3/ThinLTO/AVX2/precise构建完整 `skull3.cpp`，从独立冻结文件编译，所有输入核SHA和非硬链接：

- `client-events-02/result.json`：144个原事件case、9196比较，0失败。
- `client-prediction-03/result.json`：803检查、118次宿主预测分发，0失败；覆盖两独立ID/共享回调、左位、重新播放、单双右键、换弹完成、松键计数、禁止预测KickBack、存活/回合/部署门。该组为实际生产TU行为检查，不声称完整原指令预测回放。
- `client-bullets-03/result.json`：15个原世界材质/距离case、1225比较，0失败，另核 studio 实体不走本枪火星/造血。先前01揭示fraction1错误已修，失败记录保留。
- `client-callbacks-02/result.json`：240个原回调case、2640比较，0失败。01仅为runner字段路由错误，未产生行为比较结果。

测试命令：`python tests/csol-skull3/client_events.py --fixture events|prediction|bullets|callbacks --output <新build目录> --baseline build/csol-skull5-integration-20261103/sdk-01/proposal.json`；除prediction外使用 `--oracle <对应原指令JSON>`。资源下载、听感/画面和实际伤害由父合并后验证。

完整客户端 `build/skull3-audit-20261105/client-full-01` 已完成 23 TU + 1 link，206 对象完整来源、14269 输入稳定、0 未知 include、PE/PDB 匹配。该产物包含 SKULL3 事件/预测，尚不包含后加的配对标题和四项墙烟资源。匹配 PDB 的实际 DLL 加载两次、静态注册检查 2179/0：11 个 descriptor、13 个 ID，D 只供展示元数据，不重复注册 A/D 生命周期。

## 默认配对标题和资源闭包

原 client `02A260E0..02A26450`、同槽首项 `02A25D30..02A25D57`，TITLE-08 取得 919 字节/256 指令并核原 PE。原指令尾部 12 场景、19 检查证明：普通标题分支在同槽首项存在、不等于当前项且其 record+44 配对 ID 等于当前 ID 时递归；不是缺翻译触发，也不是无条件 D→A。

`client/weapon_inventory.cpp` 根据已提交的完整库存和实际槽位/位置找首项，只映射已核 SKULL3 A→D 配对。宿主允许更长槽位列表，用现有选择菜单的 position/ID 顺序对应原首项；不修改协议或虚构其他配对。缓存仅在定义、完整库存和重置后失效。`client/hud_style.cpp` 独立缓存标题来源 ID/槽号，图卡和弹药仍取当前 D 的目录。没有接入原可定制物品标题服务，不声称支持涂装/强化标题分支。

`client-title-04/result.json`：完整实际库存 TU 19/0、完整实际 HUD 消费者 103/0、原切枪 HUD 回归 1039/0，共 1161/0。测试覆盖同 ID 库存变化、其他首项、A 不在/跨槽/后到定义、D 在首位、未完成的多段库存及视频重置。受控 VGUI API 通过不等于实际屏幕像素验收。

资源工具 `collector-03` 使用审核冻结输入。generated 描述走真正的 exists/read；已准备根目录优先 canonical 文件，源图集别名仅为回退，JSON source 与所用文件一致。`Skull3Resources` 补全实际 client 消费的 wall_puff1..4。`collector-resources-03` 原 common/HUD 坐标检查 910/0；真实 `Prepare-CSOLResources --relocate-materials` 制备 35 文件，运行时 63 字节路径复读 76/0。四模型合并闭包与单独 v 模型闭包相同。最后标题/四烟头需父合并到下一完整 DLL；未进行游戏输入或部署。

后续 full02 已完成标题/四烟头合并，DLL `4a0618182df8f4e116cf00ffaaae8b34aa98f4ae73faf13256d56bb036d897c8`。匹配 PDB 的 `registry-probe-02` 为 2179/0；完整当前控制器/模块的 `client-modules-02` 为 1920/0，覆盖 14 个 ID、112 个输入/预测组合、全部槽位、迟到事件与回合/地图生命周期。实际生产字节全部取 full02 清单，GL/HUD 外部接口为受控测试边界。24 对象和 620 个 include 身份已核，不能据此声称实际伤害、画面、下载或多人通过。入口为 `client_modules.py`，详见领域报告后续核验。
