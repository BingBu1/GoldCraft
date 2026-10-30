# CSOL 武器灾变伤害自动选择

2026-10-30。修复黑龙炮、赤血龙骧、御天绝影在灾变规则下仍默认取普通伤害的问题。三把武器已有原版 Scenario 数值与服务端 JSON，但此前仅各自 `csol_*_damage_mode=2` 才选中；灾变规则没有自动设置这些逐枪 cvar。

现在共享 `CSOL_CurrentWeaponDamageMode` 每次攻击查询实际安装的 `CSOL_UsesScenarioRules()`：灾变直接选 Scenario；其他模式原样返回武器的旧选择结果。既有 cvar 名称、默认值、非灾变手动档位和 ZP 行为均保留，没有新增每枪 cvar，也不把请求中的模式配置或客户端角色当权威。Beam 的模式特效阶段复用同一选择函数。

## 已有武器到底适配到哪一层

| 武器 | 灾变基础伤害状态 | 本批结果 |
|---|---|---|
| cannon / 黑龙炮 | JSON 已有 DamageScenario=2950，旧默认取410 | 当前实际灾变规则自动取 JSON 的 Scenario 值；自定义值和显式0保留 |
| beamgun / 赤血龙骧 | JSON 已有11种攻击的Scenario字段，旧默认取Normal | 两变体、11种攻击统一自动选择；不改配置值 |
| beamgunle / 御天绝影 | 继承beam默认，再应用LE专属overrides | 自动选择后仍由原配置对象按变体查询，LE覆盖不污染beamgun |
| tknifeex2 / 战术飞刀 | 当前投掷70、近战35 | MP确认mode15本就使用70/35，无需编造额外基础倍率；本批未改飞刀 |
| CS原生枪械 | 当前仍使用ReGameDLL各枪基础伤害；命中灾变怪物时进入公共灾变伤害链 | 尚未建立所有枪的原版Scenario属性目录，不能称为原版基础伤害已全量适配 |
| K3 | 已有独立研究：Damage29、Scenario85 | 独立武器实现由另一任务负责；本批提供共享选择入口，不修改其源码 |

普通枪也已经进入 `scenario_monster_damage.cpp` 的公共升级、物品和地图处理；“进入公共灾变链”和“具有CSOL原版该枪Scenario基础数值”是两层不同验收。common的物品UI统计不可直接当真实伤害，K3已存在UI统计与真实getter数值不同的明确证据。

## 原版证据

复用项目复制MP：`analysis/csol-model-20261017/samples/mp.dll`，SHA256 `39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2`。只对已复制文件做有界字节读取/反汇编，没有运行原版DLL、启动或重开分析服务。

- 普通步枪getter `10AE2CC0` 先查询实际mode；`{15,17,48,59}` 分支取property+A8（DamageScenario），普通取+38、生化取+70。K3原属性工厂按mode查、无专用对象回退普通对象；这与伤害getter再次选择Scenario槽是独立步骤。
- Cannon getter `1106B4A0` 在同族模式中读取自身property+70的Scenario槽。现有Cannon JSON 2950来自已保存的属性构造证据；本批不改为统一倍率。
- TKnifeEx2 v308=`11221750` 只对显式生化mode集合返回`11D4AA60`的60；15不在集合中，因此取`11D4AA58`的70。v358=`11221850`固定读`11D4AA88`的35，近战入口已保存的调用读该槽；投掷源函数把v308值复制到弹体。不存在本批需要补造的飞刀Scenario基础倍率。
- 这些是复制版本的静态分派与默认属性，不证明在线调参、全武器目录或所有其他游戏模式完整一致。

有界窗口、原字节、常量、来源哈希：`build/csol-weapon-mode-20261030/original-evidence.json`；可重提取脚本同目录 `original_evidence.py`。

## 攻击升级与兼容性

新模块只返回模式枚举，不接受或乘算伤害。Beam继续从 `CCSOLBeamDamageConfig` 取值；Cannon继续使用已载入的四个JSON数值。`scenario_monster_damage.cpp:323` 原有 `call.damage *= attack` 保留在公共命中处理；墙体继续其原有独立升级公式。本批没有改上述两个消费者，也没有在枪械侧再乘攻击等级。

