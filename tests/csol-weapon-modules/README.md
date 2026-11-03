# 客户端武器模块注册回归

本目录验证注册架构的行为保持，不重复推导武器伤害或时序。生产注册器、八个真实模块和公共 controller 独立编译；SDK 引擎回调由夹具控制。

## 运行

使用已验证的 SKULL5 SDK02 输入、注册重构冻结快照、匹配宿主客户端随机函数和 x86 Clang 工具链。运行器只读原始游戏 DLL，不会加载或启动它。

```powershell
python tests/csol-weapon-modules/prepare.py --sdk build/csol-skull5-integration-20261103/sdk-02 --after build/csol-client-weapon-registry-20261103/source-03 --parent-after build/csol-client-weapon-registry-20261103/source-03/after.json --output build/csol-client-weapon-registry-20261103/sdk-fresh --native analysis/csol-model-20261017/shared-random-native-20261103/client.dll
python tests/csol-weapon-modules/run.py build/csol-client-weapon-registry-20261103/sdk-fresh
```

输入目录及原客户端不随源码发布；命令中的参数应指向本机合法持有的匹配材料。候选输出必须不存在，所有命令单 job，首次失败保留结果。`--reuse-build` 仅供同一冻结候选的夹具修正续测；使用它必须另核编译日志、对象和全部实际依赖，不能以对象文件存在作为验证。

## 覆盖

- `registry.cpp`：真实注册器的乱序排序、多 ID 映射、重复/非法 ID、重复顺序、初始化拒绝的原子性；固定表查询和生命周期的零堆分配观察。每个非法输入独立进程，避免错误锁存相互污染。
- `client.cpp`：八个实际模块静态构造在 ThinLTO 后保留，十个映射 ID 加内联飞刀、普通武器；通过编译器函数入口观测验证 Initialize、Reset 两阶段、Video、Receive、Predict 和 Diagnostic 的实际顺序。
- 新旧版本使用同一组输入，对照可靠消息、`cl_lw` 开关、静止、主副攻击、换弹、冷却、预测重放、切枪、弹药 HUD、准星绘制参数和地图清理；对照输出必须逐字节相同。
- 另注册第九个合成弹夹模块，不改 controller，验证 ID 查询和 RGB(17,37,57) 传递到数字、分隔线与图标。
- `dll_registry.cpp`：完整 DLL 构建完成后，用其匹配 PDB 提供 Registry RVA、字段偏移及结构尺寸。正常加载本项目 DLL 两次，读取 CRT 静态构造后的表，检查所有模块和 1024 个 ID 映射；不调用引擎或游戏初始化。参数顺序为 DLL 路径、Registry RVA、ordered/count/invalid 偏移、Registry 大小、WeaponModule 大小，禁止沿用另一 DLL 的偏移。

引擎回调是受控 SDK 接口，绘制只捕获参数；这不等于实际游戏画面、完整 DLL 启动、网络同步或多人验收。宿主随机函数使用已有匹配原函数字节夹具，不重新实现一份随机算法。
