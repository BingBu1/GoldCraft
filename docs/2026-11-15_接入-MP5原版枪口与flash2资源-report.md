# MP5 原版枪口与 flash2 资源

本阶段接入原版 MP5 本地／世界枪口组件，并补齐 flash2 的下载和服务端冷准备。它尚未进入实际客户端能力／购买目录，没有部署新 DLL，也未操作单 B；用户继续自行测试已安装版本。完整 CoF Goal 保留25库存身份及变体，不把组件通过记作整枪完成。

## 范围与修改前证据

Scope 为用户授权的 CoF 合作模式武器移植，验收边界见 [CoF Goal](COF_GOAL.md)。受保护安装只读；生产源码直接修改项目，原版文件及分析记录只保存在私有 analysis。父独自推进，无子代理、无非打包跨聊通信。

| Evidence | source_ref／身份 | 结论与对应位置 |
| --- | --- | --- |
| E1 | 原x86 client.dll，base10000000，SHA256 `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`；完整Local1004BC30／1166B、World10090130／1209B | 本地1004BD77的id27分支先查flash1再查flash2，base size2；世界借用TMP P的分支仍查flash1、size2。`native/guest/cof/client/muzzle_flash.cpp/.hpp`。 |
| E2 | `analysis/cof-weapons-20261111/mp5-integration-20261115/PREEDIT-client-muzzle.json`及`PREEDIT-flash2-resources.json`；2375B重新对照原PE，归档`muzzle-before.json` SHA256 `5acf906c8721bf7dec53746593639204012f1288ceddf67ada78e26d96518aa1` | 修改前已记录分支、调用顺序与源码hash；不是根据模型文件名推测。 |
| E3 | 原flash2完整122304B，SHA256 `f7f098c0db5eb3254458593d14c6b5a2e2e37af2d4afc1a1e56bb166c1df778c` | namespaced `models/guest/cof/common/flash2.mdl`，资源作者保持原字节。 |
| E4 | 实际ReGameDLL enginefuncs／ReHLDS PF_precache_generic已有接口与回归 | 客户端专用模型使用generic下载，不添加服务端model slot；这是宿主适配，不能当作原CoF服务端结构。 |

F1（severity=n/a_re，evidence=E1/E2，confidence=高，status=组件已实现）：MP5本地不能沿用TMP的flash1／size4，世界也不能照搬本地flash2。原第二次模型查询及其发生在随机数之前的顺序均保留。F2（severity=n/a_re，evidence=E2/E3/E4，confidence=高，status=冷资源已实现）：原本45文件的MP5候选缺本地flash2；新增后为46文件，40声明＋5弹孔＋1继承弹匣贴图。

P1（path_type=callflow）：原Local1004BC30→id27→flash1／flash2两次查询→原临时实体／点光；原World10090130→借用TMP P名称分支→flash1／size2。本组件只还原已证发射边界，骨矩阵和实际图形输出由宿主提供。P2：当前资源目录→资源作者原SHA核对→独立原子候选→地图VFS预检→generic下载字段；实际客户端flash2冷句柄及MP5准入继续接。

## 源码与生命周期

新增`MP5Muzzle`，共享原P345发射核心；TMP仍保留重复flash1查询，本次将私有布尔参数改为明确的第二模型键。其余池分配、随机数／时钟顺序、渲染字段、烟和光不改。原指令观察器记录flash1／flash2不同身份，避免“查错资源但次数相同”也能通过。

`resource_catalog.hpp`新增固定`MP5ClientModels`；实际`guest_cof_resources.hpp/.cpp`在任何precache之前预检flash2存在，地图冷准备只注册generic并在固定输出数组保存索引。所有回调的取消／失败维持事务结果不变。既有五枪模板的额外数组为空，保持原回调顺序及模型数量。资源工具严格绑定当前驻留P身份，不再用历史无附件候选当本轮P验收依据。

## 编译与自动检查

- `build/cof-mp5-muzzle-original-20261115-02`：Arkari x86／C++20／O3／ThinLTO／AVX2／precise及禁止FP融合；P345、Revolver、VP70、TMP、MP5各1145原指令场景零差；记录源码前后hash和实际PE32产物。第一次观察包装60秒超时未完成总回执，原产物保留；本轮使用有状态后台进程完成整套。
- `build/cof-mp5-flash2-cold-20261115-02`：最终六枪4120实际SDK检查零失败，其中MP5657／41真实文件／92回调边；包括每缺件、每外调撤销、失败返回、完整时长和metadata字段。使用实际当前作者P及完整原客户端枪口证据门。
- `build/cof-mp5-flash2-author-20261115-02`：最终408检查／32CLI场景通过，140输入稳定；实际MP5候选46文件，flash2与作者P及其他原资源逐SHA核同。

`build/cof-mp5-flash2-map-20261115-04`：最终1023实际SDK检查零失败，生产Host编译exit0，834组合输入前后稳定。实际地图准备回调核对generic句柄且不占模型槽，缺flash2时完整退役并拒绝工厂；五枪原有场景及MP5射击／停止／装填／模型重绑／死亡／退图均继续通过。

01保留了夹具未把新flash2装入模拟VFS的失败；补齐后02暴露新增测试InitEdict参数类型笔误，已修为实际edict引用。03由父进程提前创建阶段脚本目录触发fresh-output拒绝，未执行编译。以上未修改生产武器逻辑、未部署；最终04完整通过。

## 成本、部署及剩余

只新增一个固定资源声明和一个generic索引；文件准备在地图冷阶段，无新逐帧分配、读盘、网络、缓存或服务端模型槽。原两次模型回调保持，未以跳过原逻辑换取速度。本轮没有真实CPU／I/O采样或帧时结论，不能把原指令／受控SDK检查称为实机性能通过。

未构建整套配对DLL、未部署、未改变游戏配置或临时保护。继续实际客户端flash2冷句柄、MP5目录／协议／音频／准星／HUD／机瞄／购买与JSON伤害、生产Host选择和整DLL，然后在已授权环境窗口验收；当前用户自行测试，不另行接管B。P握姿、CSO手模／Glock破面、多人与长期性能、其他库存和变体均保持未完成。

## 时间线与持久交接

2026-11-15：重核原客户端及2375B／flash2身份→保存资源修改前证据→组件和冷资源实现→原指令／SDK／实际候选验证→地图组合与源码发布继续。没有创建源码快照。

领域`docs/COF_GOAL.md`同步本阶段。共享文档更新明确待主调度应用（按用户要求不发非打包消息）：PROJECT_STATE／PROGRESS记录“MP5枪口组件和46文件闭包已验、未实际客户端／购买／部署”；GOAL保持25身份和变体；TODO把下一步列为实际MP5客户端／Host全链，已部署五枪仍由用户自测。

原版 MP5 flash2 当前是generic下载；现有客户端`guest_cof_effects.cpp`只缓存flash1，且其常规`Model`走EV_FindModelIndex。后续须在实际客户端冷阶段核验并接通generic模型的加载API／句柄代际，不能因为资源声明或下载完成就强行设置准备成功。现有目录只有五个Firearm身份，MP5原27／宿主901尚未加入准入和买单。

## 后续全局生产者纠正

本阶段generic-only分类已被新IDA全局生产者证据纠正：原CoF会通过PrecacheModel登记flash2，实际宿主CL_LoadModel仅查登记表。撤回“generic即可构成完整闭包／不占model槽”的当前结论；该候选未部署／未用于MP5准入。现已改为模型登记并接实际客户端缓存，详见[后续报告](2026-11-15_接入-MP5客户端路由与全局模型登记-report.md)。
