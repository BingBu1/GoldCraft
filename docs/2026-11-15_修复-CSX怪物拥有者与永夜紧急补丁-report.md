# CSX 怪物拥有者崩溃与永夜紧急补丁

本轮关闭转储中已定位的 CSX 越界，补齐已证实漏发的客串服务端配置，并完成资源修复的八路协程、后台落盘和加载保活。**交付后用户仍报告客串拒购；补配置不能代表该问题已关闭。** 正式服务器没有被操作。满体力前几次连跳晚一拍尚未定位，不能宣称已修复。

## 范围、证据与结论

用户提供崩溃转储并要求修补后打包。授权范围、网络边界和未知项记录在私有 `analysis/resource-admission-hotfix-20261115/scope.json`；主代理独自处理，没有启动子代理。项目命令直接通过本机 Git Bash 执行，受保护 Steam 安装保持只读。

| 证据 | 已证结论 | 源码／交付路径 |
| --- | --- | --- |
| E1：用户转储 SHA-256 `4931f9d87503d8be32a27d6ef0869eaf9d70b170d2979434a21481330ccfc1fc`；WinDbg `.ecxr; kv` 及模块身份 | F1／high／validated：怪物技能实体创建 HE 模型，CSX 把拥有者索引54用于33项玩家表，越界后访问无效玩家实体。 | `patches/amxmodx-csx-npc-owner.patch`；上游 `modules/cstrike/csx/meta_api.cpp::SetModel_Post` |
| E2：实际 CSX x86 DLL SHA `ec7759916d279099e2c4a63fa691bfff05124ff79616641c5d5d1193352a1fc3` 的 IDA；VA基址10000000，入口100041B0，调用1000423C，故障RVA382D，玩家表RVA2C7B8、步长31D0 | 在形成玩家指针前同时检查1..maxClients与数组容量；正常玩家路径继续使用原统计和回调。宿主兼容修复，不改变 CSOL 怪物技能。 | `tools/Build-CSX.py`；`tests/csx-npc-owner` |
| E3：已发永夜包清单及真实 `CSOL_PrecacheGuestCof` 消费者 | F2／high／validated：包内缺少 `configs/guest/cof/scenario_damage.json`。该表缺失时，服务器在登记客串武器资源前返回，整组购买准入关闭。 | `tools/nightfall-update/package.py` 增加必需服务端引导配置；安装器只准入该精确配置路径；补丁携带已验证274字节表。 |
| E4：主线程栈／CPU／I/O采样，`build/resource-repair-performance-20261115-01`；实际生产下载队列与缓存消费者 | F3／performance／validated：原修复队列最多2个文件；主线程执行缓存二次SHA和同步刷新。采样6695项中315项含缓存Store路径；这是主线程墙钟栈证据，不能把所有等待当CPU热点或FPS收益。 | `resource_download.cpp/.hpp` 与 `resource_update.cpp`：最多8路、总128MiB缓冲，已有Asio阻塞任务池执行完整校验／原子落盘，主线程只提交和挂载。 |
| E5：精确宿主 hw SHA `9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295` 的 IDA；基址10000000；帧101D43D0、收包101A4D50、发送101A3A40，状态3分支101A3A72..101A3AA2，调用Netchan_Transmit101DCC00 | F4／host_adaptation／implemented：资源修复暂停准入时，在当前连接与主线程内每秒调用一次原生状态3发送分支；不伪造玩家输入、不修改服务器超时值。 | `resource_bridge.cpp`；`resource_sync_10210.json` 及配套目录。实际公网长下载仍待验证。 |

E1/E2在 `analysis/server-crash-20261115-173410`，含样本身份、完整入口证据、修改前记录和原转储；E5及本轮修改前源SHA在 `analysis/resource-admission-hotfix-20261115`。原DLL、反汇编正文、转储和签名私钥均不发布。

P1／callflow：普通怪技能 → `CCSOLMonsterSkillEntity::Spawn` → 引擎SetModel → CSX `SetModel_Post` → 玩家数组 → 手雷统计。修复在数组形成前拒绝非玩家拥有者；不是伪造拥有者或吞掉怪物技能。

P2／callflow：客户端下载清单 → 完整VFS首检 → Asio并发HTTP → 后台SHA／独立临时文件／刷新／原子发布 → 主线程挂载 → 再次全量VFS解析 → 恢复原生准入。旧文件、PAK优先级、SHA、取消和世代事务保持。

## 构建和自动化验证

