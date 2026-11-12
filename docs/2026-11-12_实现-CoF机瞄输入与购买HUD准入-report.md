# CoF Glock 机瞄输入与购买 HUD 准入

本阶段补齐客串 Glock 的输入、购买卡片、弹药图标和资源准入代码。尚未部署或完成游戏验收，不能把组件检查或完整 DLL 编链当作整枪完成。

## 范围与原证

用户授权移植 CoF 武器，保留机瞄与推搡，制作缺失 P 模型，采用适合的 CS/CSO 人物动作，并自行分配“客串武器·”购买位置及灾变 JSON 伤害。原资源只读，生成资源仅进入新的构建目录。范围、原字节及修改前合同保存在私有 `analysis/cof-weapons-20261111/client-evidence/input-bridge-20261112/`；不发布原二进制、反编译正文或游戏资源。

| Evidence | 身份与位置 | 核对及复现 |
| --- | --- | --- |
| E-001 | 原 CoF PE32/x86 `client.dll`，SHA-256 `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`，基址 `10000000`；VA `10079DB4 → 10078A40 → 1007A780` | IDA 取注册、KeyDown 和按钮消费者；`10078670` 的 436B、`10078A40` 的 142B、注册窗口 560B 与样本完全相同，各窗 SHA 在私有 `before.json` |
| E-002 | 原 CoF `hl.dll`，SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`；既有 Glock `ItemPostFrame` 原指令 | 沿用已核整枪证据；按钮 `0x40` 进入机瞄，`0x800` 进入枪托攻击。主报告保存服务端完整链和验证范围 |
| E-003 | 原 `sprites/weapon_glock.txt` SHA `5115c52533be24e12dc1de4776baea91e6dab0e41e5b25b4d5cd6c798ea37cf4`，`cglock.spr` SHA `175cff5be62c7340ede984439f6ffb100d14e73c54f781a449d9974a85fb60c6`，弹匣图标 SHA `2638312b229c7a755a0ed6aa43c8b136fbc7988b749098ffa25e6cf8e3fafa35` | 生成工具校验完整 SHA、实际 640 图集坐标、SPR 格式与尺寸；这只证明资源身份，CSO 界面中的展示属于明确宿主设计 |
| E-004 | 真实宿主 `usercmd_t`、`FCVAR_USERINFO`、已有武器目录及 HUD/购买解析器 | 使用实际 SDK 编译输入与准入消费者，实际 JSON 经两个生产解析器读取；不以 fake Ready 代替真实运行后端 |

F-001：`n/a_re / reverse_algo / validated / high`，依据 E-001/E-002。原 `+attack3` 对象的状态由 `100786CC..100786E0` 转成 `0x40`；E/Use 为 `0x20`，右键为 `0x800`。对应 `native/csol/client/weapons/guest_cof_input.*`。新增独立 `+cof_aim/-cof_aim`，不覆盖原绑定；快速按下松开保留一次输入，双键按住、失焦、死亡、观察和切枪都有清理。真实键鼠网络传递仍待实机。

F-002：`n/a_re / other / validated / high`，依据 E-003/E-004。客串 ID 896 没有原 CSO 的汉化或武器图集记录。`Prepare-CSOLHud.py` 显式排除这一已知宿主身份；`Prepare-CryOfFearPresentation.py` 独立增加卡片、弹药和原生切枪图集，不改变原 CSO 其他条目或布局。购买名称和价格取已授权宿主目录；说明文字、方形 CoF 图像放入 CSO 卡片、170×80 到 170×45 的居中等比采样均属于宿主适配，不声称原版 CoF HUD 相同。

F-003：`n/a_re / other / candidate / high`，依据 E-004。`guest_cof_admission.*` 仅在连接代次、原生冷声音、枪烟、Studio/recoil、点光、枪口、机瞄 hook、输入以及 HUD 卡片均具备后，冷准备购买图并发布兼容版本；换图/视频/资源退役清除兼容状态。协议是兼容性发现，服务端仍独立核实体、身份、角色生命和伤害。此运行接线尚未实机验收。

P-001，`path_type=callflow`：E-001 原注册/状态位 → E-002 原武器输入分支 → F-001 独立宿主输入 → E-004 实际扩展目录与准入 → F-002 原资源卡片。跨玩家、换图、丢拾、实际瞄准灵敏度/原准星和最终画面仍是这条路径的未验边界。

## 复现

在 Git Bash 内，从项目根目录运行；`COF_RESOURCE_ROOT` 指只读原游戏资源目录。输出须是新的 `build` 子目录，不能覆盖输入或已生成候选。

```bash
python tools/Prepare-CryOfFearPresentation.py \
  --original-game-root "$COF_RESOURCE_ROOT" \
  --baseline sandbox/cs-server/Half-Life/cstrike \
  --output build/cof-presentation-check

