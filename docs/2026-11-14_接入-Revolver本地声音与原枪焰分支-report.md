# Revolver 本地声音、Studio 与枪口分支

左轮的四个本地声音、实际视模 Studio 消费者、P 模枪口绘制和冷资源准备已接通。原指令对照查出了直接复用 P345 第一人称枪焰大小的错误；该候选未部署，现已按原分支修正并重新通过对照。完整客户端已构建封存，左轮购买仍未开放。

## 原证与源码

匹配原 CoF client.dll：PE32/x86、基址0x10000000、SHA-256 `d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00`。原 MP SHA-256 `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63`。私有修改前映射、原函数、四字节窗核同在 `analysis/cof-weapons-20261111/revolver-integration-20261113/muzzle-and-cold`。

| 原证 | 行为与实际源码 |
| --- | --- |
| Studio5004 原1004CAEB明确包含原21；原模型5004及本地音频消费者 | `native/guest/cof/client/resource_groups.hpp` 的左轮四声音组；`guest_cof_resources.cpp` 冷准备真实本地PCM，并要求独立原生退壳声音消费者存在 |
| 10052AA0 锁存原事件owner/type；1008DAD0 在真实P骨骼阶段消费 | `guest_cof_muzzle.cpp` 按实际原ID/视模/P身份处理左轮，保持一个覆盖锁存与原附件0，使用实际光照骨矩阵 |
| world10090130；只有原type3走10090554的额外壳模型尾分支 | 左轮world复用已证default的base3.0；不额外抛Glock弹壳 |
| local1004BC30、1004BCEC..1004BCF8 的原21分支 | 左轮local使用base6.0，再与RNG按原double→float次序相加、乘scale；`RevolverMuzzle` 明确区别P345/Glock默认base4.0 |
| 实际SDK/模型的附件界限、原GetViewModel/当前武器分支 | `guest_cof_effects.cpp`、`guest_cof_muzzle.cpp` 冷存真实模型指针和附件；缺失武器组不借另一枪句柄，热路径不查资源；`catalog.hpp` 使用独立原ID→宿主目录映射 |

最初只审world时错误地将local也认作默认4.0。新加原21/真实原模型名回放后，92场景不符；修改前重新读取既存完整IDA函数及1166字节窗，与原PE完全相同，确认原21确实写6.0。撤回记录与新映射在 `local-size-correction-before.json`；原失败收据保留，无DLL部署。没有修改原Glock/P345的默认大小、光照、RNG或壳逻辑。

## 实现和成本

共用固定冷资源目录包含明确的武器身份及声音组，保留既有P345/Glock声音准备次序，追加左轮。模型查找在回调前后检查连接/资源代次；实际持枪身份变化时缓存目录下标。Studio/枪口事件只读取已准备句柄和固定附件，保留线程及生命周期保护，无热VFS查询、网络、堆分配或新计时器。声明的全部P附件仍在冷准备中检查，左轮仅变换实际被消费的附件0，Glock仍变换两个。

原生声音Registered与本地PCM分别验证，不能把lazy原生缓存误作缺少本地声音，也不能在缺少退壳消费者时开放左轮。这里的冷资源成功不是全枪购买准入；客户端购买循环目前仍只开放已完成的两把枪。

保持C++20/O3/ThinLTO/AVX2、precise和FP contraction off。本批只记录静态成本与实际SDK/原指令正确性，游戏CPU/分配/I/O与前台FPS采样未完成。

## 验证

- `build/cof-revolver-muzzle-original-20261114-03`：原P345/Glock组1145、原Revolver/Glock组1145，完整输出逐位零差。包括随机/极端时钟、光照池及浮点边界。旧左轮92差异保留在01；02修正后通过，03显式关闭FP contraction后再通过。
- `build/cof-revolver-cold-muzzle-20261114-04/receipt.json`：157实际SDK检查零失败，实际Glock/P345/Revolver原P模型分别核SHA；真实StudioDrawPoints、光照/几何矩阵区分、左轮local大小6、world无额外壳、缺失组/错身份/重入退役/跨连接及全部声明附件损坏拒绝。
- 原枪口夹具01漏较新附件观测接口；02断言误把该独立观察器合法使用的几何矩阵查询算作枪口查询。仅补边界和计数，枪口位置始终用实际光照矩阵，失败记录保留。
- `build/cof-revolver-cold-effects-20261114-02/receipt.json`：274实际SDK/真实XAudio2准备PCM检查零失败，包含四个左轮5004逻辑key、5006、错误模型/武器和连接失效，原手枪和x86后坐阶段回归通过；不是声音听感验收。
- `build/cof-revolver-cold-resource-owner-20261114-03`：既有实际冷资源owner/GL、2476原贴花场景通过；新增仅左轮声音组、缺独立壳声消费者在读取文件前拒绝。01夹具缺目录枚举声明，02补齐通过；03保持明确FP contraction off后复验。
- `build/cof-revolver-catalog-20261114-01/receipt.json`：198899协议/目录检查零失败，独立原ID不能与宿主或其他ID混用。

发布前实际输入审计在 `build/cof-revolver-cold-publication-20261114-01`。真实源/SDK均保持；枪口专项使用的生成compile_commands.json因完整构建重新配置而变化，但其实际取用的26项SDK include/define参数逐项完全一致，单独保留 `compile-command-review.json`，不改写旧专项结果。

## 完整构建与边界

`build/cof-revolver-cold-client-20261114-01/checkpoint/receipt.json`：完整CSOLSupport链接成功，470.98秒；559项实际Ninja编译依赖与控制稳定，资源刷新ON能力保持。广域源集合因同时推进独立服务端效果源而变化，明确记录broadStable=false；不冒称广域冻结。

独立封存的客户端SHA-256为 `d56963eb1bf3299822fa6b65caa460081185ebc9ca1f4cbbf0414fb4b202195a`，匹配PDB GUID `{1CA0A0A3-699A-7F88-4C4C-44205044422E}`、age1，详见 `symbol-verification.json`。未部署或进行新左轮游戏验收。

剩余：服务端实际效果分发/装填阶段和玩家生命种子、合法技能扩容的五壳位显示、HUD/菜单/真实下载/购买准入，随后配套整枪、多人及性能验证，再推进VP70和全部其他武器/变体。PROJECT_STATE/PROGRESS登记本报告的各级证据和未验边界；GOAL保持完整范围；TODO仍保留左轮整枪。按人类要求不发非打包跨聊，精确领域交接已写入本报告。
