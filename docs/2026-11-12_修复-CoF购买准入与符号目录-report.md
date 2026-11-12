# CoF 购买准入与符号目录修正

Glock 卡片可见但无法购买，实际客户端没有发布就绪能力。本轮修正目录配套与过严的音频门，已完成构建和本地部署；整枪玩法、声音听感、人物握姿、多人和实际性能仍需验证。

## 范围

承接 [完整 CoF Goal](COF_GOAL.md)，仅处理 Glock 实机接通时发现的准入阻断。授权范围为工作区隔离 B 客户端和本地专服，受保护安装保持只读。私有范围与修改前合同在 `analysis/cof-weapons-20261111/client-evidence/native-warm-20261112/admission-correction-before.json`，原二进制、反编译正文和游戏资源不发布。

## Evidence

| E-id | source_ref 与样本 | 内容与复现 |
| --- | --- | --- |
| E-001 | 原生 CS client，SHA-256 `b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2`；IDA `10048B12`、`1006BC09`、`1006BC30` | 原观察者和角度写入已有完整 IDA 记录，本轮重读；对应 `tools/Build-ClientGameData.py` 的原字节检查。真实 `NormalizeGlobal` 要求完整签名、指令和操作数字段；旧三个记录只有 RVA/VA。运行 `tests/cryoffear-catalog/run.py` 可重现三失败／新六通过。 |
| E-002 | 原 CoF client，SHA-256 `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`，基址 `10000000`；IDA `10071020` | `100710BD..10071218` 分派原生 EV_PlaySound，`10071247..10071273` 分派独立本地音频。没有用原生 PCM 驻留来决定本地后端或武器可用。新鲜反编译和原始字节保存在私有合同目录。 |
| E-003 | 当前宿主 hw.dll，SHA-256 `9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295`，基址 `10000000`；IDA `101FDC50` | `101FDCA2..101FDCB0` 按原 `fs_lazy_precache` 分支决定是否调用 S_LoadSound；有效 SFX 可以已注册、PCM 仍为空。与 E-002 共 764 字节重新逐 PE 核同，见 `admission-correction-byte-verification.json`。 |
| E-004 | `build/cof-live-runtime-20261112-01/current-resource.log` 与 `pcm-sample.json`，content_hash 见本地收据 | 只读调试确认资源 owner 为 NativeSoundUnavailable；同 epoch 的 23 个 CoF SFX 均已注册、尚未驻留 PCM。服务端实际收到 cof_protocol=0。读内存地址仅来自该进程匹配 PDB，未写内存或强改能力。此证据随会话失效，不把绝对运行地址用于生产。 |
| E-005 | `build/cof-catalog-resolver-20261112-02/receipt.json`、`build/cof-resource-owner-20261112-lazy-02/receipt.json` | 实际 x86 MetaHook 解析器正反例通过；实际 SDK/XAudio2 的 14 原 WAV、1241 检查零失败，输入前后核同。测试命令见对应 tests 目录。 |

原始私证的 content_hash 可由修改前合同及字节核对文件复算；报告不分发原始内容。运行证据不能在另一 PID 或另一二进制中直接重用。

## Findings 与实现

F-001：`n/a_re / other / validated / high`，依据 E-001/E-005。摄像机目录不符合真正的解析器合同，修复 `tools/Build-ClientGameData.py`，补齐已证签名、指令长度和操作数位置。目录只在冷初始化读取，绘制和输入路径没有新增查询或分配。

F-002：`n/a_re / reverse_algo / validated / high`，依据 E-002/E-003/E-004/E-005。旧 owner 将远端原生 PCM 驻留错误地当成本地音频准备及购买前提，现撤回该未经原版支持的跨后端限制。`native/csol/client/weapons/guest_cof_resources.cpp` 仍核对本连接真实 signon 表的完整注册；本地 WAV 继续真实解码并预建 XAudio2 voice，坏数据、缺失注册、退役和过期代次仍拒绝。更新 `guest_cof_audio.hpp` 的合同说明。

没有静音播放、修改 fs_lazy_precache、改写原生缓存头或强设 cof_protocol。原生引擎保留自己的延迟加载语义；远端首次播放是否存在实际热点仍须采样，不能把本轮组件检查说成性能通过。

F-003：`n/a_re / other / validated / high`，依据 E-004 与配套收据。更新 DLL 必须同时交付其匹配符号目录：B 起初缺少新的 CL_CheckClientState 目录，导致 PrecacheReady API 不可用。重新生成的引擎目录验证 130 个原跨度／152 符号，已经本地部署。

## Path 与验证边界

P-001，`path_type=callflow`：原 PlaySFX 的本地／原生分支（E-002）→ 宿主原生注册及延迟缓存（E-003）→ 真实就绪查询（E-004）→ 原生注册与独立本地 PCM 准备（F-002）→ 客户端能力发布 → 服务端人物准备与购买。最后两步及完整整枪操作继续实机核验，不以卡片显示作为购买成功。

完整客户端 `fc813c0299e582d691fbf25c89ffe9f7dbe22fb9d901951c09b65472b6880ae8` 已链接并以输入无漂移收据封存，见 `build/cof-client-checkpoint-20261112-lazy-01/receipt.json`，已部署到本地 B。此 DLL 来自当前混合工作区，不表示公开 HEAD 已包含全部整枪运行接线。

时间线：核对配套并部署首轮 → 原生目录解析失败与旧目录缺口 → 用户报告卡片不可买 → 匹配 PDB 只读定位音频门 → 重新取原 client/hw 证据并在修改前核字节 → 纠正门、完整构建、实际音频回归 → 部署并继续实际购买。

首轮音频回归的四个失败来自新增成功用例提前产生读取，破坏后面“累计读取为零”的夹具前提；移动该成功用例后 1241 项通过。失败记录保留，生产代码没有为夹具改变。目录首轮测试签名字段类型使用错误亦保留并只修测试。尚未关闭任何整枪验收项。
