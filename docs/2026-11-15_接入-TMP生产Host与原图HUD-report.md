# TMP 生产 Host 与原图 HUD

2026-11-15。TMP／MP9 已接入实际服务端 Host、主武器槽购买身份和客户端 HUD／准入。完整 DLL 构建、配套部署与真实游戏仍待；现有服务器配置保持四枪 v1，没有提前开放 TMP。完整 CoF Goal 不变，见 [领域目标](COF_GOAL.md)。

## Evidence → Finding → Path

分析仅使用工作区副本。原 MP hl.dll：SHA-256 0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63；原 client.dll：d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00。二者均 x86、映像基址0x10000000，以下地址是VA。证据位于 analysis/cof-weapons-20261111/tmp-integration-20261115，保持私有。

| Evidence | Finding（高可信、已接源码） | 实现路径 |
| --- | --- | --- |
| E-01：host-raw-ida.json、host-byte-reviewed.json；本轮2080个原IDA字节与PE核同 | F-01：Primary 0x1007F1A0..0x1007F8C4 的壳→独立 KillWpnPart BYTE1→Particle、原14开火事件、射击状态1和原自动音效顺序；0x1007F83C写玩家字段0x2298为1 | native/csol/server/weapons/guest_cof_effects.cpp/.hpp |
| E-02：host-effects-before.json、既有owner-before／map-runtime-before／factory-before；Deploy 0x1007E17B..0x1007E1B7 | F-02：TMP首拔使用独立玩家条目；合作规则备弹授予上限为clip×3=90，原元数据30/300独立。宿主使用独立连接账本、typed backend和主武器槽 | guest_cof_host.cpp、weapon_service.cpp、weapon_registry.cpp |
| E-03：hud-vidinit-current.json；1105字节SHA 40a3eda638b7064cc52fce1c963c218c13ad44a05c9b7b5aa37f7a441b95fa33 与PE核同，222条IDA指令 | F-03：VidInit 0x1001BB40..0x1001BF91，0x1001BDBD取cof_tmp_mag_icon，经0x1006CE40查索引，0x1001BDDC写+F4；Draw 0x1001925D原14分支消费+F4 | presentation_resources.hpp、Prepare-CryOfFearPresentation.py、guest_cof_hud.cpp |
| E-04：hud-presentation-before.json、hud-admission-before.json、production-host-before.json | F-04：原weapon_tmp库存元数据指向mp9卡片；原32px弹匣图单帧。本轮宿主卡片／弹药图离线导出、通用预缓存与严格准入 | guest_cof_resources.cpp/.hpp、guest_cof_admission.cpp、native/csol/buy_menu.cpp |

P-01（callflow）：显式v2 TMP伤害项 → 可选TMP map runtime／cold factory → 实际消息、声音与HUD准备 → 原生注册 → 连接能力16和原生玩家／模型代际 → 主武器槽装备／重绑 → 原武器事件、独立首拔账本及枪烟控制。断线／死亡／换图沿用真实旧实体释放后回收，未改成ServerDeactivate即释放。

早期kill-local-before.json里的控制消息新opcode提案已撤回；本轮使用已经完成并验证的CoFParticle v1独立KillLocal／MSG_ONE_UNRELIABLE，不重置Deploy前移、声音或资源。原版第一次查库未分析代码段曾跳入错误反汇编，随后显式恢复VidInit正确边界再取222指令；没有按错误结果改生产逻辑。

## 源码与宿主差异

