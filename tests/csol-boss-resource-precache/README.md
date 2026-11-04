# BOSS HUD 资源预缓存验证

本测试运行完整的生产 `CCSOLScenarioSession::Precache` 和匹配的 ReGameDLL SDK 对象，不复制资源登记算法。引擎边界由 SDK fixture 控制，配置、模型与导航使用隔离专服现有文件，8 张 BOSS 图片使用已解压原版资源的独立冻结副本。

```powershell
python tests/csol-boss-resource-precache/run_resources.py `
  --baseline build/skill-crash-20261104/server-world-sprites-full-01 `
  --edges build/csol-skill7-20261104/source-contract-sdk01/source/tests/csol-scenario-rules-session `
  --server sandbox/cs-server/Half-Life/cstrike `
  --common '<已解压的 common 目录>' `
  --output build/csol-boss-resource-precache-20261104/sdk-new
```

运行器要求输出目录不存在，并拒绝包含额外 Session 修改的候选。仅重编 Session 与测试 TU，其他生产对象来自明确的冻结基线；旧版使用基线 Session 对象。使用项目固定 x86 Clang、C++20、O3、ThinLTO、AVX2 和 precise 浮点，编译与链接串行执行，链接线程限制为 1。

验证范围：

- Combat 独立登记 5 张血条主图和 3 张能力背景；重复预缓存不重复登记。
- 逐张真实文件缺失与零长度文件拒绝，错误精确指向资源；失败会话不能激活或静默重试，武器状态失效。
- Selection 不读取或登记 Combat 专属 BOSS HUD 资源。
- 文件检查和登记回调中的嵌套预缓存、激活、清理被拒绝，外层操作正确完成。
- 文件检查和登记回调中地图权限被撤销时，立即停止后续资源操作，不发布可用会话。
- 正常清理后再次开图重新登记全部资源，旧武器状态不复活。

资源检查与引擎登记在生产代码中交替执行。若后续资源缺失，之前已登记的下载项是引擎副作用，测试不要求不存在的事务回滚；验收的是失败会话不可使用、缺失资源不登记、失败后不继续操作。

输出 `resources.json` 保存原版图片与冻结副本 SHA-256、尺寸；`result.json` 保存编译/include 审计和每例结果；`verified-inputs.json` 保存最终核验的源码、对象、工具链与实际资源。旧版必须仅出现 8 个缺登记断言，形成明确反例。

这属于真实生产代码与 SDK 测试，不代表已完成 HTTP 下载、DLL 部署或游戏内血条显示验证。后续仍需在仅有一个 B 客户端的实机流程确认首次下载与 BOSS 血条渲染。

2026-11-04 的 `build/csol-boss-resource-precache-20261104/sdk02/result.json`：新版 23 例、239 个行为断言和 26,015 个 SDK 检查全部通过；旧版同一正向用例精确失败 8 项缺登记断言，SDK 检查无失败。全部输入前后 SHA-256 一致。Session 候选 SHA-256 为 `fdc45da6e431394023cb0450692963cd708453b9eb81c0ab9fe46043d9b3187c`，仅包含 3 行 BOSS 资源登记修改。
