# 换图重连计时回归

`tools/Test-EngineReconnect.py` 从指定新旧 `sv_main.cpp` 提取原函数，使用同一个 x86 夹具验证服务端加载不会占用玩家的重连响应期限。使用真实 ReHLDS SDK 的 `client_t`、`netchan_t`、`cvar_t`、`DLL_FUNCTIONS` 和相关声明；网络、时钟和游戏 DLL 是可控边界。`CGameClient` 只提供夹具所有者存储，三个 inline 方法从匹配 SDK 头精确提取，因此这不是完整 `CGameClient` 布局/vtable 或完整引擎 ABI 验证。

实际提取 `SV_New_f`、`SV_CheckTimeouts`、`SV_InactivateClients`、`SV_BuildReconnect`，以及 `SV_ActivateServer_internal` 最后的完整客户端循环与其局部变量初始化。有 `SV_SendReconnect` 时提取该真实 helper；旧版无 helper 时保留真实 activation 中原两行发送代码，不手写旧行为替身。

## 运行

在项目根目录、已初始化的 x86 MSVC/Windows SDK 环境执行：

```powershell
python tools/Test-EngineReconnect.py --compiler <clang-cl.exe> --output build/engine-reconnect-tests
```

可用 `--environment <本机环境字典>` 载入已有工具环境，默认直接读取当前 `external/ReHLDS/rehlds/engine/sv_main.cpp`。无需已删除的历史 build 目录。旧版默认从根仓库 Git 提交 `1987898` 中的 `patches/rehlds-goldcraft.patch` 取得：脚本在独立临时 Git index 将该补丁的 sv_main 文件段应用于当前 ReHLDS 上游 HEAD 的 LF 规范化内容，再用 `git show :rehlds/engine/sv_main.cpp` 在内存读取补丁新侧；不会修改实际索引或工作树。清单记录精确提交、patch blob、上游提交、规范化基线blob和结果SHA。换行规范化后的基线blob必须与所选历史补丁兼容，补丁不能应用时明确失败。

`--old-ref` 可选择另一明确 Git 版本，`--old-source` 可显式指定已有旧源文件，`--compare-source` 可额外核对候选与工作树的提取片段相同。默认固定输出目录可重复使用，只生成必要的函数/方法片段、对象、程序、命令、日志及身份清单；不复制完整 sv_main、main.cpp、runner或源码树。

单 job 依次编译和链接新旧单 TU，O3/ThinLTO/AVX2/precise FP，每组独立进程。夹具直接编译 `tests/engine-reconnect/main.cpp`；实际 include 在预编译发现并记录 SHA，再核编译与测试结束时未变。`result.json` 记录全部命令、真实SDK依赖、EXE身份、Git旧版来源和精确新旧预期结果。省略 `--compiler` 仅提取当前源而不编译。当前activation局部包含可选加载计时器，测试使用实际头文件，日志出口为明确替身；它不改变测试时钟或deadline。

## 验收边界

| 场景 | 测试合同 |
|---|---|
| baseline | 原 accepted new、拒绝、重复/console new、第 1/32 槽、普通 60 秒 `sv_timeout` 和独立 30 秒重连期限、fake client/零设置规则保持 |
| load-delay | `realtime=100` 停在加载帧，`Sys_FloatTime=1000` 向前走32秒；模拟 Netchan 发送耗时0.25秒，必须在发送完成后才启动期限；第一帧不可误踢 |
| response | 发送后29.75秒 accepted new 清期限；持续资源流量保留；不发 new 即使 netchan 活跃仍在30秒后掉线 |
| clock | 发送与检查使用同一持续时钟，frame clock 暂停或跳变不能误算重连期 |
| eligibility | 原 activation 条件、fake/空槽排除、多槽发送、单人 serverinfo 分支保持 |
| repeat | accepted new 后再换图，以及前次请求尚未响应就再次换图，都先撤销旧期限，再从下一真实发送计时 |

默认 cvar 声明从真实源提取，不提高 timeout、不禁用重连安全门。旧版预期失败只允许精确登记的16条计时断言，其他失败仍令整轮失败。

2026-11-06 实际新旧对照：新 **61/0**，旧同61检查、精确16个预期失败（加载8、响应1、时钟2、重复换图5）；新旧基础24项和过滤4项都通过。第1/32槽旧版均在32秒加载后首帧掉线，新版从发送完成的 `1032.25` 开始计时并保留。历史运行核对了445个实际SDK include和485项身份引用；该历史构建目录已清理，不能作为当前脚本的输入。第一次两轮仅预检遇到夹具头包含顺序/命名问题，记录保留；attempt03完成所有实际编译/运行，但初版汇总未清Windows CR字符误报false。最后只规范化既有FAIL行并复核原日志/EXE，没有重编或重跑。

该夹具不发送真实网络包，不运行地图加载或图形客户端，不证明客户端停帧已解决，也不证明服务端32秒加载已缩短。正式引擎构建、现场重连和加载性能须分别记录。任务依据见[换图重连计时与加载阻塞报告](../../docs/2026-11-06_修复-换图重连计时与加载阻塞-report.md)。

2026-11-07 直接源码/Git旧版runner已经重新编译运行通过：新版61项/0失败，旧版61项/精确16个预期失败，12组判定全部通过，实际包含的输入无漂移。旧侧来自Git提交 `198789866e437ba0dc523a49526f4077edd5c9a1`，补丁blob `af88d5e55cbe98fed46312a44a56ad075ff8a71c`；上游 `550f2d62f13f4ebeb029c1d9d1c212133202611d`，LF规范化基线blob `5bd83fa30e4b5bb98faec027391d06c43f9a3c22`，旧侧完整内容SHA `afbb064cdf46a09d81317da924a6cf7f2c6038d416ae8f87eebad779fa6e6fbe`。当前完整结果位于 `build/engine-reconnect-tests/result.json`。

初次Git提取未先规范化上游CRLF，补丁应用明确失败、没有编译；补充仅针对临时index的LF规范化后成功。没有在实际源码树或真实Git索引中修改换行。
