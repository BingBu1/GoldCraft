# CoF 左右手与切枪镜像上下文修复

用户已确认本地候选的左右手与枪炎位置恢复正常。Glock 整枪仍未验收：弹壳、人物手模、P 模握姿、多人和实际性能继续开放。本文记录已实现组件及本地运行接线；公开源码的完整服务接线另批审计，不把混合工作区 DLL 当成公开 HEAD 完整构建。

## 范围与样本

只修改 namespaced CoF 视模的宿主镜像上下文，保留原 CoF 枪口发射数学。授权记录在私有 client-evidence/scope.md；仅分析工作区副本，Steam 原安装只读。

| 模块 | SHA-256 | 架构／基址 |
| --- | --- | --- |
| 原 CoF client | d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00 | x86／10000000 |
| 宿主 CS client | b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2 | x86／10000000 |

## Evidence

E-01：修改前 IDA 取证在私有 handedness-context-20261112/before.json、ida-bytes.json。宿主 VA100141BA 比较 item+AC 与29，100141D8 写小刀标志 RVA10F861；10014109..1001411C 提取状态 bit3，写 Elite 标志 RVA10F860。10066BC1..10066C35 仅在实际视模的 RENDER 路径改变 cl_righthand，EVENTS-only 不走该分支；10066F96..10066FB5 恢复。10069118..10069179 据此镜像根矩阵。原样本完整 SHA 重新核同。

E-02：实际进程只读 native-flags.log 显示 Glock 装备时 Elite／小刀字节为00／01。枪炎源、视模附件、骨骼／光照附件坐标相同，没有给发射器增加猜测补偿偏移。

E-03：tests/cryoffear-viewmodel-context/oracle.py 校验完整宿主 SHA，重放真实分支128组合。私有回执在 build/cof-viewmodel-native-20261112-01；复现参数见测试 README。

E-04：实际生产 RAII／MetaHook SDK1138检查通过，最终回执 build/cof-viewmodel-context-20261112-05/receipt.json；admission SDK10285检查通过。覆盖原值恢复、两种 pass、两手、非CoF、未准备、外国线程、非有限输入及嵌套入口。

E-05：本地客户端01构建输入稳定，SHA ee085cc0cbc85745c891093863e5a67eaa03e719e9c020610cfb27f875bba2d4 已运行，用户反馈“我测试已修复”。最终04的 SHA82ef034b71d9defd47832981340f9b54ee75439886c9b59866f19b31dc02cf1f 输入稳定并已部署B；包含嵌套、线程先验及卸载保护，未再次宣称完整实机通过。02／03链接成功但因其他领域输入漂移被拒，保留回执。

## Finding 与源码

F-01：severity=n/a_re；evidence_ids=E-01／02／03；confidence=high；status=validated。宿主保留上把小刀身份，导致 render 与 events 的 CoF 镜像上下文不同，并随原版切枪更新而改变。实现位置 native/csol/client/weapons/guest_cof_viewmodel.cpp。

修复冷解析两个具名全局及 cl_righthand；实际且已准备的 CoF 视模进入时保存三个原值，暂清小刀／Elite 身份，按作者右手基准统一两种 pass，退出时精确恢复。嵌套入口沿用外层，先校验线程。此为宿主适配，不改变其他枪永久状态。目录生成器校验匹配模块的操作数字节；缺目录时 admission 不发布就绪。

接口／代码为 guest_cof_viewmodel.hpp／cpp、guest_cof_admission.cpp、tools/Build-ClientGameData.py。本地插件入口 StudioDrawModel／Init／三种Shutdown；完整CMake与原服务适配器接线仍需合批审计发布，避免混入其他领域修改。

P-01：path_type=callflow。原预测写宿主刀／Elite身份（E-01）→ RENDER 专属反转（E-03）→ 视模与事件不同侧（E-02）→ RAII统一并恢复（E-04）→ 用户切枪实测确认（E-05）。

## 验证边界与性能

单B图像可见 Glock 在右侧。左侧对照抓到K3，因为人工输入改变了武器，该图不作 Glock 左手通过证据。用户报告是本次切枪效果修复的实机依据。最终04部署回执 build/cof-client-viewmodel-context-20261112-04/deployment.json；临时F11／F12及plugins.ini已恢复，诊断插件已暂停，没有新增资金／弹药／保护发放。

作用域没有堆分配、文件读取、符号搜索或 GL 同步查询，仅固定目录比较、线程／就绪检查及临时字段保存恢复。未完成实机CPU／帧时间对照，不据此声称性能验收。

时间线：11月12日保存IDA与运行字段证据→组件实现／原指令回放→本地01部署→用户确认→11月13日封存04并恢复配置。下一步为原弹壳事件／自身P模消费者、手模预缓存及握持校准。完整25库存项及变体范围不变。