- CSX：`build/csx-npc-owner-20261115-05/receipt.json`，实际六个单元与DLL成功、12项检查通过、实际包含输入稳定。新DLL SHA `bd592547434fd96b31226b422a7bea354ab64b1414abb2bce07cc171a880fa10`。早期混用CSSDK和缺资源编译器的失败保留；最终使用匹配完整HLSDK，未改生产门规避构建错误。
- 资源集成：`build/resource-hotfix-vfs-20261115-02`，20项实际x86文件系统／HTTP用例通过，覆盖错误SHA、PAK、硬链接、取消、新世代、旧缓存、最终全量同名覆盖。第一轮并发峰值7使严格8路检查失败；修正测试服务端屏障后，`build/resource-hotfix-parallel-20261115-03` 的16文件实际HTTP峰值8、取消恢复及缓存重试通过，没有改下载生产代码放松检查。
- 完整GoldCraft：`build/resource-hotfix-full-20261115-01`，输入稳定，DLL SHA `56ec0e7af59d48c6d8a9baf0ad1afa1cb02e86fc49776ef23979c19596837d28`；17组bridge和1组预缓存共18组回归通过。后台缓存时间以完成任务发布，不冒充主线程占用。
- ParallelDownload完整构建稳定；其字节与已发包相同，因此按SHA过滤。资源进度接口在本轮GoldCraft内保留。真实加载界面的计数画面尚未重新验收。
- 匹配宿主目录生成160符号／138校验窗，312项完整权威HUD下载闭包通过，未覆盖故意损坏的客户端根文件。目录和DLL通过受管发布同批更新。

## 补丁与验收边界

交付为 `server-packages/nightfall-hotfix-20261115-final/GoldCraft-Server-Update-20261115-Nightfall-Hotfix1.zip`，适用已安装20261115-Nightfall的正式服务器。默认路径仍为 `E:\cs_MineCraft\GoldCraft-Servers`。停服后运行包内Update-Server.cmd，成功后使用原Start.cmd；玩家使用原受管启动器更新DLL和符号目录，游戏资源继续自动修复。

包仅4项变化，省略并校验5495项基线：新CSX、缺失客串表、新GoldCraft内容对象、完整签名客户端发布。保留原MP与CSOLSupport，未塞入正在开发的疾跑或新枪。安装器不重置服主现有配置，支持幂等和完整回滚；实际Windows安装测试结果以 `build/nightfall-hotfix-install-20261115-02/receipt.json` 为准。第一轮测试缺少假的hlds标记，预检正确拒绝，未改生产预检；已补夹具继续核验。

客户端签名使用既有本地密钥和OpenSSL SHA-256/RSA，签名已用既有启动器公钥验证；私钥留在本地非发布目录。ZIP每一成员与暂存原文件逐SHA核同。远端未部署，公网慢链路、自然怪物施法、真人手雷和多人仍需实机验收。

## 连跳剩余事项及撤回记录

用户两次确认：满体力、前几次连跳就晚一拍。因此不能归因于恢复到15体力后等待更新。早期把原客户端空中分支当直接返回的假设不成立：实际b434客户端1005FC8C跳转1005FB28，后者会设置oldbuttons。候选与相关测试已撤回，未部署、未进入本包；证据保留于 `WITHDRAWN-air-latch.md`。本包不包含虚构的连跳修复。

还需在可复现的真实客户端采集按键→usercmd→HUD_PlayerMove→落地追踪→Refdef链，区分输入、预测与画面平滑。已有禁止不足费用起跳规则保持。本轮没有声称完整疾跑、完整CSOL或项目Goal完成。

共享文档建议：状态登记本轮三项修复与补丁，TODO只保留满体力连跳、正式服长下载／自然施法及多人验收。提交／GitHub与打包协调回执在最终发布阶段追加，不发送无关跨聊消息。

## 最终安装回执

`build/nightfall-hotfix-install-20261115-02/receipt.json` 已完成且success=true，13项检查通过：实际WinPS预检、缺失基线拒绝且零修改、真实四文件安装、服主配置逐字节保留、受管序号2026111502、重复执行幂等、恢复旧CSX与完整基线、删除新增客串表。测试未运行正式服，正式服不需启动才能应用此包。

最终ZIP1944848字节，SHA-256 `eeafd3c1ab3636311a66c0f98b476b5127403ee04761188fd3ea105ed29ffe71`。已仅就打包通知主调度，明确远端未部署及连跳仍开放；当时共享索引有其他提交，尚未写入本轮索引。完整Goal保持active。

## 交付后的拒购与连跳反馈

用户报告两项依旧存在。只读核对实际 Steam 客户端，GoldCraft 确为本包 `56ec0e7a…`，CSOLSupport 为配套 `2aac5985…`，不能归咎于客户端未更新。内存 CodeView／PDB 对应且读取前后身份稳定：当前连接代际6、原生状态5／signon2；完整扩展声音表512槽有428个非空项，其中 `guest/cof/` 为零。客串冷准备状态为 `NoAssets`，样本数／武器位均零，`cof_protocol`／`cof_weapons` 均零。证据在 `build/guest-purchase-live-20261115-04/receipt.json`。这是服务器本次声音登记缺少客串的现场证据，尚未证明远端启动中到底在哪个分支退出，仍需正式服启动日志与服务端补丁应用结果；没有强制开放能力。

