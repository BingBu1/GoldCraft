# 人物技能取消后的按键恢复

测试从实际 `scenario_active_skill.cpp` 提取完整 `GateSnapshot` 方法，链接生产的技能激活、效果时钟、技能目录、协议编码和客户端输入代码。动作播放、时钟、角色生命周期由夹具控制；没有运行完整引擎或替代游戏实测。

在项目根目录运行：

```powershell
pwsh -NoProfile -File tests/csol-character-skill-gate/run.ps1 `
  -OutputDirectory build/csol-character-skill-gate-new `
  -OldRef c4c3b79
```

输出目录必须尚不存在。脚本使用项目配置的 x86 Clang、C++20、O3、ThinLTO、AVX2 和 precise 浮点；冻结引用源码，运行当前方法与指定 Git 版本的旧方法，并编译实际服务端 TU。旧方法必须构建成功、运行失败且命中现场角色 133 的“永久 Preparing”反例，编译失败不算反例通过。

覆盖动作取消前后、原冷却到期、下一次激活覆盖旧时间戳、严格延迟边界、实际客户端请求、55 类技能权限、死亡/场景目标/FrozenWrath 拒绝、宿主失败与角色退役。保留原版取消不清 pending、不返还冷却、取消不启动效果的语义。

2026-11-04：当前源码 741/0，旧方法同 741 项中 155 失败；生产 TU 编译通过。证据在 `build/csol-skill7-20261104/gate02/result.json`。完整 DLL 和实机由父任务统一执行。
