# 自动武器原版枪声状态与 SDK 发送

TMP／MP5原版合作模式的枪声开始、持续、停止及收尾音控制器已实现，并接通独立的真实SDK发送消费者。原版6144场景逐状态／回调参数一致，最终SDK59907检查通过。TMP／MP5整枪尚未接入；无完整DLL部署、真实播放、多人或性能验收。VP70更新窗口仍待人类确认，共享B／主服没有被本阶段中断。

## 范围与样本

范围继承[完整CoF Goal](COF_GOAL.md)及私有 `tmp-integration-20261114/scope.md`。原 `hl.dll` 为PE32／x86、VA基址10000000，SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`。本轮重新建立独立IDA工作区会话，先survey核身份，再读取原函数、完整反汇编、调用者／被调用者及字节；没有修改原安装或发布原二进制／反编译正文。

## Evidence → Finding → Path

| Evidence | source_ref／复现入口 | content_hash |
| --- | --- | --- |
| E-01，修改前原消费链 | 私有 `analysis/cof-weapons-20261111/tmp-integration-20261114/automatic-sound-before.json`，同级fresh／字节／三段IDA扫描；完整原样本及2725个实际IDA字节与PE核同 | `ef0e1e60bb42a6a7d64e28b23c55cd76b5a342658d3eaedb72693be4e25f34c0` |
| E-02，原代码对照 | `build/cof-automatic-sound-20261114-02/receipt.json`；README内控制器命令，原窗口／stop／quiet指令执行，最终原生EmitSound为观察边界 | `a84490d97f34492864fe8b391d68254bb1e6c15ef712952a941fc892a1973242` |
| E-03，实际SDK消费者 | `build/cof-automatic-native-sound-20261114-03/receipt.json`；README内SDK命令，606依赖前后稳定、5种坏对照均拒绝 | `81d3df20b72adc2f54872d3d03ba0e482a0554119bdc0599de9442f23305da72` |

F-01（severity=n/a_re，evidence_ids=E-01/E-02/E-03，confidence=high，status=validated，仅控制器及SDK边界）：原TMP Primary VA1007F788..1007F82E、MP5 Primary1005BA23..1005BAC9只在weapon+1A5未设置、fire引用非零时开始枪声；先设置状态，再调用声音。普通基类PostFrame1011D6BA..1011D81B仅在人物存活、clip>0、非装填、攻击仍按住且action0时保留状态，否则先清状态，再按原引用是否存在停止fire、播放tail。不是每一发重复启动整段声音。

F-02（severity=n/a_re，evidence_ids=E-01/E-02/E-03，confidence=high，status=validated）：原stop VA1001E990调用100FC9F0，CHAN_WEAPON1、volume0、attenuation0、flags32、pitch100；开始及收尾是1／.8／0／100。普通WAV quiet标记10226489使音量按原double*.25再回float，原morphine指针例外不属于这两把枪的固定WAV。控制器输出最终音量，SDK边界验证该值且不二次缩放。

F-03（severity=n/a_re，evidence_ids=E-01/E-03，confidence=high，status=validated，宿主适配）：初次SDK01在首次权威回调改变实体序号时发现新适配器会接受变更后的身份。修正为在回调前捕获实体／序号，再在每次可能读取原生实体前先验证权威；回调改变serial／private data／owner／资源代际或释放原edict页都返回Cancelled，不访问失效实体。身份保护属于宿主生命周期适配，不宣称原版存在本项目能力协议。

location：`native/guest/cof/automatic_sound.hpp/.cpp`、`native/csol/server/weapons/guest_cof_automatic_native_sound.hpp/.cpp`。`native/server/GoldCraft.targets`增加两独立TU；不把自动武器写入全部武器include，不新增逐枪cvar。完整武器工厂／调用者尚未安装，编译入口不等于整枪运行入口。

P-01（path_type=callflow）：E-01匹配原模块 → TMP1007E8A0末尾1007F03E实际进入1011D560 → 玩家vtable101A4DA8+BC解析到10013560（deadflag0且health>0）→ 原普通声音分支 → 原1001E990／100FC9F0 → typed控制器 → 实际SDK EmitSound → E-02/E-03比对状态、事件顺序及参数。

## 精确实现边界

原完整IDA可执行区三段扫描共发现8处直接byte+1A5引用：TMP／MP5两个Primary设置者，以及基类普通／dual两个清理分支。该扫描只证明这些直接引用，不冒称已覆盖任意指针别名、批量清零或所有完整武器生命周期。原singleplayer普通停止消息使用tail引用，而dual分支使用fire，差异已保留在私有证据；本阶段按用户明确的原版合作基准只实现native multiplayer路径，其他规则返回UnsupportedRules，没有套用或猜补消息逻辑。

控制器以单个bool保存原声音状态，调用者提供真实鉴权帧、冷准备的fire／tail存在性和quiet值。开始先改状态；结束先清状态；回调拒绝时保留当时已达到的状态并报Cancelled，不重试或强制成功。SDK资源只持有固定两项原生句柄／不可变路径及代际，必须在受保留的地图内活到同步回调结束。调用者须在权威线程持有有效原生对象；不从客户端传入进程指针或伪造prepared状态。

## 分层验证与失败记录

- 控制器最终02：6144个原TMP／MP5开始／普通后帧场景、182个原指令地址，全部状态及native回调参数一致；218宿主保护检查通过。包含clip0／1／30／75、动作0／1／2／5、装填／死亡／松开攻击、quiet、缺fire／tail和连续100次保持状态不重复开始。
- SDK最终03：同样6144个原场景通过完整生产控制器和实际SDK消费者，共59907检查、606实际编译依赖／控制输入稳定。包含实体／序号／owner／pev／私有数据／资源代际的前后回调撤销、缺引擎／资源、非法参数和真实VirtualFree后的无再读路径。原生engine音频回调是受控捕获，没有实际播放。
- SDK01编译及普通场景通过，但身份变更负例失败，作为真实新适配器缺陷保留。SDK02修正首次身份捕获后通过；随后源码审查将所有后续实体读取置于权威检查之后，并增加真实内存页退休负例，最终03通过。没有把较早候选当作最终证据或部署。
- 空／截短／额外／畸形／错误原对照5项均拒绝，避免夹具在缺少原结果时假通过。编译为x86／C++20／O3／ThinLTO／AVX2、precise且关闭浮点收缩，临时文件在独立build/temp；没有复制源码树。

复现步骤见 [自动声音验证](../tests/cryoffear-automatic-sound/README.md)，需授权工作区MP及匹配私有IDA字节证明。原样本、字节、dumps和运行收据均不随源码上传。

## 成本与下一步

新生产路径只访问固定状态、两项冷资源和SDK对象；无热分配、查表扫描、文件／网络I/O或RNG。播放操作只有原分支发生状态转换时才提交，保持阶段不重复发送。仍需整枪接通后的真实CPU／帧时与多人测量，不能用这6144场景或组件时间宣称游戏性能通过。

继续TMP／MP5完整自动射击、机瞄／推击、所有者／工厂／库存及地图生命期接线，再做HUD购买、资源与P／手模和真实玩法；切枪、丢拾、死亡等完整路径继续按原证接入。VP70配套候选已完整构建及独立冷启动，28文件更新窗口仍待确认。

共享交接已写入本报告与COF_GOAL：PROJECT_STATE／PROGRESS登记“声音组件／SDK发送、原6144和59907检查通过，未整枪／未部署”；GOAL全范围不收窄，TODO保留上述接线及真实验收。按用户要求不发送非打包跨聊消息，由协调方维护共享连续性文件。