真实原生 x86 客户端 SHA `b434b1c0…` 的 IDA 已核公开 PM 入口 `10047660→100602C0`、初始化 `10047670→1005F6B0` 和实际跳跃 `1005FB10`。新增 `tests/native-jump-prediction` 直接加载未修改的工作区样本，在公开 PM 回调提供的平地上执行700条命令：727项检查通过、12次起跳均在同一命令移动，体力足够的适配门不修改命令。回执 `build/native-jump-probe-20261115-03/receipt.json`；这不替代真实按键／网络／画面验收。

同一故障客户端的617项原生cvar链只读取白名单：`cl_lw=1`、`cl_lc=1`、cmdrate/updaterate101、fps_max99.5。原宿主 `9ba9a2db…` 的 IDA `Cvar_FindVar101BDEB0`／全局104B74CC提供链身份。30秒只读PM采样5999项、187项跨写入窗口明确标记、采样CPU0.0625秒；实际未出现跳跃，不能据此判定卡顿原因。记录在 `build/native-jump-live-sample-20261115-01`。新增观察工具只查询／读内存，无游戏命令、键鼠、断点、线程暂停或受保护安装写入；连跳和客串购买均保持待验。

后续用户主动复现已采到25秒9999项／11次上升，以及60秒23999项的余额、PM和Refdef联合快照。后者包含15297项满100余额，全部仍带精确 `cso_jdeny=1`；100→85→70→55→40→25的真实扣费均为15。存在落地、跳跃指令、oldbuttons已锁存却不离地的片段，之后才回传上升，Refdef跟随平滑。快照不是逐指令插桩，不能把采样差值冒充精确按键延迟；但足以证明“仅因体力耗尽”不成立。证据在 `build/native-jump-live-hud-20261115-02`。此前一次临时内联Python检查用了错误的字节转义，产生的 `after-stamina.json` 不作标记已清除的依据；正式观察器正确匹配反斜杠及精确值。

已在匹配原宿主继续核对实际消费者：`CL_ParseClientData1019EB50` 在1019EF4E..1019EF71把网络clientdata的physinfo复制到引擎全局1140E8B8；`CL_RunUsercmd101AC460` 在101AC844..101AC88B再复制到公开PM结构4F3F0偏移，最后补零。不是依据SDK猜测引擎布局。未改该原生路径或用HUD余额强制开放预测，生产服务端的错误来源仍需闭合。

用户进一步核对正式服磁盘：MP完整SHA确为 `60ba4f3f…`，JSON为正确v2五枪表。不能再声称这些文件尚未更新。使用同一旧MP／PDB、CSOLSupport2aac、已发包HUD和本次表的独立32槽灾变冷服，真实ItemInfo896–900及15／8／5／18／30弹匣全部登记，148项真实HTTP／SHA及服务端配置404负例通过，全部临时文件恢复；回执 `build/hotfix1-guest-cold-20261115-04/receipt.json`。早期夹具混入MP5新卡片、漏原版MP5图标的失败保留，未改生产资源门规避。现需对比正式服**运行模块身份**和 `[CoF]` 初始化日志，不能把磁盘同SHA当运行同版本。

`tools/nightfall-hotfix/Inspect-Runtime.ps1` 提供默认服务器路径的只读诊断，不停服、不写服务器文件。使用0x410查询／读内存句柄，检查真实加载模块的RSDS身份；正确60ba的身份为 `52534453baf25f6c749b443c4c4c44205044422e01000000`。本地实际HLDS验证记录 `build/nightfall-runtime-audit-local-20261115-01.json`，已正确区分当前不同的运行MP；最初.NET模块枚举未返回WOW64模块，改用实际PSAPI枚举后通过，失败不冒充匹配。正式服该回执仍待用户返回。客户端 `csol_info` 无输出，已明确撤回把该命令当完整客串诊断入口的错误建议。

用户后来回传正式服加载身份，确认MP60ba／引擎162ad016与磁盘、v2表均匹配，已排除运行旧DLL。进一步清单审计发现两包还漏六份库存／枪烟脚本；精确旧MP缺件冷服已复现，补齐后五枪148HTTP通过，独立六脚本安装／幂等／配置保留／回滚13项完成。该资源补丁与连跳剩余状态见[后续漏包修复报告](2026-11-15_修复-客串脚本漏包与满体力预测诊断-report.md)。原Hotfix1 ZIP不重写，连跳仍未修。
