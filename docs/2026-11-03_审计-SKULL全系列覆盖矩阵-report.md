# SKULL 全系列覆盖矩阵

2026-11-03。当前资源目录中确认 **24 条真实装备武器记录：10 把基础 SKULL、SKULL-T9，以及 13 个周年、赛季和怀旧变体**。另有 SKULL-3 的双持状态实体，不能因它没有独立商城记录而遗漏。龙炎 Balrog-Ⅺ 属另一系列，不计入 SKULL 完成数量。

本次为只读审计：沿用已有原版 IDA 证据，核对 common 的物品表、中文词条、模型和 JSON；补读匹配原 PE 的 SKULL-1 接线参数。未修改生产代码、运行目录或 Git index，未启动图形客户端或专服。父任务正在写公共接线，以下是本检查点证据，不把正在编辑的代码算为已验收。

| 基础成员 | 原内部名 | 原武器 ID / 物品 ID | 现有实现与验证 | 尚需完成 |
|---|---|---|---|---|
| 噬魂 SKULL-1 | skull1 | 130 / 407 | 独立服务端、客户端、资源表；核心 471/0、实际 SDK 客户端组件 599/0；25 资源闭包 | 公共接线、完整 DLL、购买与实机；完整 50AZ 弹道及人物特殊状态 |
| 噬血狂袭 SKULL-2 | skull2 | 312 / 828 | 身份、v/p/w、原 MP 类和事件已定位 | 主副模式完整原证、服务端与客户端实现、测试 |
| 喋血 SKULL-3 | skull3 | 142 / 427 | 身份、v/p/w、CSKULL3/CSKULL3D 与单双持事件已定位 | 转换状态、弹药/冷却、两个实体接线及测试 |
| 死亡咆哮 SKULL-4 | skull4 | 201 / 538 | 身份、v/p/w、原 MP 类与左右事件已定位 | 双枪完整原证、实现及测试 |
| 死亡骑士 SKULL-5 | skull5 | 112 / 367 | 原属性/元数据/12 段方法已保存；现有核心头及伤害 JSON | 核心原指令对照、服务端/客户端 TU、事件与变焦、完整接线及实机 |
| 碎魂骑士 SKULL-6 | skull6 | 216 / 577 | 身份、v/p/w、原 MP 类已定位 | 完整原证、实现及测试 |
| 碎魂者 SKULL-7 | m249ex | 96 / 327 | 独立模块及共享接线候选；既有两枪组件 428/0、资源核验，购买事务验证进行中 | 合并版 DLL、完整弹道/特殊状态、购买与专服/客户端实机 |
| 碎魂利刃 SKULL-8 | skull8 | 205 / 562 | 身份、v/p/w、附属 s_skull8 模型与原 MP 类已定位 | 次攻击等完整原证、实现及测试；不能从 s 模型名推断玩法 |
| 旋风 SKULL-9 | skullaxe | 4010 / 363 | 本项目桥接 ID 1023；两刀完整 DLL、受控专服攻击/玻璃/生命周期 34 检查点，普通怪 TakeDamage/死亡 18/0，33 HTTP 资源核同 | 真正武器 TraceAttack→普通怪、换图后重新发放、真实玩家/图形/多人；原价格 |
| 雷霆 SKULL-11 | skull11 | 152 / 437 | 身份、v/p/w、原 MP 类已定位 | 完整原证、弹种/模式核对、实现及测试 |

这 10 把的 v/p/w 模型均存在。SKULL-9 所列实服数字是两刀共同夹具的累计证据；18 项普通怪阶段直接进入真实 TakeDamage，不能当成完整刀 TraceAttack 链。最后一次 runner 的换图 RCON 超时仍未通过，不能仅据 phase success 把整个测试标成通过。两刀详情见 [近战报告](2026-11-02_还原-青龙偃月刀与旋风-report.md)，枪械实现与旧测试记录于本地两枪领域报告，随该阶段源码另行发布。

