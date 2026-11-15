# CoF体力周期与耗尽提示

新增独立CoF体力周期组件，保留原周期恢复／消耗、耗尽分支、语音和HUD参数顺序，以及出生时的体力初始化。4656原指令场景对照通过；真实地图owner、M16接线与效果传输尚未完成，没有部署或操作用户B。完整CoF25身份／变体Goal继续。

## 证据→行为→实现

原MP hl.dll为x86／base10000000，SHA0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63。原client.dll为x86／base10000000，SHAd2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00。均使用工作区副本，受保护安装未写入。下列地址为VA。

修改前PREEDIT-stamina-cycle.json，SHA8f0ae580c69ef18c9eb10d31ddec1f0d13fb774b9fdf501c75fbf8b93dbbe750，绑定10个原字节窗口及完整100F4D70／Spawn100F4090和调用者IDA归档。私证位于analysis/cof-weapons-20261111/m16-integration-20261115。原函数690条指令的全周期／移动上下文已读；本组件只实现其中已明确隔离的资源周期，没有把它扩大为完整CoF移动已还原。

| Evidence | Finding | Path |
|---|---|---|
| E1：100F539B..100F56F5完整858B，PostThink100EE748调用完整100F4D70 | F1，高置信／原指令验证：严格now大于周期时钟；消耗1或2，恢复站1蹲2；消耗间隔double0.05／0.1、恢复double0.75／0.25后转float，无补帧追赶；无限仅跳过扣费，不跳过周期安排 | native/guest/cof/stamina_cycle.cpp::AdvanceStaminaCycle |
| E2：同窗100F548E..5657 | F2，高置信／原指令验证：精确零值且周期过期才种0.1、动作截止now+4，按水位／语音截止与rules／variant选择原喘气声；声音后读取当前时钟安排30秒语音门、5／10秒恢复等待及5秒音频门；先按当前值清耗尽，再夹0..100。负值本帧末尾夹0，下次周期才进入零耗尽 | 同上；StaminaCycleState／StaminaCycleOwner |
| E3：100F5657..56F5、100F468D..4724完整151B | F3，高置信／原指令验证：变化才发HUD；Begin后读当前体力转int，Byte后读当前pulse，End后清当前pulse并缓存当前体力。Spawn写100与两个当前时钟、无条件发送，验证段不清旧cache／pulse | SpawnStaminaCycle及HUD消费者 |
| E4：client10038900登记Stamina→1003A360完整122B，100808B0无符号byte，10080910→10080A10及double常量1013EE58=0.125；实际SDK pfnWriteCoord槽 | F4，高置信／观察：提示时长经有符号16位坐标／8解码，正值才安排客户端提示截止。写入参数是float，不代表线上float格式；未发布的初稿Float操作名已改为Coord | stamina_cycle.hpp::StaminaHudKind；实际包编码／HUD消费者继续待接 |

P1（callflow）：PostThink移动决策之后→周期扣费／恢复→必要的耗尽语音→读取声音回调后的时间→耗尽标志与末尾clamp→变化HUD→读取各消息回调后的当前值。调用者仍负责原移动前缀、FOV／水环境尾部以及同一生命／地图认证，不能放到绘制或武器切换时更新。

P2（lifecycle）：出生标量初始化发送100→同一玩家状态跨切枪与模型保留→真正玩家生命周期退休。组件不自行绑定或猜测退休策略，地图owner接线完成前不准入M16。

## 源码与边界

新增native/guest/cof/stamina_cycle.hpp／.cpp，独立StaminaCycleState保存原8个相关float；alternateDrain对应+3BC，alteredCycle对应+1EE0，尚不冒称其未闭合的上游玩法身份。flags必须使用CoF语义的独立状态，不能把CS的同位免疫标志当奔跑或耗尽。没有引入CSOL绝命档案的恢复规则或阶段修正。

四个HUD操作和喘气事件必须由真实准备好的消费者提供。无猜测音效、布局、默认计时或强制成功；取消／缺消费者会停止当前操作，既有前缀副作用保留。原cvttss2si的越界结果安全实现，消息回调即使改变体力也不触发C++未定义转换。

native/server/GoldCraft.targets加入单独编译项，使用C++20／O3／ThinLTO／AVX2／precise与ffp-contract=off。组件无堆容器、线程、资源加载或同步I/O，10000调用零new通过；没有实机CPU／分配／帧时采样，不作完整性能结论。

## 分层验证

- 最终原指令夹具使用匹配MP的完整858B周期和151B出生子段，4656场景／122373检查零差异，280依赖前后稳定。覆盖浮点边界、全周期分支、声源／随机选择、回调改变时间／体力／模式／flags／pulse，14个取消／拒绝边界及无效输入。空／截断／额外／错误答案四负例均拒绝。
- 初轮01／02是测试执行器未为Spawn子段设置EDI所有者，已用IDA核对修正；03首次全通过保留。Coord命名补充取证先于更名，初稿未部署／未发布；最终04原指令重核已通过，回执build/cof-stamina-cycle-20261115-04/receipt.json，4656场景／122373检查及四种坏夹具均过，280输入稳定。音效包装器和网络传输均为明确ABI边界。
- 完整服务端第一轮01通过：3117源／467链接输入稳定、零错误，MP5129a4a6931eb3937ca4194aa0f81046003c9162621338f45903b62c37edf6c8；独立DLL／PDB保留在build/cof-stamina-cycle-whole-server-20261115-01/frozen。更名后的最终02也已完整通过，3117源／467链接输入稳定、零错误；MP24d7c70eaa82a1d53ada14dce54a560636b3b0237721423e3d04fa7705ff3344，PDB5b6e358a5b2135fb809467c7f7b739b6c1d83c63efe132fb7c5ee69311e0981d。最终独立产物在build/cof-stamina-cycle-whole-server-20261115-02/frozen；构建保留当前其他领域工作树，不表示其全部变动本轮发布。
- 没有新增地图host、冷服、购买开放、部署、UI／多人或游戏性能验收。用户“我自行测试”仍有效，当前B／专服保持。

复现入口见[原指令夹具](../tests/cryoffear-stamina-cycle/README.md)。客户端旧会话已实际失联，本轮只打开已有工作区client副本的新会话cof-stamina-client-20261115，未重启其他IDA worker。handler原先未定义函数，已依HookUserMsg入口和实际retn界限建立122B函数；没有改二进制。

## 下一步与共享文档pending

已定位10019DA9的单人skillset命令，参数4写+1EE0为1、其他参数清0；10066540武器分支会写owner+3BC。该字段候选通过Capstone定位后用IDA验证，未把不同对象的同位字段混为玩家状态。继续保存／恢复及完整生产者与真实CoF玩家状态、生命周期；接玩家Think入口和推搡目标／原伤害语义；补客户端Stamina完整原布局与生命周期、M16完整common／HUD／购买及整枪。以上尚未完成，25身份／变体范围不缩小。

按用户非打包不跨聊通信，本轮共享连续性更新精确pending：PROJECT_STATE记录CoF独立周期4656原场景及最终构建身份、无部署；docs/PROGRESS链接本报告与最终回执；docs/GOAL保留全部CoF范围；TODO继续真实owner／体力生产者／推搡／HUD购买与玩法。领域COF_GOAL直接维护，不覆盖协调者文件。
