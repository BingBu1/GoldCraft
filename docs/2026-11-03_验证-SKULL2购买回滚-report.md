# SKULL-2 真实 SDK 购买与回滚验证

2026-11-03，`purchase-sdk-08` 使用完整生产 ReGameDLL 对象和真实武器派生类，80 组共 **734 项行为检查、15,160 项 SDK 边界检查，零失败**。只对 `cbase.cpp`、`weapons.cpp` 和测试夹具做三个 TU 的增量编译与一次链接，没有重复完整 DLL 构建或部署。

## 证据与结论

| 证据 | 发现 | 处理 |
|---|---|---|
| `build/csol-skull2-20261103/purchase-sdk-04/result.json`、`remaining-diagnostic.json` | 56 组中 8 个直接释放新枪的案例未恢复旧枪，其余 48 组通过 | 保留反例，核查真实 OnFree/Attach 时序 |
| `external/ReGameDLL_CS/regamedll/dlls/cbase.cpp` 的 `OnFreeEntPrivateData` | 原清理通过 `pev->owner` 找玩家，而 Add 的 post hook 早于 `AttachToPlayer`；库存已插入但 owner 尚空，直接释放会漏掉原生 unlink | 实现方增加仅匹配活玩家指针的 `m_pPlayer` 回退，复用原 RemovePlayerItem |
| `external/ReGameDLL_CS/regamedll/dlls/weapons.cpp` 的 `DefaultTouch` | Add 回调可能释放或转移当前实体，原 Attach/UseTargets 尾部缺少再次确认 | 实现方增加 scope、EHANDLE、出生编号等失效检查 |
| `build/csol-skull2-20261103/purchase-sdk-08/result.json` | 80 组全部通过；三个实际 TU 无未知或变化包含文件，所有输入前后保持 | 修复后的生命周期与购买结果可交接，游戏实测另验 |
| `build/csol-skull2-20261103/purchase-tail-negative-02/result.json` | 仅换回旧 DefaultTouch 对象，保留新 OnFree 和相同夹具：11 项检查中唯一失败是释放后目标查询，SDK 195/0 | 确认尾调用检查能够识别旧实现，而非仅验证无崩溃 |

最终验证源：`cbase.cpp` SHA-256 `24fe18a237d7667e25928b40eab2a15e5d5e07b987ba6898615368e157a03e15`；`weapons.cpp` SHA-256 `b2dab18309521b139bd38b0878dc55e9582bb30b35f83f45d60bec4c775aab39`。`primary_purchase.cpp` 在本轮未改动；其完整生产函数来自已核验的集成基线。

## 实际运行范围

调用真正导出的武器服务 `IWeaponService::Buy`，执行 Spawn、DefaultTouch、AddPlayerItem、DropPlayerItem、CreateWeaponBox、PackWeapon、SelectItem、DefaultDeploy 和事务回滚。测试先运行实际武器预缓存、注册 ItemInfo/AmmoInfo，再经真实 ReAPI hook 注入各类插件结果。引擎模型、消息、文件加载等外部边界采用受控实现，不加载游戏 DLL 或地图。

四类原武器状态分别为 SKULL-2 Single、ToDual、ToSingle，以及使用不同 `.45 ACP` 弹药的原生 USP；分别替换为 SKULL-2 和 SKULL-1，共八种起始组合。每种组合执行十个独立进程案例：成功、部署拒绝、Add 前拒绝、Add 后拒绝、直接释放新枪、旧枪 Remove 前拒绝、旧枪 Remove 后拒绝、正常解绑后释放、转移给另一玩家、`UTIL_Remove` 延迟删除。

验证原实体身份、武器槽和所有者、私有模式、弹匣容量、切换 deadline、开火与待机计时、精度和射击计数。失败时检查所有备弹槽，防止旧弹药类型未恢复或无关弹药被覆盖。Single/ToDual 的实际丢枪先搬移七发备弹，再检验事务最终恢复；ToSingle 按其原分支处理。转移给第二玩家的枪必须保留，不能被回滚删除或夺回。

成功案例检验 SKULL-1/2 共享实际注册的 `50AZ` 弹药槽，同时与 USP 的 `45acp` 分离；实际 `BuyGunAmmo` 一次增加七发、只扣 70 金钱、保留已装弹匣。此处武器服务负责交付，不负责前端菜单定价；不宣称已经验证完整灾变菜单扣款、ZP Pawn 购买限制或真实玩家操作。

## 释放后尾调用检查

夹具在真正的 `OnFreeEntPrivateData` 已返回、实际私有内存释放前，将被释放新枪的目标设为独有标记；真实 `FindEntityByString` 边界记录该标记的查询。直接释放和先解绑再释放都要求该注入边界恰好到达一次，修复版不得继续走 `DefaultTouch` 的目标调用尾部。80 组中的对应检查均通过。这是实际调用链证据，不以“进程没崩溃”替代判断。

## 保留的准备与夹具失败

早期准备缺少两个测试专用 STL 头的登记，随后用已核验 SDK 清单核对并补齐。第一次执行尚未打开测试环境中的 `csol_weapons`，在预缓存资格处停止、行为检查为零；之后显式设置测试 cvar 为 1。后续 04 才是上述真实释放反例。05、06、07 仅为未执行准备版本，最终 08 采用两份修复源和释放边界计数，不能把准备结果记作通过。尾调用负例 01 的准备误用文件名后缀，替换到另一个对象而链接失败；02 改为精确文件名，仅替换目标对象，得到上述唯一行为反例。

源码入口为 `tests/csol-skull2/purchase_sdk.cpp`、`prepare_purchase_sdk.py` 和公共 `run_lifecycle.py`。需要已核验的完整服务端对象基线、生命周期 SDK 模板、生成的引擎边界声明及覆盖源码的独立清单：

```powershell
$baseline = Read-Host '已核验完整服务端对象基线'
$template = Read-Host '已核验生命周期 SDK 测试目录'
$edges = Read-Host '生成的 engine_edge_names/defaults.inc 目录'
$override = Read-Host '修复 cbase/weapons 的冻结 source.json'
python tests/csol-skull2/prepare_purchase_sdk.py --baseline $baseline --template $template --edges $edges --override $override --output build/skull2-purchase-check
python tests/csol-skull2/run_lifecycle.py build/skull2-purchase-check
```

输出目录必须不存在。完整窗口渲染、HTTP 下载、网络 delta 编码量化、真实服务器与多人行为仍在本轮验收之外。