| 额外装备 | 原内部名 | 原武器 ID / 物品 ID | 已核资源或派生关系 | 当前覆盖 |
|---|---|---|---|---|
| SKULL-9 周年纪念 | chn9thskull9 | 4053 / 8081 | 物品与中文词条；无同名 v/p/w 或 weapons JSON | 尚未核实原映射及行为 |
| 【阵风】SKULL-T9 | skullt9 | 4072 / 8611 | v/p/ef_skullt9、knife_skullt9 HUD 与多种技能声音 | 尚未实现；必须独立分析技能，不能当普通 SKULL-9 换皮 |
| 冰海战记 SKULL-2 | y24s2skull2 | 581 / 9023 | JSON baseWpnName=skull2，texPrefix=y24s2，useCustomDeploy=true | 尚未接入派生身份/贴图及行为核验 |
| 怀旧·碎魂者 SKULL-7 | m249exZHC | 629 / 9348 | JSON resourceName/wpnClassName=skull7zhc，base/model=m249ex | 尚未接入；物品资源名与 JSON 实体名不同 |
| 怀旧·旋风 SKULL-9 | skullaxeZHC | 4101 / 9353 | 物品与中文词条；无同名模型或 weapons JSON | 尚未核实原映射及行为 |
| 怀旧·死亡骑士 SKULL-5 | skull5ZHC | 634 / 9355 | JSON base/model=skull5，useOriginDeploy=true | 尚未接入派生身份及属性 |
| 怀旧·噬魂 SKULL-1 | skull1ZHC | 638 / 9360 | JSON base/model=skull1，drawSeq=7 | 尚未接入派生身份及属性 |
| 怀旧·喋血 SKULL-3 | skull3ZHC | 644 / 9366 | JSON base/model=skull3，seriesID=645 | 尚未接入；必须包含下述双持实体 |
| 怀旧·雷霆 SKULL-11 | skull11ZHC | 648 / 9370 | JSON base/model=skull11，drawSeq=4 | 尚未接入派生身份及属性 |
| 碎魂者·水墨丹青 | y25s3m249ex | 661 / 9424 | JSON baseWpnName=m249ex，texPrefix=y25s3 | 尚未接入派生身份/贴图及行为核验 |
| 怀旧·死亡咆哮 SKULL-4 | Skull4zhc | 670 / 9471 | JSON base/model=skull4，drawSeq=9，pAnimSeq=dualpistols | 尚未接入派生身份及属性 |
| 怀旧·碎魂骑士 SKULL-6 | skull6zhc | 671 / 9472 | JSON base/model=skull6，drawSeq=4 | 尚未接入派生身份及属性 |
| 怀旧·碎魂利刃 SKULL-8 | skull8zhc | 672 / 9473 | JSON base/model=skull8，pAnimSeq=skull8 | 尚未接入派生身份及属性 |
| 怀旧·旋风 SKULL-9 赤血 | skullaxezhczf | 4124 / 9667 | 物品与中文词条；无同名模型或 weapons JSON | 尚未核实原映射及行为 |

SKULL-T9 当前没有同名 w 模型，这只表示该文件不存在；仍需核原近战掉落契约，不能据此认定漏资源。变体 JSON 的 baseWpnName、modelName 证明模型/基类的引用关系，**不证明伤害、模式或购买规则完全相同**；应继续审查原 MP 派生属性。未列出虚构的 SKULL-10：本次物品、模型、JSON 关联范围没有对应成员证据。

额外的模式实体与非武器分类：

- SKULL-2 使用 skull2AMode、skull2B_left、skull2B_right 事件和 p_skull2dual；不存在 skull2.sc 不表示资源缺失。
- SKULL-3 双持导出为 weapon_skull3d，与单持共用 v_skull3，另有 p_skull3dual。其怀旧版 JSON 明确列出 ID 645、class=skull3dzhc、base=skull3d、model=skull3、pAnimSeq=dualpistols；644 的 seriesID 指向 645。它是状态实体，不新增一个商城物品。
- 当前另有 11 条 SKULL 相关非装备行：喷漆、制作材料、制造资格及周年涂装。它们 ClassName 不为 Equipment 且 InGameID 为 0，不能计入可持有武器。SKULL-T9 的 skull9ex_piece 是材料，也不能代替 4072 武器。

SKULL-1 的接线补证如下。地址只用于匹配的本机原版证据定位；不在产品中硬编码这些地址。

