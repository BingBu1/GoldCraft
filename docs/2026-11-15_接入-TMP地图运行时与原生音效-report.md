# TMP 地图运行时与原生音效

TMP已接入可选地图资源、typed工厂、独立canonical owner和事件分发；实际SDK组合通过。当前生产Host没有选择TMP、没有购买/client能力发布或部署新DLL，当前用户已确认可用的四手枪运行版本保持。

## 修改前原证

原MP hl.dll为PE32 x86、VA基址10000000，SHA-256 0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63。原client为d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00。沿用已核同的TMP Deploy1007E130、Primary1007F1A0、Post1007E8A0、Reload1007F8D0、Secondary1007F9E0及共同生命周期函数；本轮再次核样本完整SHA和自动声音七个原窗口。原虚表101BCB7C完整404字节从IDA重新读取并与PE核同，+190确实指向共同EmptyPlay1011E4A0。

私有修改前记录：analysis/cof-weapons-20261111/tmp-integration-20261115/map-runtime-before.json、vtable-native-current.json；自动声音七原窗口在tmp-integration-20261114/automatic-sound-byte-proof.json。实际源布局、SDK和原生声音适配器均先读后改。原采样模块、反汇编和资源不发布。

| Evidence | Finding | Path |
| --- | --- | --- |
| 原TMP动态loop/end、共同EmptyPlay与pickup/ammo/sleeve消费者 | 自动声用既有控制器原事件；其他四类普通WAV走对应原生样本/通道/quiet规则 | native/csol/server/weapons/guest_cof_tmp_native_effects.cpp/.hpp |
| 已核原TMP规则、资源和首拔独立项 | 可选地图冷准备、工厂、独立owner及同生命持久状态均须明确完整 | native/csol/server/weapons/guest_cof_runtime.cpp/.hpp |
| 实际ReGameDLL地图/edict/库存与已测canonical owner契约 | 每次事件检查正确枪身份与生命；角色重绑保存绝对时钟，死亡/退役清任务 | 同上及实际TMPWeapon |
| 原推击共同声音consumer和已准备TMP资源表 | 复用相同逻辑选声，单独绑定TMP命名空间样本 | native/csol/server/weapons/guest_cof_melee.hpp |

## 实现与生命周期

地图只有显式提供TMP prepare/effect和完整效果位才选择TMP。缺消费者、坏mask、缺资源或冷准备失败均结束该地图事务并撤销工厂，不能报告Ready。完整资源准备一次后，native fire/tail ready来自实际预缓存句柄。TMP未加入Pistols目录，也没有伪造客户端支持位。

事件内部使用独立EffectWeapon区分Glock/P345/Revolver/VP70/TMP，不把TMP冒充手枪enum或默认Glock。普通声音和自动声在运行时直接消费，外部图形/消息仍由显式Host提供。FirstDraw先更新TMP独立状态，再向Host递交原事件；重新认证/模型换代不借用其他武器首拔项。地图和所有者上下文在引擎确认实体释放前保留，回调期间不能释放借用上下文。

自动声适配发现并修复一处新引入的资源代际缺陷：临时AutomaticNativeSounds复制generation后，共用发送器看不到原TMPResources在回调中的变化。最终栈上authority桥在调用原权威前后检查原资源generation，再复用既有capture-before-callback的edict/私有数据保护。这个桥不读取玩家/武器，也不改原声音时序。首轮失败证据native-sound-generation-failure.json与build/cof-tmp-native-sound-20261115-01保留。

成本为固定栈事件、常数次当前生命/实体/资源校验与已预缓存句柄；没有新热路径查表、资源I/O或堆分配。每模型准备的独立owner存储与原模式一致，仍有明确地图/绑定预算，不能无限缓存。尚无实际游戏CPU/帧时，不宣称性能通过。

## 分层验证

- build/cof-tmp-native-sound-20261115-02：180原指令声音场景及2918真实SDK检查零失败；普通WAV原顺序/通道/quiet/armor/RNG、自动fire/tail/stop及回调代际退役均覆盖，坏oracle输入拒绝。完整原自动声音控制器的6144场景另有前阶段证明，未用本次180普通WAV场景替代。
- build/cof-tmp-map-runtime-20261115-04：843实际SDK检查零失败，真实生产Host独立编译0错误。TMP实资源来自已验证候选；其余四枪仍执行原生命周期场景。覆盖初次Spawn/库存、TMP准入关闭、首拔、扣弹、Shell→KillLocal→Gunsmoke→FireEvent顺序、一次fire后松开stop/tail、1000备弹物理装填、角色重绑不重置装填时钟、相同生命保留、库存快照恢复、推击think重绑、死亡取消、缺效果/资源工厂撤销和地图实体释放。
- 地图01保留误用TMP不存在DispatchThink的夹具编译失败；02/03分别暴露共享夹具仅列896–899和缺cof_tmp GiveAmmo规则，已按本次宿主900、原90/300规则扩充测试边界，未修改生产准入或弹药上限掩盖。成功04终态与这些失败分开。
- C++20、x86 Arkari Clang、O3/ThinLTO/AVX2、precise FP和禁融合保持，compiler-discovered输入前后稳定，无源码树快照。

组合夹具的图形/消息和引擎边界仍受控；普通单B游戏、实际loop/tail听感、P握姿、CSO手模、多人和真实性能未验。没有完整新DLL或运行部署。

## 接续与连续性

继续实际Host选择/角色库存接线和客户端TMP原类型14、资源/效果/枪口/本地声音、购买/HUD及弹药，完整双端构建后再做配套下载和游戏验收。全部25库存身份及变体范围保持，Goal active。

共享文档精确待应用：PROJECT_STATE／PROGRESS登记本阶段可选地图链、843/2918检查与适配修正；GOAL保持全CoF；TODO继续TMP实际Host/client整枪及多人/性能。领域docs/COF_GOAL.md同步，不发送非打包跨聊消息。
