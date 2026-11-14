# 永夜滴血月亮、体力HUD与踢击提示避让

用户要求永夜月亮显示血迹并持续滴血、体力有可视化HUD，同时报告F踢击提示被人物技能图标挡住。本轮自定义展示已接入源码：实际32帧循环GIF及同帧图集、服务端权威体力消息、可视化体力条，以及避开人物技能/购买提示/武器卡片的共同布局。是否已部署和真实游戏通过，见末尾检查点，不以离线测试代替。

## 依据与路径

这是用户明确指定的自定义表现，不冒称原CSOL的图标、布局或体力参数。实施前契约保存在私有`analysis/csol-challenge-ui-20261114/PREEDIT.md`；原体力、阶段和声音证据沿用已核身份的阶段体力报告、黄昏与永夜音频报告。本轮保留原扣体力/恢复时序和人物技能原布局。

| Evidence | Finding | Path |
| --- | --- | --- |
| 用户指定血迹月亮GIF、当前Codex API和本地认证、`gpt-image-2.6-sunburst` | F1：图像生成属于授权的素材制作，32帧/50ms为自定义动画规格 | 本地`local-image-api` skill → imagegen CLI → `Prepare-ChallengeMoon.py` |
| 实际GIF回读32帧、loop=0、每帧50ms、帧内容各不相同 | F2：动态图集来自真实GIF解码帧，512×256 RGBA只冷上传一次 | `phase_art.hpp`、`challenge_phase.cpp` |
| 现有权威`StaminaState/View`与真实PostThink/踢击扣款；实际SDK接到本人消息 | F3：HUD显示服务端原始amount/max及无限/耗尽标志，不由客户端计时恢复 | `server/custom/action_stamina.cpp` → `stamina_snapshot.hpp` → `stamina_hud.cpp` |
| 用户重叠截图；实际`abilities::State::Layout`为77×75，正常/提升两位置；`PaintBuyHint`居中330；`hud_style::MakeLayout`提供武器卡片区域 | F4：旧F提示`height-148`与正常技能图标交叠。新增显示位于中央技能/购买栏右侧，并按实际HUD缩放放在武器卡片之上 | `action_hud_layout.hpp`、客户端`human_kick.cpp`、`stamina_hud.cpp` |

P1音画路径：用户指定imagegen CLI生成带真实alpha源图 → 本地工具生成循环GIF → 回读GIF各帧形成TGA图集 → 服务器冷预载/资源计划/HTTP闭包只包含实际消费的图集 → 客户端冷解码/上传 → 绘制按单调时钟选择UV。GIF源文件供编辑/预览，游戏不下载或运行时解码这份源文件。

P2体力路径：真实已接受动作扣款 → 实际体力Tick/阶段容量 → 状态变化才向本人发送22B `CsoEnergy` → 校验life64/revision32/有限float/标志 → 显示剩余、上限、无限或恢复状态。死亡先退役，再发送隐藏；InitHUD强制重发已有权威，回调重入关闭运行环境后禁止继续发送。普通相同恢复等待帧不重复发包。

## 资源与skill

本机已存在`local-image-api`，按用户要求更新为可复用imagegen CLI技能。它读取当前provider的API base path和本地认证，优先使用该provider的env_key或experimental_bearer_token，随后才用auth.json API key；不修改Codex配置，不把认证写入项目。模型固定为`gpt-image-2.6-sunburst`。skill-creator正式校验通过，generate/edit两路dry-run通过，8项认证选择/脱敏检查通过，实际生图请求成功；没有切换模型。

源图、提示词、实际GIF、图集和回执保存在本地`assets/generated/csol/blood-moon/`，由`.gitignore`排除。工具读取生产`phase_art.hpp`的帧数、尺寸、路径和时长，不在另一个配置中维护副本。图集512×256、524288B解码像素；GIF52829B、RLE TGA92813B。相关文件均有SHA身份；动态源文件不进入源码Git提交。

新动画的实际冷资源是`resource/goldcraft/challenge/night_blood_v1.tga`，使用现有TGA HTTP路径；不因源GIF扩大发送类型。部署预检在TimeCycle启用时额外检查它，地图资源准备和服务器包闭包也列入同一生产常量。背景、字体和完整旧HUD资源门继续保留。

## 验证层级

