# 原生 HUD 恢复分辨率与退栈审计

本轮先独立审查 validation04/05，再按父代理授权窄修恢复边界。04/05 的原证、冻结源和结果均保留；不得将 05 的 914/0 解释成这两个新边界已经通过。仅原生 HUD 组件和对应测试在本轮范围内；不接 bridge、不启用资源 v4、不部署或操作 Git。

## 修改前原证与缺陷映射

精确宿主原 CS client SHA-256 `b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2`、GoldSrc hw SHA-256 `9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295`，均 x86、静态基址 `10000000`。以下为 VA。这是宿主 ABI/内存安全适配，不改变 CSOL 原游戏规则。

独立身份审计 `build/resource-native-hud-recovery-20261105/audit01.json`，SHA-256 `3fb4b1012e7e6592e9f14ba541933834276d02f9216eca2d0040737324bbf117`：3420 个引用、630 个去重路径匹配；106 个原 IDA 窗口、16639 字节与精确 PE 一致。精确 IAT `102B2438` 的8字节沿既有规范单列为 IDA 归一化项，不冒称原字节一致。04 handoff SHA `731cc4919320842bb467229629a4cfbaeb23ebdb1bb64dc0ac3042b780a143f3` 已核；仅当前源码、测试和已更新领域报告3项是明示的04被替代路径，04 frozen 无漂移。04实际 SDK include 链、05窄增量和复用的04 hud_sprites.obj均已核；既有测试没有重复执行。

| 原证/实际源 | 结论 | 实施位置 |
|---|---|---|
| client `100510A0` 开头从实际屏幕宽度选择320/640；非空目录分支 `10051266..100512C3` 仅按新分辨率过滤旧目录并写原 HSPRITE 数组，不重算 selected、不重新分配 | 只在回调返回后验证数组来不及防止不同分辨率项数造成的越界。05的普通 `Rollback → restore` 没有当前屏幕分辨率输入；仅 `Initialize` 有事件分辨率门。 | Binding增加每次调用时只读实际屏幕分辨率的回调。准备/重建/提交/恢复均核对；恢复必须在 owner.restore、write(old_) 和原 native 回调前拒绝不匹配。不能读 gHUD 的旧分辨率字段充当实际屏幕。 |
| 05 `restore` 与初次 `rebuild` 都写 Phase::rebuilding；`Initialize` 对该phase无条件 `replacement_=read()` | 恢复途中非C++退栈时gHUD已经指向旧数组；下一自然初始化将真实候选快照覆盖为旧数组。候选三数组失去释放来源。第一次恢复的 displaced Scope IDs仅存在于局部变量，也会随退栈丢失。 | 区分 restoring 与 rebuilding。将恢复前 displaced 快照存为成员；只有初次重建中断才捕获 replacement。确认原回调已退栈后，按当前 Scope 引用退休不再使用的ID，保留候选数组至成功重绑。 |
| client Scope VidInit `10065430..10065517` 分支各创建1或4个ISurface ID；每次Create后立即写gHUD字段，加载纹理在后 | 恢复可以在部分Scope ID已替换时中断，不能假定回调失败意味着没有分配，也不能把旧全部ID再次删除。 | 退休去重、跳过当前字段仍引用ID；失败重试和非C++退栈使用同一保留/退休规则，经相同 ISurface owner释放。 |
| 原新数组 `100B4910`、delete[] `100B4919` 与 `CHud::~CHud 10050420` | 三数组必须由原 client owner释放，目录由 hud_sprites vector owner管理。 | 保留04/05的独立owner合同，不因恢复失败猜测释放外来地址。 |

05的两处回调后先核目录/数组身份，再遍历目录，顺序正确，保留该修复。所有恢复仍要求父桥接证明消费者暂停、VFS/cache代次一致以及先前原调用已真正退栈。C++ catch不能宣称捕获Host_Error longjmp；本轮新增外层非C++异常退栈夹具，用于验证组件保留状态，不冒称已跑真实引擎Host_Error。

## 实际修改和验证