| 合同 | 原证 | 结论 |
|---|---|---|
| 原价 | HW 38312868，ID130、alias skull1、48 字节记录的 +18 | 1200；mode15 的 19 项覆盖没有 ID130，仍为 1200 |
| 价格消费 | HW 379EAB10 → 379EA220 → 379E9A60 → record+18；mode15 分支 379E9B16 | 使用模式选择后的价格记录，不从商城伤害统计或其他枪价格猜值 |
| 准星 | 原 HW 构造 37A7D96D；字段 +3C/+40/+5C/+60；client ID130 分支到 02A23DBA | Parameters{8,3,0,7}，分别为 gap/shotDelta/speed/flags |
| 普通怪 v22C | MP 表项 119539A8 → 106129F0，mov al,1; ret | true，与 SKULL-7 的 false 分开 |
| 普通怪 v230 | MP 表项 119539AC → 105FFBE0，xor al,al; ret | false，不能因 v22C 为 true 而一起置 true |
| 击杀回调 | MP 表项 11953960/11953964 → 10611B90，ret 8 | v1E4/v1E8 均为空回调；只对精确武器身份分派 |
| 弹药显示分类 | client 表项 03110CE4 → 01E370A0，xor eax,eax; ret | v218=0，走普通弹夹；不因 50AZ 名称而改成特殊总弹药 |

证据链与复现：

| Evidence | source_ref 与 SHA-256 | Finding / Path |
|---|---|---|
| E-01 | 本地 `series-audit-20261103-02/series-catalog.json`，7040bc65bc852a6a83da27cb31d3738a065ac19ccddfe3fc10af8f2ca4be0148 | F-01（n/a_re，身份已核，高可信）：24 装备/11 非武器。P-01：item.csv 分类/ID→中文词条→实际模型及 JSON 关系 |
| E-02 | 同目录 `skull1-integration-contract.json`，eb6b297c8c8bc75625346c2baf16239441c8616d18077eec63bf256f6ec18808 | F-02（n/a_re，参数已核，高可信）：上述 SKULL-1 接线参数。P-02：原表/构造→原槽位/消费指令→现有公共接线合同 |
| E-03 | `build/csol-skull-balrog-20261102/skull1-handoff-01.json`、core01/client03 result 与日志 | F-03（n/a_re，独立组件已核，高可信）：前 9 个源码 SHA 与已测版本仍一致，471/0、599/0 可沿用；不是公共接线或实机验收 |
| E-04 | `build/csol-melee-20261102/live-handoff-01/manifest.json`，1a60aa4a84b018017173644f04ebaa0dec1f3bd6b3841c0a5630cc3b1912254d | F-04（n/a_re，分阶段实服证据）：34 检查点、18/0 普通怪和 33 HTTP；保留失败与换图未通过边界 |

E-01/E-02 目录前缀为 `analysis/csol-model-20261017/skull-balrog11-20261102/`。原版 DLL、完整原数据及反汇编仅留本机，不随源码公开。模块 SHA：MP 39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2；client f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd；HW c7bddfda93c2e0877ea126a15a318b83cce74c59bb95e38a7301b08033ac7783。审计后均保持。

本机复现命令如下，输出目录必须不存在；依赖本机已授权的 common、原版副本、Python/Unicorn。该脚本不会写入 common、原 DLL、运行目录或现有输出。

```powershell
python -X utf8 analysis/csol-model-20261017/skull-balrog11-20261102/audit_series.py analysis/csol-model-20261017/skull-balrog11-20261102/series-audit-new
```

本次时间线：先复核已保存的 SKULL-1/SKULL-5 结果和源码；再重新执行原 HW 元数据构造并读价格/vtable/准星分支；最后扫描当前 common 的 24 条装备和 11 份变体 JSON。第一次目录的中文名只用 Name 关联，四条名称空缺；第二次改为原 recourcename 优先后补齐，武器集合和 SKULL-1 参数不变。未用新建 IDA 数据库冒充新的反编译分析。

下一步：父任务按上述合同完成 SKULL-1 公共接线，并完成 SKULL-5 两端实现；SKULL-7/龙炎继续合并版购买和资源验证。随后按矩阵逐枪补主副模式、伤害、预测、资源闭包、买弹/丢弃与灾变击杀路径；T9 和所有真实变体持续列为未完成，不能用已有 7/9 两把或 Balrog11 宣布整个系列交付。该报告交父任务写入共享连续性文件，未直接改写其他域状态。
