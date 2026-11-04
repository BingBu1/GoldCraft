# 人物技能世界 SPR 预缓存回归

`resources_sdk.cpp` 执行完整生产 `PrecacheSkillHost`，链接已验证的 ReGameDLL SDK 对象，先建立实际 `Combat` session，再独立观察技能资源登记。资源从专服 `cstrike`、`valve` 目录读取；四张已知 Boss HUD 图从指定的解压 `common` 目录补足测试输入。引擎边界受控，`ModelIndex` 只查找，绝不偷偷注册模型。

验收内容：

- 原有二十个技能模型的实际索引及登记顺序保持。
- 五个经 `CL_LoadModel` 使用的世界 SPR 追加到模型表末尾，不再只登记 generic 下载。
- `sprites/csol/640hud12.spr` 仍以 generic 下载，技能初始化不为它增加模型索引。
- 每个新增世界 SPR 分别缺失、注册返回零或越界索引时，技能宿主初始化失败并指出准确资源。
- 旧生产对象在同一测试中必须恰好失败十项：五个索引缺失、五个仍只登记 generic；其他断言仍须通过。

`run_resources.py` 接收完整服务器构建清单、已有的准确 SDK 引擎声明、实际资源目录和新输出目录；冻结两处候选及测试输入，审计真正编译的 include 与复用对象，并保留旧、新结果。输出目录必须新建。示例：

```powershell
python tests/csol-character-skill-host/run_resources.py `
  --baseline build/<verified-server-build> `
  --edges build/<verified-sdk>/source/tests/csol-scenario-rules-session `
  --server sandbox/cs-server/Half-Life/cstrike `
  --common '<extracted-common>' `
  --output build/<new-resource-regression>
```

这是实际 SDK 函数及资源分类证据，不能代替完整 DLL 构建、游戏引擎下载/模型表和人物技能实机验收。
