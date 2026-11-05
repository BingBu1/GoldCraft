# SKULL-1 客户端预测回归

`skull1_prediction.cpp` 编译完整生产 `client/skull1.cpp`，使用实际 HLSDK 类型和记录式引擎回调。原始预期来自身份校验过的 CSO MP 指令执行记录，不调用待测 `RapidRecoil` 来生成预期。

覆盖主攻击固定后坐力、300 组原副攻击后坐力上下文（每组执行正常预测和重放）、六次 CRT 随机消费、攻击和生命锁、空弹、`cl_lw 0`、右键释放后自动换弹、服务端或重放先进入换弹的动画补播，以及持续换弹不重复重启动画。断言同时检查预测输出和实际 `pfnWeaponAnim` 回调。

在已配置 Windows x86 SDK 的本地工作区运行：

```powershell
python tests/csol-skull-balrog/run_skull1_prediction.py `
  --baseline build/csol-skull-balrog-20261102/native-skull1-01/proposal.json `
  --oracle analysis/csol-model-20261017/skull-balrog11-20261102/skull1/core-oracle.txt `
  --old-source build/csol-skull1-review-20261104/pre-fix/skull1.cpp `
  --output build/csol-skull1-review-20261104/sdk-prediction-01
```

输出目录必须尚不存在。`--baseline` 提供真实 SDK 的编译参数和环境；不复用游戏对象文件。`--oracle` 旁须有同名 JSON 元数据，记录原 MP SHA-256 和正文 SHA-256。`--old-source` 是修复前的完整生产源，测试使用 VFS overlay 编译负例；预期新源退出 0、旧源退出 1。

2026-11-04 验证：候选完整源 SHA-256 `212a2b1ab1f2bd9cf5b11a138b3ed0b706c7edcbf850eb964114e882ef678a86`，8,664 项零失败；旧源 `24f45303f6d72cce9f4fc1e7fe12a923e5108555dc48f506652ff3c739821a48`，5,050 项中 1,847 项失败。检查总数不同是新源实际调用六次 CRT 回调，该回调自身也检查消费上限。两版各记录 501 个真实编译 include，无读取未冻结的工作区生产源。

随机值在本夹具中受控；宿主 CRT IAT 解析、完整 DLL、网络状态校正和实机画面不在该测试的证明范围。原 DLL、原指令执行记录及本地 SDK 配置不随源码分发。