- 协议/实际布局：最终302项，覆盖22B消息、旧生命/重复序号/非有限值、无限/耗尽状态、动画循环、实际人物技能正常/提升布局，以及实际武器卡片0.5/1/1.5/2缩放。4:3、16:9、超宽、4K及更大视口均检查边界。
- 实际VGUI/GDI owner：阶段/月亮142、体力30、F提示23项；各自100热帧无C++堆分配、文件读取或纹理上传。体力不会在100次本地Frame中自行回升，死亡即隐藏，资源刷新保留权威状态，关闭释放字体owner。Surface和消息输入为受控边界，不是游戏GPU或实机FPS验收。
- 真实资源事务：27个owner入口，108项通过，体力HUD参与暂停、失效、重建与回滚。
- 部署资源门：8项通过，生产C++ `ParseCatalog/ResourcePaths`逐项对照，缺三张旧HUD图、错误尺寸、重复成员和开启新时间系统后缺图集均被拒绝。CPU调用采样用于去掉每文件重复GetFinalPathName，仍逐叶拒绝重解析点。
- GameDLL首轮失败保留：SDK夹具构造器先执行过真实初始化、随后清空观察包；以“当前帧必有初次包”计数的预期不成立。补真实force/InitHUD发布后旧13组通过；后续UI布局从协议头中分离、冷资源收窄为实际图集，按最终完整服务端封存重新运行，结果另附。没有放宽生产所有者/消息校验。
- 完整构建首轮VGUI源包含目录缺失、CMake重生成和实施中输入变化的拒绝回执均保留。只有最终输入稳定的完整候选可部署。

## 接续与限制

原恢复声音和永夜音乐的源码及历史双端/SDK证据保留；本轮新增GIF/体力条与F避让尚须配套实际部署、单B画面和听感。实体Escape已停止Computer Use，此后未发送界面输入。现存主测试服/单B的其他领域更新不被静默覆盖；部署前再次核对实际版本与对局状态。

完整CoF奔跑/真实跳跃消费者、时间系统光照/雾/人数刷怪等仍未完成；体力HUD不等于这些玩法验收。完整大灾变goal仍active。受保护安装四个核心模块与本轮前置基线逐SHA相同；不是全目录元数据审计。

## 最终构建与独立专服检查点

最终客户端`build/csol-challenge-ui-full-20261114-06/client/receipt.json`成功、575个实际编译输入稳定、资源刷新ON，DLL SHA256 `75153cb9e8c91071c00e418414800bbb96cbde163f5b4df22afad725adec6a33`。最终服务端full05成功、445真实链接输入封存，MP SHA256 `013778a66b9488b95520bec343f218cdadc5442efca13bd0fee4f506715756ae`。两端DLL/PDB独立封存在`build/csol-challenge-ui-sealed-20261114-02`。

`build/csol-challenge-ui-sdk-20261114-04/receipt.json`最终13组、142行为、19267 SDK检查通过且实际输入稳定。包括两种随机音乐结果、重连/重玩/全灭、男女喘息、体力数值/耗尽/恢复、100等待帧消息去重、InitHUD强制重发、退役隐藏和HUD消息回调撤销。首轮错误计数、构建中修改导致的拒绝均保留，不改成通过。

`build/csol-challenge-ui-cold-20261114-09/receipt.json`真实独立专服通过六项：灾变mode2、TimeCycle已启用、32槽、0玩家、原目录98的难度1..6就绪、实际三资源HTTP逐长度/SHA相符。图集92813B/SHA `4b8f9bf39c8ef08ee1e333d582ff0ae7ccd950d6ba06a5a97046a209a7e5e1e7`，男女WAV分别133250B和147238B。实际匹配MP/PDB只读确认Session Active、无错误；测试配置和DLL已恢复、测试服退出。没有操作主服或B的图形输入。

前面冷启动失败来自隔离服先前恢复脚本移除了新音频：显式配置mode2、TimeCycle和32槽不能证明灾变Session初始化成功。最终脚本先从逐SHA核同的九音频资源计划临时准备，再核真正Session/难度/HTTP；缺资源时继续保留失败，不让空配置通过。原始失败控制台回执保留。图集源GIF只作本地素材，未扩HTTP白名单。

用户报告F和技能重叠后，实际界面提示矩形检查新增23项，完整的302布局检查也比较了实际武器HUD缩放矩形。最新月亮142/体力30/F23三个UI owner和资源事务108均通过。现已准备16文件的本地双端/媒体/原生音频gamedata更新方案，尚未应用到当前主测试服和B；当前有人对局且实体Escape停止有效，保留单B重启/更新和真人画面、听感验收。

源码发布以窄范围保留已有其他领域改动。当前完整二进制和108项事务检查包含尚未整体公开的资源刷新域；本轮对该域的新增owner接线另存`patches/csol-stamina-resource-refresh.patch`，供其整体发布时核对。没有把其它地图/渲染/CoF在途改动混入本次窄提交，也未声称公开最小检出独立构建通过。
