# 原版实体伤害来源契约回归

验证 `CCSOLScenarioPlayerRuntime::ReadEntitySource` 的通用来源规则：已验证武器保持真实 ID，显式投射物适配优先，原 `CGrenade` 家族返回 4，其余基类/地图实体返回 0。未验证的武器实现仍拒绝，不能落入默认 0 分支。

夹具使用实际 ReGameDLL SDK 导出创建烟雾、可破坏物、触发器、门、列车、炮台和手雷家族对象。通过显式模板实例化访问原私有方法，调用真实 type22/23 `ApplyDamageCallback`，不替换生产方法体、不添加测试入口。引擎服务及外层 `DamageHost` 为受控边界。

```powershell
python tests/csol-character-source/run.py `
  --baseline build/csol-melee-buy-20261104/server-skill7-full-01 `
  --edges build/csol-boss-lifecycle-tests-20261030/compile-03/sdk-edges `
  --output build/csol-character-source-new
```

输出目录必须不存在。执行器从已部署基线取得完整源/对象，只将工作树中的 `ReadEntitySource` 方法替换进冻结运行时 TU；烟雾实体保持原版实现。分别链接旧运行时对象和候选对象，两版都必须成功编译链接。旧版必须复现指定来源拒绝，新版必须全部通过；崩溃或未知引擎边界异常不算有效反例。

2026-11-04 记录位于 `build/csol-skill7-20261104/source-contract-sdk01`：新版 352 项、0 失败，旧版同 352 项、51 失败。完整运行时 TU 与夹具分别记录 667/640 个实际 include，无未知或漂移输入。候选运行时 SHA256 为 `c681a9885baa662f00a6dfbe7c45a1f56b4b028af20785c7215c34cd8faa2de7`。

这证明实际 SDK 来源读取及隐身伤害回调行为，不覆盖真实地图伤害调度、网络、画面或从施放到冷却后重放的完整技能周期。完整 DLL 构建与游戏验证需另行记录。原 MP 审计和交付状态见 `docs/2026-11-04_修复-技能23伤害来源拒图-report.md`。