实际灾变规则优先于旧cvar的Normal/Zombie/Escape设置；这是本次自动选择的必要行为。离开灾变后，Beam原来的浮点精确档位、Cannon原来的int转换和ZombieMode兜底都保留。强制在普通CS中用旧cvar选择Scenario仍保持可用。

## 修改和验证

生产改动仅四文件：`native/csol/server/weapon_damage_mode.h/.cpp`、`wpn_beamgun.cpp`、`wpn_cannon.cpp`。独立测试：`native/csol/server/weapon_damage_mode_tests.cpp`。

- 完整 `wpn_beamgun.cpp`、`wpn_cannon.cpp` 和新模块通过实际ReGameDLL SDK、Clang x86/C++20/O3/ThinLTO/AVX2/precise编译。SDK旧有warnings保留，不等于警告清零。
- **1093项检查通过**：生产适配函数原文提取后按实际SDK编译；五轮普通/灾变往返、八种旧cvar值、Cannon普通/生化/灾变/逃脱分支、两Beam变体全部11攻击、LE自定义覆盖、显式0、无效重载保留与离开灾变恢复。
- 测试实际使用生产JSON解析器和生产共享模式函数。规则查询由离线fixture提供，未启动专服；提取的适配函数不是整个武器攻击、命中与死亡流程。
- 输出测试EXE核为PE32/x86。11份测试输入最终SHA复核无漂移；构建日志、命令、输入身份和结果见 `build/csol-weapon-mode-20261030/evidence.json`、`commands.json`。首轮链接工具选择/编码和SDK外部变量缺失失败已处理，失败日志保留。

仅静态审查确认倍率仍由原公共阶段处理，尚未重跑升级后实际命中/破墙数值。后续完整候选 `build/csol-menu-mode-candidate-20261030` 已成功链接并部署专服，B 已连接；三枪真实伤害、攻击升级和普通模式回归仍未完成，不能以构建/入服代替。

## 交接与剩余

父代理只需在 `native/server/GoldCraft.targets` 已有武器ClCompile列表增加 `weapon_damage_mode.cpp`。精确单行补丁见 `build/csol-weapon-mode-20261030/integration.patch`；不要把带main的离线测试文件加入DLL。本代理未改公共构建登记、SDK接点、共享四文档、Git索引或运行目录。当前文件身份及登记前SHA见 `handoff.json`。

后续实机在穷途末路核对三枪全部攻击、破墙和至少两级攻击升级，并切回普通CS/ZP比较。原生枪械属性目录需从对应MP属性构造/当前模式对象获得；对缺失数值继续保留原生基础行为，不能套统一倍数或猜UI数值。所有未移植武器也不会仅凭菜单名字自动获得攻击实现。

## 可发布复现入口

已将上述独立构建流程整理为 `tools/Test-CSOLWeaponDamageMode.py`，不依赖被排除发布的 `build` 脚本或反编译文件。它从当前生产源提取 Beam/Cannon 适配表达式，编译实际模式选择器、Beam JSON 解析/加载器及 `weapon_damage_mode_tests.cpp`；另单独编译完整两枪 TU 校验 SDK。需要项目已准备的 ReGameDLL/RapidJSON 依赖和 Clang x86 SDK 环境。

```text
python tools/Test-CSOLWeaponDamageMode.py --output build/weapon-mode-check --compiler path/to/clang-cl.exe --environment path/to/compiler-environment.json
```

`--environment` 可省略以继承开发命令行；JSON 只使用 `INCLUDE`、`LIB`、`LIBPATH`。`--validate-only` 仅校验参数、依赖和生产表达式提取，不创建输出目录、不调用编译器或测试。正式运行拒绝覆盖既有证据，保存源身份、提取片段摘要、命令、日志及结果；规则仍为离线fixture，Cannon四档值仍是fixture参数，不扩大原验证边界。

整理后的新脚本仅执行 `py_compile` 和6组参数校验通过，**没有重新编译C++或运行1093项测试**。本次校验记录：`build/csol-weapon-mode-20261030/runner-validation.json`。临时语法缓存和环境夹具已清理，生产源码、公共构建脚本及候选快照未改。
