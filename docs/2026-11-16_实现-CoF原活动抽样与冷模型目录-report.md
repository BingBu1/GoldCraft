# CoF 原活动抽样与冷模型目录

已按原指令实现活动序列抽样，并使用真实人物/僵尸模型验证冷目录。原CoF与CS宿主的LookupActivity算法不同，不能用同名宿主函数冒充等价。993原场景/79982检查通过；生产目标伤害/动画桥仍待，无部署或B/专服操作。

## 证据与差异

原CoF MP x86，映像基址10000000，SHA-256 0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63。修改前analysis/cof-weapons-20261111/player-action-runtime-20261116/PREEDIT-activity-catalog.json，116B完整1000F450..1000F4C4与39B1000E950包装器由IDA和PE逐字节核同，三份原IDA归档SHA绑定。模型验证补充activity-catalog-models-validation.json绑定原PREEDIT和三实际文件身份，非修改原证时间。

| Evidence | Finding | Path |
| --- | --- | --- |
| 原1000F450..1000F4C4完整一遍序列表扫描 | 每个匹配活动累计权重，累计零时直接选本序列；否则每个候选各调用RANDOM_LONG(0,total-1)，signed比较返回值与当前weight | native/guest/cof/activity_catalog.hpp::Lookup |
| 原1000E950包装器、原SetActivity100C1F50 | 模型查找是受击选择和实际安装共同消费者；不可改变随机数消耗顺序 | 目录供后续实际SDK目标桥使用；未直接调用CS宿主LookupActivity |
| 实际external/ReGameDLL_CS/regamedll/dlls/animation.cpp::LookupActivity | CS先遍历统计，再一次RNG/二遍选择，零总权重也会随机抽 | 已排除作为CoF等价实现，未更改CS原函数 |
| 原模型描述符步长176/活动+40/权重+44；实际SDK engine/studio.h | 冷准备按实际序列表保留原顺序，并依据宿主MAXSTUDIOSEQUENCES2048界定缓存 | ActivityCatalog::Prepare/PrepareModel |
| 当前player_actions.cpp及ReHLDS非像素cache约定、实际CSOL解码器 | 完整v10模型与严格texturedataindex前缀可读；磁盘20/21须先由已有真实解码器解码 | PrepareModel输入适配；测试链接native/csol/model_codec.cpp，不新造解码规则 |

## 实现与性能边界

冷准备复制并按activity/原sequence编号排序，构建连续组索引；同活动候选始终保持原序列表顺序。运行只二分组并遍历匹配候选，保持每次原RNG调用及signed选择；不扫描其他活动，不读资源/模型，不分配。权重加法以定义良好的uint32位表示保留原ADD32回绕，不依赖C++有符号溢出。

目录按地图/模型所有者实例存储，容量受实际宿主2048序列限制，没有无界全局缓存。坏模型/替换失败保留旧目录；同步RNG/current回调重建目录后，旧查询按代次取消，不能继续用旧vector迭代器或提交陈旧结果。空但已准备的实际序列表返回原-1；未准备/缺服务不伪装成模型不存在或成功。

这是冷数据布局优化和宿主输入校验；原有效非空模型选择算法按指令保持。原空model指针提前返回路径不作为有效输入；既有引擎/模型所有者仍须验证生命周期，不能以空span猜一个序列。目录尚未接生产攻击/怪物消费者，没有实机CPU/帧时间采样；零分配/原指令差分不能称FPS验收。

## 验证

最终build/cof-activity-catalog-models-20261116-06/receipt.json成功：993原场景（768构造模型＋225真实模型场景），79982检查，47个实际原指令地址无未覆盖区，288实际编译依赖前后SHA稳定。三模型为stock urban、CSO csol95、CSOL zombi_host，全部原件只读并以私有SHA锁定；后者v21经实际model_codec解码为v10再解析。

每个选择和每次RNG low/high/result逐事件一致。覆盖零/负权重、32位回绕、重复/交错活动、缺活动、回调拒绝/取消/目录同步替换、两种模型存储和负尺寸/偏移/逻辑长度/2049序列拒绝；10000热查询无new。四类空/截断/错误/多余夹具全部拒绝。x86/C++20/Arkari/O3/ThinLTO/AVX2/precise/-ffp-contract=off保持。

失败保留：01主体通过但wrong-result负例用ffffffff恰与空序列表真实-1相同，修正为不可能序列值；03/04正确拒绝磁盘v21，夹具原先误当引擎解码后的v10，现链接真实解码器（未放松生产解析）。05全部通过；06增加实际宿主2048上限/2049负例并最终通过。没有猜测原样本为空时的返回寄存器或放宽模型版本。

本阶段为头文件核心，无生产消费者，故没有新增完整DLL构建、部署/UI/玩法/多人验收。已有死亡快照完整MP05fa73c4与原任务29阶段另有回执，不能冒称本目录已经让实际怪物受击。

## 后续与交接

继续实际模型/序列复位消费者与原四任务链的灾变目标桥，再接生产体力种子/独立玩家任务资源、声音/HUD及M16购买。目标伤害必须保留玩家归属/升级/奖励/死亡，CoF原伤害位不可直接传CS，不借用CSOL人物踢击力/定时眩晕。完整25库存身份/变体Goal保持active。

共享连续性精确交接pending（遵守不做非打包通信）：PROJECT_STATE/PROGRESS记录算法差异、993原场景/79982检查及目录未接生产；GOAL完整范围不缩；TODO保留目标/生产体力/HUD声音/购买及整枪多人性能。领域COF_GOAL本轮更新，其他共享文件与WIP保持。