python tests/cryoffear-host-admission/run.py \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --commands build/resource-bridge-20261108/csol/compile_commands.json \
  --presentation build/cof-presentation-check \
  --output build/cof-host-admission-check
```

独立命令已经注册后，玩家可自行把可用按键绑定到 `+cof_aim`；程序不自动改动 MOUSE3、E、右键或其他现有绑定。

## 已验证与成本

- 实际生产输入/准入 TU、SDK 声明和两个实际界面解析器：10,250 检查零失败，见 `build/cof-host-admission-20261112-05/receipt.json`。覆盖按下松开、按键重复、双键、失效、命令碰撞、十种准备条件、代次替换、冷准备内重连、非 owner 线程；服务就绪由受控回调提供，不等同于实际 Renderer 或网络。
- 新生成 8 项资源共 197,817B；所有输出 SHA、输入不变、原有 HUD/购买条目完整相等、CSO 导出分离均通过，见 `build/cof-presentation-validation-20261112-01.json`。SPR 透明像素的 RGB 可以是红色，alpha 为 0；未按截图猜测改色或抠图。
- 当前枪口桥 66 项零失败，使用真实 Glock P 模型附件，见 `build/cof-muzzle-bridge-20261112-06/receipt.json`。世界相位和服务就绪为受控边界，未宣称完整游戏画面通过。
- 首轮 fixture 的类型/返回 ABI 编译错误已修；第二、三轮错误替换实际 inline 资源门，导致一项拒绝用例失败。第四轮直接操作真实事务开关；第五轮补充 HUD_Frame 被事务门跳过时的即时输入失效检查，通过。旧收据保留。
- 输入和就绪热路径使用固定数组及现有缓存，不读文件、不上传纹理；界面生成在离线工具，购买图片在 HUD_Frame 冷准备阶段。10,000 次静止轮询只验证不重复发布/准备，不能作为实机性能结论。

最终完整客户端已编链并通过源码不变校验，SHA-256 `e662ee41273cf6f7b0fac5d3f9ace26f998b3cdeb5e70c3ec729779e663ed26c`（PE32/x86，5,211,648B），见 `build/cof-client-checkpoint-20261112-admission-02/receipt.json`。首轮客户端和服务端也均实际链接，但期间服务端 `weapon_registry.cpp` 的 include 与本轮客户端事务失效源发生改变，因此两份全域封存收据保留为失败候选，服务端正在稳定源码增量重建。本阶段提交是独立组件与接口，不激活公开 HEAD 的整枪目标；工作区完整 DLL 仍包含其他未发布接线，不声称公开 HEAD 可重建整批配套。

## 未完与接续

尚须验证配套实际后端、服务端购买及灾变 JSON 契约、弹药/重购/丢拾、CSO 男女握姿与动作、机瞄灵敏度和原准星、所有特效/声音、多人及真实 CPU/帧时间。其他 CoF 武器及缺失 P 模型范围保持；当前没有新增可玩验收数量。

主连续性文档的精确更新单独保留在本领域私有 `shared-document-updates.json`，等待共享编辑窗口；遵照用户要求，本次不向主调度发送非打包消息。2026-11-12 时间线：恢复核验 → 新输入 IDA 字节复核 → 客串资源/实际准入实现 → fixture 纠正并通过 → 完整构建与源码封存核验。