- TMP效果支持本地粒子清理、原14世界事件／烟、状态1、动画0..15及独立首拔；自动枪声仍由已验证的原生控制器负责。资源代际、edict serial、所有权和消息结束取消检查保持；没有以强制成功填缺消费者。
- 生产Host仅在显式TMP配置存在时准备其完整资源、工厂、消息能力及HUD；玩家保留TMP独立首拔状态，模型变化／重认证扫描原生主武器槽，保留原装填和延迟推击任务。实际购买验证原类、宿主ID900、类名、已装备及Applied状态；自动枪不被放入手枪槽。
- 通用Firearm冷HUD API保留旧Pistol入口的四枪限制。客户端必须具备实际音频／枪口／粒子／机瞄／输入／鼠标／准星／左右手／HUD／购买图后才发布第16位；缺HUD或枪口会撤回。
- 商店“客串武器·MP9”，冲锋枪分类3、价格2000；弹药30发／30价格为用户授权宿主平衡，非CoF原经济。当前部署配置仍v1；尚未写入规划弹伤55／推击120的新v2配置。
- 原卡片gfx/vgui/weapons/mp9/640_mp9_slot.tga完整SHA f84f2d152c3992e10dc28d7836df837c4a0ab344ea53524c63b33a5266d62055；原cof_tmp_mag_icon.spr SHA 227060ee05aab117e57e94bb14c72235e19c1852c9942e195ca7b5d5c0a9613b。作者生成8项小候选，保留旧四枪导出字节。另补VP70独立标准弹匣模块；五枪仍只注册一次公共准星绘制，未借原CS预测或准星。

## 分层验证

| 检查 | 结果与build回执 |
| --- | --- |
| 实际服务端效果／消息消费者 | 1337 SDK零失败：cof-tmp-host-effects-sdk-20261115-01 |
| 原图作者及旧四枪输出对照 | 15290检查／8资源通过：cof-tmp-presentation-test-20261115-03 |
| 五枪原生资源／HUD冷准备 | 3548 SDK零失败（TMP653）；丢失／取消／generic拒绝路径过：cof-tmp-presentation-cold-sdk-20261115-01 |
| 实际CSO解析器／第五枪准入／输入 | 10446 SDK零失败：cof-tmp-complete-admission-sdk-20261115-01 |
| 实际HUD绘制／模块注册／纯目录 | 实际HUD10660、纯386零失败；其中基础HUD9118为包含项，不重复相加：cof-tmp-hud-modules-sdk-20261115-02 |
| 实际地图／五枪runtime与生产Host编译 | 845 SDK零失败、Host编译0：cof-tmp-production-host-20261115-02；包含仅TMP可用时实际PlayerAuthority、失权不修改输出、1000备弹装填、角色重绑／延迟推击和死亡清理 |
| 生产购买／弹药注册编译 | 两个真实单元、674依赖稳定：cof-tmp-purchase-compile-20261115-02 |

HUD夹具实际上传原TMP32×32图后逐字节核对RGBA；30／75／511／65535／INT_MAX弹匣和500备弹保持完整数字。100次TMP重绘零new／零读盘，旧左轮动态六帧和单个公共准星保持。这是受控surface的真实代码验证，尚非实际GPU／游戏截图／FPS。

失败均保留：资源作者初始脚本缺工具搜索路径；解析01把固定数组误当空容器；Host01第二个注册循环漏改为Firearms导致编译拒绝；HUD01用只有TMP新增资源的目录运行左轮全素材分支，夹具抛异常，原始HUD9118已过。HUD02改为实际消费该目录具备的TMP原图并保留原左轮分支，10660过；未放松生产资源门。购买编译01验证脚本依赖正则转义错误，02修正后真实两单元通过。原失败不改成成功。

## 构建、部署和后续

未构建本轮完整DLL、未更换运行中的B／专服、未宣称原图已进游戏或整枪完成。模型研究对话已部署的GoldCraft 59498c0b／Parallel00affa36及配套目录继续保留，前一报告有实际进程核查；后续CoF更新只按明确计划整合，不覆盖加载改动。

下一步推进整合：购买资源路径升版并生成完整配套闭包、启用显式v2平衡、完整双端构建／依赖封存、隔离冷服及完整HUD预检、原V/P/W和CSO手模／握姿、单B真实购买／机瞄／连射／推击／换弹／切换／丢拾，以及多人／实机性能。旧四枪、人类技能合法扩容和25身份／变体全范围继续保留。

共享文档精确交接：PROJECT_STATE／PROGRESS记本轮Host、购买、原图及准入已接且上述组件已验，但完整DLL／部署／游戏待；GOAL不缩小25项范围；TODO继续TMP整枪配套与手模、余下全部武器／变体、多人／性能。按用户要求无子代理、不发非打包跨聊，领域交接在本文落盘。
