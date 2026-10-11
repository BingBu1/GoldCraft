# NightFall / Avalon 动画资源检查

当前80条“缺少 constructor”日志不能等同于80个动作丢失。独立真实专服扫描另找出3个已注册但缺文件的条目；当前包内没有找到它们的实际播放入口。没有修改或删除 Mod 资源，也没有屏蔽错误。

## 证据与结论

范围为 Minecraft1.21.1 / NeoForge21.1.256、Epic21.17.3.1、NightFall3.4.0及其内嵌Avalon21.12.6.2，Invincible21.15.8.2。使用已部署 GoldCraft 核心 `c8bfdbdb9e7f8036c1eb47829de1bd8681229aaa07bacddf4ea2354e3dc2da86`；原 CS 安装只读，不接入 B 或原生服务器。

| 证据 | 检查结果 | 判断 |
|---|---|---|
| E-01：实际80条日志对应的JAR文件 | 32份仅含shake/blur等特效配置；48份原始clip。全部没有constructor | 不能作为资源包动画单独注册 |
| E-02：真实FML注册表及clip/姿态加载 | 共810项；检查460项NightFall/Avalon，457项加载成功，3项因文件不存在失败；80个报错ID均未注册 | 加载错误与资源扫描日志是两组问题，整次诊断返回失败，不能记作全部通过 |
| E-03：精确类字段引用与实际字节码 | 缺文件条目的使用见下表 | 未发现当前包内玩法会播放这3项的入口；动态第三方调用仍可能触发 |

F-01（validated，n/a_re，high；E-01/E-02）：Epic在重载时扫描所有动画目录JSON，将不在注册表中的条目送入资源包动画解析；缺constructor则记录错误并返回。P-01：资源扫描 → 过滤已注册动画 → `readResourcepackAnimation` → 缺constructor日志。此路径不代表已注册动画被删除。

F-02（validated，n/a_re，high；E-02/E-03）：三个注册条目指向包内不存在的文件。P-02：真实 `AnimationAccessor.get` → `getAnimationClip` → `JsonAssetLoader` → 文件不存在异常。

| 缺文件条目 | 当前包内实际使用 |
|---|---|
| `efn:biped/falchion/falchion_walk` | 字段只在注册类写入，没有其他精确字段引用 |
| `efn:effect/crimson_slash_anti` | ScytheSkill只比较此动画身份；反向猩红斩的VFX管理器实际播放存在的`crimson_slash`，区别是纹理 |
| `epic_fight_avalon:vfx/empty` | 字段只在注册类写入，没有其他精确字段引用 |

次元斩字段 `DMC5_V_JC` 实际使用 `efn:biped/yamato_judgementcut_end`，该clip加载与姿态采样通过；报错的`biped/dmc5_v_jc`是另一资源。长枪充能同样注册为`nf_meen_charge_all`，并非报错的旧分段文件。

另有92项普通待机/移动动画在专服返回空关节表；实际Epic加载器明确省略非主动作/攻击的专服变换数据。这不等于客户端动画为空。365项有非空关节数据；客户端全部关节、画面、重载和多人同步不属于本次专服检查。

## 复现

工作区准备上述精确Mods、NeoForge运行时和已部署核心后，暂停性能测量再运行：

```powershell
python tools/Test-EpicAnimations.py --name animation-audit
```

输出位于`analysis/epic-animation-tests/animation-audit-result.json`。当前版本预期报告3项缺文件并返回非零；这是诊断结果，不应添加空动画或忽略断言使其通过。`--candidate`改用构建后的核心；`--warning-log`可指定待核对的客户端日志。没有日志时仍执行注册表检查。

本次原始证据：`analysis/epic-animation-resources/registry-01-result.json`、`resource-content-audit.json`、`missing-field-refs.json`和匹配原JAR的javap输出；不公开第三方资源、字节码或游戏日志。

## 后续

当前没有证据支持修改这3项动画的播放行为。保留缺文件诊断，在新增技能或Mod实际引用它们时修复真实调用链。原有80条日志的分类已经核实，用户报告的动作/绘制问题仍需按实际技能单独验证。
