# TMP 冷资源、离线作者与原生工厂

TMP继续整枪接入。本阶段冷准备、离线资源作者和原生构造通过实际检查；没有安装TMP运行回调、注册购买、发布客户端能力或部署新DLL。当前VP70人工测试可用版本保留。

## 修改前原证

原CoF hl.dll为PE32 x86、VA基址10000000，SHA-256 0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63。复用匹配client d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00的Studio5004本地声音重写，以及既有枪口/粒子事件消费者；未按文件名猜播放生命周期。

本轮在IDA重新读取Precache1007F070完整289字节、Spawn1007FC10完整279字节、Info1007E220完整113字节，合计681字节，与当前PE原字节逐一核同。Precache经虚表+30调用，动态原生循环声和尾音分别写入1A8/1AC后预缓存；Spawn在公共世界初始化前选择weapon_tmp库存元数据。完整原生类和自动声音证据另见前一阶段报告。

| Evidence | Finding | Path |
| --- | --- | --- |
| Precache1007F070；实际V/P/W与七个原生WAV完整SHA | 保留原TMP模型、五个Studio声音、独立loop/end | native/guest/cof/resource_catalog.hpp |
| 原库存元数据与机瞄消费者、原V的16段动作 | forward/right/up为0/-4.75/3、FOV50；时长从真实帧数和fps冷准备 | native/csol/server/weapons/guest_cof_resources.cpp/.hpp |
| 实际Studio5004序列/帧/选项与匹配client重写 | 七个事件共用五个weapons_new样本；loop/end不伪造成本地Studio样本 | tools/Prepare-CryOfFearResourcePack.py |
| 当前ReGameDLL GetClassPtr/OnCreate/CCSPlayerWeapon和已审通用工厂 | typed TMP实体保留edict/私有数据/代际检查；重复构造不清库存 | native/csol/server/weapons/guest_cof_tmp_factory.cpp/.hpp |

修改前私有记录位于analysis/cof-weapons-20261111/tmp-integration-20261115/：resource-ida-fresh.json、resource-inputs-before.json、cold-implementation-before.json、resource-author-before.json、factory-before.json。原模型/脚本/反汇编正文不公开。

## 实现与成本

新增TMP资源类型及共用冷准备器入口，固定存储10模型/精灵、22原生声音、5本地声音下载、2事件、库存/粒子字符串、16段真实时长。共42文件（包括共享弹匣贴图伴随文件）。继续使用原生VFS和引擎的依赖广告；预检完整存在性、元数据、模型和贴图后才预缓存，失败保持旧输出，所有外部回调均重新核权威代际。每次冷准备三次完整读取，未增加绘制、输入或逐发I/O；实际地图运行时尚未调用TMP入口，不会在当前玩家对局中逐实体重复准备。

离线作者固定41个主资源和五个共享弹孔，加一件继承贴图依赖，生成47项候选；每项核完整SHA、文件身份、MAX_QPATH、原事件顺序和独立副本。V/P/W、声音和库存原字节不改。原生自动声的loop/end仍由独立自动声音控制器消费，不混成本地单发声音。工具仅生成候选，不部署。

工厂为既有NativeWeaponFactory的typed接入，宿主身份900、原30发；原生构造不触发资源准备，真实Precache/Spawn消费显式冷回调。导出函数不代表安装回调或开放购买。未决原特殊状态、P握姿/附件与客户端效果准入继续单独验证。

## 验证与边界

- build/cof-tmp-cold-resources-20261115-01：五枪实际SDK资源准备3463项零失败，其中TMP653项、42真实文件、94个可取消回调边界；四手枪原709/787/668/646检查保持。TMP16段真实模型时长与机瞄浮点逐位核对，缺资源、坏元数据/模型/贴图、引擎失败、每个回调退役及事务输出均覆盖。
- build/cof-tmp-native-factory-20261115-01：26实际场景、161 SDK检查零失败，包含重复构造保留75发/动作、缺回调、重入/代际变化、edict回收、错误私有数据及真实Spawn。未安装到实际引擎。
- build/cof-tmp-resource-author-20261115-01：351检查、27 CLI场景通过，五枪共236资源副本；TMP47项逐SHA/独立文件校验，原V模型全字节保持。任何旧计划在工具/消费者身份变化后均拒绝复用。
- x86 Arkari Clang、C++20、O3/ThinLTO/AVX2、precise FP及禁FP融合保持。没有完整DLL/运行部署或实机CPU、I/O、帧时验收，不能据组件成本宣称性能通过。

上述冷资源实测只证明解析/注册回调输出；前一阶段派生Post对照仍使用受控1秒时长，不能把两项相加称整枪实际时序已验。新增原生工厂不证明实际购买、机瞄、音频播放或第三人称握姿。

## 下一步与交接

继续canonical owner、实际Host/地图运行时、自动声音和KillLocal发送、客户端id14资源/粒子/枪口、购买/HUD/弹药、CSO手模及P校准，再完整双端构建与配套游戏/多人/性能。TMP未开放购买，全部25身份与变体Goal保持active。

共享文档精确待应用：PROJECT_STATE／PROGRESS追加本阶段三组验证及未部署边界；GOAL保持全部CoF范围；TODO继续TMP整枪而不重复已获人工通过的VP70购买。本报告和docs/COF_GOAL.md作为持久领域交接，不发送非打包跨聊消息。
