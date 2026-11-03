# SKULL-2 服务端生命周期验证

2026-11-03，独立验证使用真正的 ReGameDLL C++ 类型、武器导出工厂、完整函数与已核验的生产对象。最终 `sdk-lifecycle-10` 五组共 **75 项行为检查、192 项引擎边界检查，零失败**。完整服务端 DLL 也已构建，尚未部署；购买交易、真实网络编码和图形多人另验。

## 验证范围与证据

| 证据 | 检查与发现 | 后续路径 |
|---|---|---|
| `build/csol-skull2-20261103/sdk-lifecycle-10/result.json` | 实例容量/取消 9、实际换弹与同帧开火 25、实际丢枪 5、实际 GetWeaponData 5、重入保护 31，均零失败 | 保留购买事务与实机边界 |
| 同目录 `prepared.json`、编译日志与各组日志 | 生产源采用独立冻结副本；真实 SDK、引擎边界由明确回调控制；实际包含文件无未知或变化，所有声明输入前后保持 | 可复核编译参数、实际读入源和对象来源 |
| `build/csol-skull2-20261103/sdk-negative-01/result.json` | 只换回旧 SKULL2 对象，十种重入情况全部出现弹药/状态错误：31 检查中 10 失败，SDK 边界 38/0；正常退出，无崩溃 | 证明新增保护具有实际反例，旧版不可部署 |
| `build/csol-skull2-20261103/server-full-02/handoff.json` | 40 TU + 1 链接，291.893 秒；346 个对象，其中 306 个复用；2,186 输入保持、1,158 实际输入、未知输入 0 | 待双端配套部署、HTTP 和游戏测试 |
| 同目录 `product-verification.json`、`complete-object-provenance.json` | x86 DLL 与 PDB 匹配、输出文件独立，完整对象来源闭合 | 可作为后续增量构建基线 |

最终生产武器源采用 `server-03/04` 的 `wpn_skull2.cpp`，SHA-256 `aab71fcdc321ccb5c118006c9bb609d6891f1023e5e9dde3aa41b965ca3d5608`；实际基础类 `weapons.cpp` 为 `1656ed3cef21d90f36891895aee076cad7772844c14d2f192359e3637d738993`。核心原指令对照见另一份核心报告，本报告不以组件替代原证。

## 实际执行的调用链

测试经导出的 `weapon_skull2` 工厂创建真正的派生对象，以基础 `Spawn` 建立每个实体自己的 ReAPI `ItemInfo`，然后控制玩家持有关系。没有调用完整武器的资源准备或普通地图拾取流程，不能把这部分视为 Spawn/Precache 已验。

- 两个实体分别保持 14/7 容量；B→A 待切换时 clip 11 不提前裁剪；Holster 恢复原容量；严格 deadline 相等时不提交，超过后仅提交一次。
- 调用真实 `Reload`、`DefaultReload`、基础 `ItemPostFrame` 和 `PrimaryAttack`。六组 A/B 模式与不同备弹量核实：基础类先扣备弹、清 reload 状态，再执行派生完成回调；同帧攻击观察到最终弹匣奇偶侧及一次开火扣弹。仅 `SetAnimation` 的真实 ReAPI 插件回调记录动画请求，不执行人物渲染。
- 调用真实 `DropPlayerItem`、投放回调和 `CreateWeaponBox/PackWeapon`，验证单枪 3 发、备弹 20 变为双枪 10 发、备弹 13，并保留同一个实体。之后单独验证 Save/Restore 助手；这不是完整购买失败回滚验收。
- 调用真正导出的 `GetWeaponData`，检查扩展武器 slot 0、私有模式、精度、前次开火时间、射击计数和 `m_flNextReload` 倒计时。没有执行实际引擎 delta 编码、网络传输或客户端预测重放；字段量化与网络实机仍独立验收。
- 在真实基础类 `ItemPostFrame` hook 返回后分别改变健康、休眠、删除标志、所有者、出生编号、断线、活动武器和玩家/武器实体序号，共十种情况。均阻止待完成切换再次搬移弹药。此组没有释放实体内存；真正移除路径继续由购买事务专项覆盖。

## 首次失败与修正

保留每轮输出，没有覆盖失败结果。早期 SDK 发现生产遗漏 `weapon_service.h`、`DefaultDeploy` 的实际可写指针 ABI，以及真实基础 `weapons.cpp` 缺少生命周期接口声明；均交由实现方修复，最终使用新冻结源。SKULL-5 不存在于所复用的旧完整 DLL，显式增加其四文件和配套弹道重载两文件，不能继承旧基线不存在的新枪验收。

验证准备器早期两次没有正确闭合冻结头文件映射，另一次遗漏弹道重载依赖，分别保留。运行时 `sdk-lifecycle-07` 首次在动画发送遇到未实现的 `pfnCanSkipPlayer`，`09` 在真实开火的材质查询遇到未实现的 `pfnTraceTexture`。夹具分别规定不跳过本地动画、空世界无纹理命中；最终 `10` 通过全部五组。没有删除或跳过生产调用来让测试通过。

## 完整服务端构建

基线为已验证的龙炎资源修复 DLL `60ad5e51…`，增加冻结 `integration-01/server-04`、SKULL-5 和弹道依赖。保留每个 TU 原有 C++20、O3、ThinLTO、AVX2、precise 参数；PCH 源及 552 项依赖没有内容变化。所有受影响 TU 根据实际依赖重编译。

- `mp.dll`：5,076,480 字节，SHA-256 `bd5b89577a063973bd27086847ac0e383c2ad923f614a32d3bc04d1d6bbfeae7`。
- `mp.pdb`：40,906,752 字节，SHA-256 `5e46dca5d39d3211195f4cba0e159b01498a3f8b3d2047bcda1ba4f64eb63ecd`。

`server-full-01` 只有准备阶段失败，没有编译。`server-full-02` 才是本次通过产物。二进制、分析资料、运行资源和构建缓存不进入源码发布。

## 复现入口

从项目根目录运行。需调用者准备自己的匹配 SDK、已核验完整对象基线、冻结候选清单、生成的引擎边界声明，以及 x86 编译环境。每次输出目录必须不存在。

```powershell
$baseline = Read-Host '已核验完整服务端对象基线目录'
$candidate = Read-Host '冻结集成 source.json'
$supplement = Read-Host '新增武器依赖 source.json'
$edges = Read-Host 'engine_edge_names/defaults.inc 所在目录'
python tests/csol-skull2/prepare_lifecycle.py --sdk $baseline --candidate $candidate --supplement $supplement --edges $edges --output build/skull2-lifecycle-check
python tests/csol-skull2/run_lifecycle.py build/skull2-lifecycle-check
```

如需补齐已有弹道 TU 等依赖，用 `resume_lifecycle.py --previous <前轮目录> --manifest <仅本轮变化的冻结清单> --output <新目录>`。脚本复用前轮已成功且未变化对象；所有失败历史保留。真实购买测试使用独立 `purchase_sdk.cpp/prepare_purchase_sdk.py`，本报告的五组数字不包含尚未核收的购买结果。
