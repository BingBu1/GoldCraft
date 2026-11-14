# HUD测试服资源遗漏与部署前检查

本轮音频实测复用了旧的独立专服，漏验完整HUD资源，导致整套现代HUD退回原生样式。缺失的三张图片已补齐到测试服；客户端通过真实v4资源校验/缓存覆盖恢复，运行中HUD与金钱面板均已加载。未人工替换此前保留的客户端故障原件。

## 范围和证据

私有范围记录：`analysis/csol-hud-deployment-regression-20261114/scope.md`。这是宿主部署和本项目注册表修复，没有改写原版HUD布局、字体或颜色；既有原版取证见前一轮HUD回退报告。

| Evidence | Finding | Path |
| --- | --- | --- |
| E1：测试服完整目录256项，缺`hud_weapon_on_bg.tga`、`weapon_list_new.tga`、`weapon_selection_new.tga`；主测试服三项均存在 | F1（validated/high）：`PrecacheHudStyle`先校验整个闭包，任一缺失就不加入HUD下载；旧清单2820项不含这一完整闭包 | `native/csol/server/characters.cpp` → `hud_style::ResourcePaths` |
| E2：补齐三图后实际清单3076/3076校验完成，167个缓存覆盖对象，provider available=1/paused=0，无会话错误 | F2（validated/high）：原始错件已被正确内容覆盖，恢复路径真实执行 | 私有`build/csol-challenge-audio-live-20261114-02/receipt.json`及资源状态 |
| E3：匹配运行中DLL/PDB的只读状态，loaded=1、moneyLoaded=1，surface/schemes/extension均有效 | F3（validated/high）：当前HUD消费者及金钱面板已准备成功；此证据不等于新截图验收 | `hud-state-after.json`，SHA256 `be04d9f59162cbb4271deeed4e1be7ceaa14e21464b32812989ba19bf92a1702` |
| E4：第一机会C++栈指向InitializeWeaponModules；K3和待接入mountgun均order=30 | F4（validated/high）：此前完整客户端的CRT注册冲突导致InitHUD被捕获后主动退出；新机枪改为35，保留K3/近战相对顺序 | `client-eh.log`，SHA256 `c16a5714bf847e858866ce9060f582c5df693bf3800dbf67582080617cd29e2e`；`client/weapons/mountgun.cpp` |

P1调用路径：E1 → `PrecacheHudStyle`整体验证失败 → HUD不进入服务端清单 → 客户端VFS无法得到本轮权威覆盖 → 原始错图尺寸校验失败 → 保留原生HUD。补齐服务端闭包后，E2的权威校验/覆盖执行，E3确认两个HUD owner恢复。没有删校验或强制设置ready。

## 防止重复遗漏

新增`tools/csol_hud_resources.py`，从当前生产头文件读取目录、字体、方案和固定图片契约，完整枚举人物、武器、选中图和所有弹药帧；校验权威服务端每项存在、非空及固定图尺寸。拒绝目录穿越、资源重解析点和重复JSON成员。检查在部署前执行，不进入游戏绘制热路径。

`Deploy-CSOLSupport.ps1`新增目标`ServerInstance`，在复制DLL前运行此检查。`AGENTS.md`同样要求更新或将B指向任何测试服前检查该服完整HUD闭包；二进制provider标记与新增功能资源清单不能代替它。客户端原件可能由合法缓存覆盖，因此此门检查权威服务端，不误伤自动修复试验。

在Git Bash中复验：

```bash
python -X utf8 -B tools/csol_hud_resources.py --root sandbox/cs-server/Half-Life/cstrike
python -X utf8 -B tools/csol_hud_resources.py --root sandbox/cs-dione-probe/Half-Life/cstrike
```

这是完整性和固定尺寸预检，不能单独证明全部图片像素、字体或客户端VFS/GPU正确。运行中状态核对另列E2/E3。

## 编译和检查

修复候选CSOLSupport SHA256 `63b4c758fc66217dfc276d1bb0994ff6104cbe3ad04aa400db2249f913107c2e`，完整编译及真实依赖稳定，资源刷新ON。旧7ec候选的真实DLL注册检查2205项中2项失败，修复候选通过。注册检查使用匹配PE/PDB布局，覆盖实际CRT建表、有效唯一ID、严格顺序及byId一致性、卸载/重载，不再固定为历史八武器。mountgun整枪仍属未发布领域工作，本轮不宣称其原版玩法完成。

`tests/csol-hud-style/run_resource_paths.py`编译实际C++ `ParseCatalog/ResourcePaths`，与预检路径逐项对照；复现漏三图、补齐、错误尺寸和重复字段。最终`build/csol-hud-deployment-preflight-20261114-04/receipt.json`为6项通过，全部输入和实际头依赖稳定。首轮7项行为通过但主测试服目录被并发更新，输入稳定性失败已保留，没有改写成功。

真实CPU调用采样发现旧预检主要成本是每资源重复GetFinalPathName。现按共有父目录缓存验证、每个叶节点独立检查重解析属性，保持校验范围；原始和最终采样分别留在02/04回执。它是部署工具的冷路径采样，不是游戏FPS验收。

## 当前交付边界

独立测试服和单B已配套加载本轮候选，主测试服及远端正式服没有因此重启。新截图请求收到实体Escape停止，后续未再进行Computer Use；画面、男女实际听感及Rex自然串接仍待，后台loaded状态不冒充这些验收。

恢复声音原有源码/双端编译和12组SDK证据保留。用户新增的永夜滴血月亮GIF、真实体力HUD，以及当前API和本地认证的imagegen CLI skill另行继续，不因本修复标记完整TimeCycle或整个goal完成。