生产仅改 `native/client/resource_native_hud.hpp/.cpp`，测试仅改 `tests/native/resource_native_hud_tests.cpp`。05的回调后目录身份检查保留。新增 `Binding::current_resolution` 为必填、noexcept只读回调，每次从实际屏幕状态取320/640；缺回调、未知结果或与实际边界不符均拒绝。准备后变化会在重建前拒绝；回滚在目录owner切换和旧数组写回前检查；提交也不在分辨率已变化时释放回滚代次。自然初始化同时要求事件记录与这次实际读取一致。

新增 `Phase::restoring` 与成员 `restore_displaced_`。恢复中断不会覆盖候选 `replacement_`。在父确认上一原调用已退栈后，比较当前Scope字段，退休其不再引用的旧ID，再重试恢复；保留候选数组直至旧消费者重绑成功。普通Rollback不能自行接管仍标记为回调中的阶段。

实际 x86 SDK 使用既有 Arkari clang-cl、C++20、O3/ThinLTO、AVX2、精确浮点、/EHsc，最多两个编译任务；独立链接测试可执行文件。`hud_sprites.obj` 复用04版本，SHA `da12a3974c7f7bd6c89574ef3ee8d5f92e05b87ec876496d003f51e2598ce27a`；它使用的Catalog/DirectoryOwner声明及其自身源码未改，不依赖Binding/Refresh布局。SDK include审计确认这条边界，没有混用旧Refresh对象布局。

- `build/resource-native-hud-recovery-20261105/sdk01/result.json`，SHA `52476b451dbc3c285bde717c05dadbfcec0866e4a4e41a2298131b1c42e855bc`：新增恢复组 **752检查/0失败**；合并既有组 **1666检查/0失败**。覆盖普通准备/重建/回滚/提交的新鲜分辨率，过期自然事件分辨率，以及初次重建和连续两次恢复在Scope分配前、分配1/2/4个ID时的非C++退栈。
- SEH夹具在原回调内抛专用非C++异常，外层 `__try/__except` 证实它穿过 `/EHsc` 的 `catch(...)`；随后才提供新的退栈已确认事件。没有用返回false替代中断，没有直接篡改组件phase。最终实际分配集合只剩当前3数组和5个Scope槽位所持ID；释放器逐项检查原owner、重复释放和当前字段引用。
- `negative05/result.json`，SHA `89ad2c9e8e667d8a448bd992b91e5b9802a7152bfa4af91369e04a58b66b18cc`：**05冻结生产源与05冻结头文件**重新编译同一新增测试，保留ABI一致；只执行新组得到 **603检查/14失败**。失败为6个分辨率门/观察断言及4种退栈位置各自的数组和Scope泄漏断言，并非14个独立缺陷。不是拿新头文件去链接旧Refresh对象制造失败。
- 编译器不支持 `/sourceDependencies`，首轮日志保留该警告，没有把空依赖列表当完整输入证据。补 `/Zs /showIncludes` 四次窄语法扫描，没有重复运行测试。`include-audit01/result.json` SHA `d1539622aa61049d0998077a1524a6d920b5cbdbf38f3dfa389c0a41f51ec554`，**2132引用/422去重文件**，全SHA已核。

## 父接线要求与验收边界

`Binding::current_resolution` 必须每次新取实际屏幕宽度，按已证原分支选择320/640，不能返回gHUD保留字段或缓存的事件分辨率。原 native trampoline、原 client delete[]、实际ISurface owner和hud_sprites owner合同保持。恢复要求旧VFS/cache视图已就绪、消费者暂停、实际owner未更换；新的自然入口还须有真实ClearState epoch及上次原栈退栈证据。

新增测试证明组件遇到非C++退栈后保留与回收两代资源的行为；它不是原引擎Host_Error现场、真实分辨率切换或游戏画面验收。未构建完整DLL、未接bridge、未启用v4、未部署、未修改共享四文档或Git。未发现需要再次改动hud_sprites owner的证据。完整DLL与实际同名更新、恢复失败、断线/重连及普通服务器回退仍留父任务继续。
