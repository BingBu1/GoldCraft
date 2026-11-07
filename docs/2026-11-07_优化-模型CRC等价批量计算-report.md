# 模型 CRC 等价批量计算

## 修改前证据与范围

诊断引擎86a0aa64在同一空服zs_trap三轮实际加载中，实体阶段CRC分别2.609601、2.668731、2.638371秒；完整引擎changelevel为8.368395、8.815978、8.860918秒。来源build/map-transition/profile.json及L1107061/063/065日志。CRC是当前已测单阶段最大耗时；动画storage约0.78–0.85秒，入口safety不到0.1毫秒。不能把decode父子时间相加。

实际开源宿主external/ReHLDS/rehlds/engine/model.cpp在COM_LoadFile后、解码前，对完整原文件调用CRC32_ProcessBuffer，再Final；保留initialCRC和firstCRCDone，后续不同仍Sys_Error。engine/crc.cpp的原实现逐字节使用IEEE反射多项式0xedb88320，Init为全1、Final取反，分段调用传未取反中间值。nBuffer非正时保持状态且不读buffer。

本改动是保持结果的宿主性能适配，不宣称为CSOL原代码。采用从同一多项式编译期推导的八张表，每次计算8字节；短尾仍逐字节。按原签名保留所有调用方、覆盖范围、初值、分段和结束语义，未缓存或跳过任何文件校验。不得用SSE4.2的CRC32C指令替代IEEE CRC。

对应源码native/engine/crc32_ieee.h及外部宿主crc.cpp的CRC32_ProcessBuffer，最终通过根patches/rehlds-goldcraft.patch发布。必要验证：与真实旧函数/独立bitwise oracle的多初值、所有短长度/未对齐、分段、受保护页末尾、文件变更对照；实际角色语料结果一致；完整x86构建和同图三轮。源码实现不等于性能或玩法验收，当前均待执行。

原版复核补记：匹配hw的CRC接口、IEEE表和原指令已由IDA核对并回放；原版也是IEEE，但使用单表串行展开，未采用本八表算法。原模型完整CRC首次写入链尚未闭合，故本次完整输入/首次记录/变更拒绝仅依据实际ReHLDS宿主现有源码，不能伪称原CSOL有完全同位流程。详见[原版CRC边界](2026-11-07_取证-CSOL引擎CRC32与宿主模型校验边界-report.md)。

## 验证结果

真实x86/O3/ThinLTO/AVX2夹具执行303085项断言，覆盖任意初值、零及负长度至INT_MIN、输入/输出别名、分段续算、未对齐和受保护页边界。150个实际角色共1229174960字节，450组旧/新CRC及独立zlib结果一致，每模型首/中/末修改均检出；源与模型SHA前后未变。纯计算每轮旧2.368–2.381秒、新0.548–0.553秒。入口为 `python tests/engine-crc32/run.py`，细目见tests/engine-crc32/README.md和build/engine-crc32/result.json。该字节变更测试不是实际Sys_Error拒图实测；加载分支未改变。

完整引擎直接项目构建耗时93.522秒，1433登记输入无漂移，109链接对象中只有crc.obj内容改变；PE32及匹配PDB通过。DLL为5f869363a5e116ea01eaee924bd332f4521fba725e742569b7ce70c03c339549，PDB为f7d152ea6a19e8995b79c463f982399c49fe81aa562a5a29f1441aae4da96cf6。验证报告build/rehlds/Release/crc-verification.json。随后仅本地专服swds/PDB/登记原子替换，正式服务器未改。

## 专服实际对照

同一zs_trap、同样MP/资源/80MiB独立模型缓存，旧新各三轮；计时期间没有构建和语料bench并行。

| 引擎 | 三轮完整changelevel | 中位数 | CRC阶段范围 |
|---|---|---:|---:|
| 86a0诊断基线 | 8.368 / 8.816 / 8.861秒 | 8.816秒 | 2.610–2.669秒 |
| 5f86等价CRC | 6.346 / 6.645 / 6.472秒 | 6.472秒 | 0.592–0.602秒 |

完整引擎中位数减少2.344秒，约26.6%。证据为build/map-transition/{profile,crc-after}.json和日志L1107061/063/065、L1107071/073/075。每轮仍521次CRC、412次模型解码/缓存提交，未减少校验调用。此为空服实际服务器时间，不是客户端加载总时间或多人玩法通过。

## 客户端观察边界

单B真实zs_trap→zs_2gate在21.410秒出现新mapCollisionEpoch及viewReady，资源接收已结束。但首次采样器错误要求predictionFrames在此之后继续增长，60秒失败，原记录build/map-transition/crc-client.json保留。源码确认该计数仅在 `!server && !HasControl() && world && Seconds()-last_colliders<1` 的Minecraft碰撞注入路径增长，不代表每一帧原生CS预测；不能据此认定画面卡死或完整重连验收。

采样器修正为新地图/完成清单/fresh view后，要求后续nativeWork时间窗推进且原生HUD_Redraw有调用；prediction仍保存为观察值。该规则验证宿主回调恢复，不能替代视觉、输入或稳定帧率。后续真正客户端卡顿仍需加载阶段计时，完整goal继续开放。

修正后的单B实测zs_2gate→zs_trap：新地图视图状态24.792秒、后续native HUD回调25.258秒，最大状态报告间隙7.887秒；同一MetaHook.exe PID14168、完整清单3429→3325与服务器地图身份通过。最终窗口未在前台，不能作为前台FPS或显示器首帧验收。证据build/map-transition/crc-client-reverse.json。实际GoldCraft.dll匹配Git66e0fb8fae32460921e7649b24c488ffce63341b的plugin.cpp，证实该HUD窗口消费和prediction计数条件；未按当前WIP猜运行行为。


## 现客户端原有计时的下一阶段样本

在既有e6a18fd3客户端上启用CSOL_HITCH_TRACE，保持单B；正常退出14168后启动37384。专服、客户端DLL和地图资源未再改。真实zs_trap→zs_2gate新地图视图23.172秒，后续原生HUD回调23.380秒；记录build/map-transition/crc-client-traced.json。CSV为build/map-transition/client-hitch.csv，已固定前294613字节SHA792107908d15a3a47b15e6b2bf29c90e8654293adc4e9510811d2d7ce50b6e27，按前缀复核不要求正在追加的文件全SHA保持。

实际QPC/频率与Python3.14 monotonic同一时基已核。对应区间记录LoadStudio1188次、414个模型名、共4.188秒；其中358名3次、55名2次、1名4次，无LoadStudio彼此嵌套。ReadResource1.783秒为嵌套项，不与LoadStudio相加。最大7.683秒frame-gap内仍有约4.563秒未被已记录scope覆盖，不能把剩余时间猜成SEQ或GPU。已有客户端诊断含100微秒门限，缺席短调用不算零耗时。

下一步追三轮加载的实际消费者和缓存失效/内存淘汰原因，再确定如何避免重复工作；不能只凭同名重复就跳过加载。运行身份与计时入口见本地报告docs/2026-11-07_审查-运行客户端加载计时入口-report.md。最终请求F10正常退出37384被消费但进程仍在，未强杀；CSV前缀是已落盘证据，不宣称最终fclose。客户端停帧与画面验收仍未完成。

维护补记：按只留两份部署备份的规则尝试删除旧engine-177e61795593bcd4和engine-9dd350a9db200d84，工具自动审批返回blocked by policy；未执行删除，未改用其他工具规避。两个目录保留，需用户手动清理。
