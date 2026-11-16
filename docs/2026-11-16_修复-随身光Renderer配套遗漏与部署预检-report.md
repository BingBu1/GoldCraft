# 随身光 Renderer 配套遗漏与部署预检

时间开关和随身光 CVAR 的即时消费已在合并版实际 GameDLL 回归通过。本轮进一步查到：本地 B 已装新 CSOLSupport，但 Renderer 仍是旧版，无法提供随身光接口。现已给停止中的 B 原子更新 Renderer DLL 和匹配 PDB，并为后续部署、候选计划和客户端打包增加配套预检。未重启专服、启动客户端或执行界面输入；实际夜景和多人画面仍待验收。

## 范围与依据

这是用户指定的客户端随身光扩展与宿主部署适配，不改变 CSOL 原版玩法、灾变 AI 或绝命档案还原逻辑。私有操作记录和恢复验证脚本位于 `analysis/custom-gameplay-integration-20261116/`。

| Evidence | 来源 | 已证事实 |
| --- | --- | --- |
| E-001 | 本轮只读进程／匹配 PDB 检查；`validation.json` | 本地专服实际加载 MP `f9a25326…`，B 曾实际加载 CSOLSupport `dec72188…` 和旧 Renderer `637f2066…`。CSOLSupport 的 `nightPrepared` 为假、`nightRenderer` 和 `nightLights` 为空。B 随后退出；未因读进程失败而推断游戏崩溃。 |
| E-002 | 实际 SDK 的 `IMetaRendererMovingLights.h`、Renderer `gl_scene_lights.cpp` 与 735 项构建输入封存 | CSOLSupport 请求 `MetaRendererMovingLights_API_001`，Renderer 的对应工厂注册使用同一版本。旧实际 DLL 缺少此标记，新候选包含它；735 项输入及两个产物逐 SHA 保持。 |
| E-003 | `build/custom-gameplay-integration-sdk-20261116-02/receipt.json` | 使用最新合并 MP 的真实链接对象，27 组全部通过，270 项行为／41,808 项 SDK 检查。包括控制台即时覆盖、M 原子保存、同图开关、调时、生命周期与无效配置。 |
| E-004 | `build/csol-moving-light-capabilities-20261116-03/receipt.json` | 14 项真实 DLL／包成员检查通过，涵盖旧／缺 Renderer 拒绝、正确配套通过、旧客户端兼容，以及包中缺件、替换、重复模块。 |
| E-005 | `build/custom-gameplay-renderer-update-20261116-04/receipt.json`、`validate-only.log` | 原已发布的资源刷新检查接受缺 Renderer 的组合；新真实 Windows `ValidateOnly` 在写入前拒绝同一组合，六项运行文件身份不变。完整服务器 HUD 闭包 319 项通过。 |

F-001，`status=validated`、`severity=n/a_re`、`confidence=high`，依据 E-001／E-002／E-005：仅核 CSOLSupport 的资源刷新提供方不足以保证随身光配套。旧 Renderer 可以继续显示普通 HUD，同时使新增灯池无法准备。新增检查属于冷部署路径，不增加游戏绘制、输入或逐玩家处理成本。

P-001，`path_type=callflow`，依据 E-002／E-004／E-005：候选 CSOLSupport 所需接口标记 → 本次实际安装或包成员中的 Renderer → 缺件／旧版时拒绝 → 通过后才允许既有部署或打包。静态标记不等于已执行工厂调用；实际初始化、GL 阴影和画面验收保持独立。

## 源码与验证

`tools/csol_resource_capabilities.py` 增加配套检查及包成员检查，保留既有资源刷新验证；`tools/Deploy-CSOLSupport.ps1` 按实际插件链选取 Renderer 并在写入前检查。`tools/Build-CSOLCandidate.py plan` 校验候选搭配，允许显式 `--renderer` 纳入 DLL／PDB 更新计划。`tools/client-installer/package.py` 在计划和发布前检查实际配套，防止只在生成计划时检查、随后换成旧 Renderer。

`tests/csol-resource-capabilities/run.py` 增加实际 Renderer 正负例及包成员回归。Python 语法、Git 差异格式和真实 Windows 拒绝路径通过。原 GameDLL 27 组使用当前生产链接对象，SDK 的引擎／实体／媒体边界受控，不等于实机对局。

从 Git Bash 在项目根目录复现配套检查：

```bash
python -X utf8 -B tools/csol_resource_capabilities.py \
  --consumer sandbox/cs-client-b/Half-Life/cstrike/metahook/plugins/GoldCraft.dll \
  --candidate sandbox/cs-client-b/Half-Life/cstrike/metahook/plugins/CSOLSupport.dll \
  --renderer sandbox/cs-client-b/Half-Life/cstrike/metahook/plugins/Renderer_AVX2.dll
```

该命令只读取文件。将 `--renderer` 指向本轮回滚目录中的旧 DLL，可复现缺少随身光接口的拒绝；不需要覆盖游戏文件。

首次规则驱动在通信内核重置后丢失 stdout，编译和真实对象链接均已成功，但行为未执行。原失败回执保留；恢复脚本先逐项验证旧链接产物和输入，再执行全部 27 组，没有重新编译或把原失败改标成功。离线更新准备另保留了可选旧 PDB 不存在的夹具错误，以及 Windows 子进程缺 Python 命令解析的两次失败；补正确 PATH／PATHEXT 后真实 `ValidateOnly` 和最终更新通过。这些失败均未修改 B 的运行文件。

## 本地状态与剩余工作

B 的 Renderer 已从 `637f2066…` 更新为 `b402198e7765a87fc444c206649111d9f6e2f71b2178edc876f3231219e24f2a`，新 PDB SHA 为 `19a511d248a3e5d454b845829668eedac7b3d9c330f02f60fee10dbfb8279bbd`。已保留独立回滚，两个文件经独立临时文件验证后原子替换；没有改写共享硬链接、客户端修复用坏资源、插件顺序或配置。CSOLSupport、GoldCraft、下载器和运行中专服保持。新管理员插件未在本轮替换。

这也更正此前“即时 CVAR 尚未部署”的过时状态：另一次合并部署已将相应 MP 和 CSOLSupport 放入本地；本轮补的是遗漏的 Renderer。正式远端服务器未操作。

仍需恢复界面后的随身光初始化、夜景／遮挡、死亡／重连／换图、多人互见、多分辨率与真实 CPU／GPU 验收。完整绝命档案疾跑、熟练度、盲盒和模型表面血迹仍未完成。昨天下载中断恢复与换图进度修复已有独立补丁及本地记录，模型加载造成的整体换图慢仍另行排查，不因本次配套完成而关闭。
