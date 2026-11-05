# 普通僵尸盾盒与真实子弹链回归

这个夹具把真实 ReHLDS Studio hull/clip 函数与完整 ReGameDLL 对象分开链接，通过 `bridge.h` 的标量接口传递命中结果。它验证普通僵尸尾部盾盒排除能否从真实碰撞一路进入实际 `FireBullets3`、普通怪伤害所有者、HP 和血效消息，并保留旧版本火星且不扣血的反例。

## 输入与执行

在项目根目录运行。完整服务端构建必须已经成功；参数所指目录包含其冻结输入、对象、编译环境及构建回执。`--old-sdk` 必须是修复前的完整构建，不能将新类头与旧对象混用。输出目录必须不存在。

```powershell
python tests/csol-monster-shield/prepare_server.py `
  --sdk build/csol-skull1-review-20261104/server-full-03 `
  --old-sdk build/csol-melee-regression-20261104/server-full-01 `
  --output build/csol-skull1-review-20261104/shield-sdk-repeat `
  --engine build/csol-skull1-review-20261104/shield-engine-03 `
  --models "G:/New NARTOOL/CsolTools/pak解压/UnCsoPak/lstrike/common/models"
python tests/csol-monster-shield/run_server.py build/csol-skull1-review-20261104/shield-sdk-repeat compile
python tests/csol-monster-shield/run_server.py build/csol-skull1-review-20261104/shield-sdk-repeat link
python tests/csol-monster-shield/run_server.py build/csol-skull1-review-20261104/shield-sdk-repeat run
```

`prepare_server.py` 冻结测试源、实际模型、双份匹配头文件映射和编译器依赖。执行器检查实际 include 身份及输入漂移，并逐步写出 `compile-result.json`、`link-result.json`、`run-result.json`。旧版程序预期返回 1，但只有同时发现盾标志断言和 `hp=1000->1000 blood=0 sparks=1` 时才把它记为有效反例。

若要重建引擎侧，运行 `prepare_engine.py <新的 build 子目录>`。它从本机已匹配的 ReHLDS 源抽取 16 个原函数，生成 `plan.json`，其中包含需顺序执行的编译、链接命令和环境。不得混合不同构建的引擎对象、头文件或 GameDLL 结构体。

核对该引擎夹具的实际头文件与原构建输入：

```powershell
python tests/csol-monster-shield/audit_engine.py build/csol-skull1-review-20261104/shield-engine-03 --baseline build/csol-hd-http-unified-20261031
```

它比对 `/showIncludes` 的全部实际文件与被冻结的源及外部依赖清单，输出 `include-audit.json`；未知路径或 SHA 漂移会失败。

## 验证范围

- 8 个实际普通/精英模型：4 个 21 盒、4 个 20 盒尾盾布局；测试躯干/手臂与头部。
- 同一模型的玩家盾牌命中保留，奥伯恩 28 盒不删减。
- 真实工厂出生、实际子弹循环、实际普通怪 HP 更新及 `CsoMonFX` 血效消息。
- 配置完成的人类玩家持实际 `weapon_skull1` 实体的伤害来源处理。
- 同一池对象普通怪→奥伯恩→普通怪的模型切换。
- `GET_MODEL_PTR` 回调撤销会话访问、改变模型索引或删除实体时取消状态写入。

骨骼矩阵、动画序列、射线起点、散布及资源存在边界受控。引擎碰撞读取原模型的真实 hitbox；GameDLL 动画边界使用既有受控序列夹具。此测试调用公共 `FireBullets3`，不驱动噬魂 `PrimaryAttack`/`SecondaryAttack` 的完整武器时序。它不验证真实动画姿态、客户端血效绘制、GPU、网络下载、真实专服进程或多人玩法。实际源码与模型 SHA 保存在对应构建回执中；游戏资源及原版二进制不得发布。
