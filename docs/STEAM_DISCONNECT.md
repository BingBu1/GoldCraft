# Steam 掉服调查与在线会话恢复

## 2026-10-17 重装后的有限对照

用户自行重装 Steam 后已正常登录。保持同一沙盒 B、DLL/helper 和原公网服务器，主调度未改认证、代理或服务器设置。三次公网只读状态保持同一玩家连接和真实 Steam 身份，末次持续 6 分 37 秒，跨过旧约两分钟掉服窗口；两辅助通道均为 AppID0/已登录，样本新鲜。原安装19575项元数据及5项内容hash保持。

两分钟1179个空闲样本没有新增长帧，但没有菜单切换，不能验收 ESC；新 Steam 已登记模块不含旧安装目录文件，加载的 XInput1_4.dll 为微软签名有效的系统文件。游戏与 Steam 二进制hash未变，缓存、安装位置和会话已变，不能将单次恢复视作完整因果对照或永久保证。证据 `build/steam-reinstall-20261017/checkpoint.json` 与 `esc02/summary.json`；网络身份和原始日志仅本地保存。随后用户确认“已经不卡了”“我重装了Steam好了”，本次ESC故障结案；后续继续语音、重连和长期在线，详见 [ESC 调查](2026-10-17_分析-ESC-Steam阻塞-report.md)。

## 2026-10-16 实际恢复

隔离新版仍在进服113/121秒后收到拒绝12。个人Steam日志停在前一晚的I/O断开，没有新的账号登录成功；当天HTTP连通性测试成功、进程存在和在线模式偏好都不能证明用户会话在线。专服则有独立的成功登录记录。

主调度使用Steam官方正常退出入口，旧CS退出码0，保留MC及双服；重新启动同一Steam后，16:02:55出现个人登录OK/Logged On。随后启动同一沙盒CS、同一已部署DLL/helper与服务端，在16:03:35收到真实`STEAM USERID validated`。保留现有VPN/代理和认证规则，没有改为LAN、屏蔽拒绝或更换票据。同一userid/serial连续在线6分24秒，无重连或再次拒绝，已跨越多个旧两分钟失败窗口；MC重新配对，HUD/相机/实体持续接收。

两个AppID0 helper的真实新鲜状态也从loggedOn0变成1；这是辅助证据，服务端认证批准才是本次游戏准入依据。恢复前后证据在`build/steam-disconnect-20261016/OFFLINE_HANDOFF.md`与`build/status-vision-20261016/recovery-*.json`。为何原Steam用户会话未自行恢复仍未定位；不将本次恢复当作未来网络异常或所有语音/头像功能的永久保证。

已确认本地服务器实际出现“Steam connection lost”和“Unable to connect to Steam”；后者来自 ReHLDS 的 `k_EDenySteamConnectionError=12`，前者对应11。另一次是 MC断链90秒踢出，属于不同路径。隔离候选已部署且生效，但随后仍约两分钟掉服；进一步确认个人Steam连接未恢复，而专服已独立在线。正常重启Steam后已重新登录并得到服务端认证通过，同一会话连续6分24秒无再次拒绝，本次恢复已验证。

## 已证事实及边界

| 证据 | 发现 | 路径／限制 |
|---|---|---|
| 当前ReHLDS控制台、匹配的sv_steam3.cpp/steamclientpublic.h | 15:11:37与15:17:27收到“Unable to connect to Steam”；连接分别约116秒、112秒后断开 | Steam拒绝 → ReHLDS `OnGSClientDenyHelper` → 原生断开；没有屏蔽此检查 |
| 三个实际进程的只读环境与Steam gameprocess日志 | 旧helper继承SteamAppId/GameId/OverlayGameId=10及SteamEnv=1，两helper也被登记为AppID10游戏进程 | 游戏启动身份被复制给查询辅助进程，是已证隔离缺口；尚未证明它引发本次拒绝 |
| Steam连接日志 | 14:43网络设备断开、HTTP502；专服Steam连接中断后在14:43:21重新登录 | 有真实网络异常，但时间不直接等于后续每次掉服；不归咎于全部代理流量 |
| 对当前B的只读接口／导入表审查 | 认证、票据、取消票据、回调注册／运行与Steam初始化／退出仍指向原Steam模块；只有已声明的语音和context适配指向GoldCraft | 证明入口未被本插件替换，不证明Steam后台或引擎每帧调度没有其他问题 |

SDK1.61匹配接口为SteamClient021、SteamUser023与SteamUtils010；引用版本`1b9765621fddbaea0ab68cd4bbdaf9a99c207b72`。只用公开接口声明，不发布SDK副本或个人会话数据。

## 候选改动

子进程使用独立环境块，按不区分大小写的变量名去除SteamAppId、SteamGameId、SteamOverlayGameId、SteamEnv、SteamClientLaunch；父游戏环境和其他配置保持原值。helper入口在加载Steam之前再次清理，取得现有用户接口后检查GetAppID；非零则报初始化错误10并正常释放自己的管道，不进入头像或语音处理。

辅助进程只连接现有真实用户，不登录新用户、不申请或取消认证票据、不启动游戏服务器会话，也不接管游戏回调。没有关闭认证、切换直连或修改VPN。是否在真实Steam客户端中仍能以无游戏AppID的辅助管道正常取得头像和语音，是后续必要验收项；失败不得用伪造身份代替。

增加后台每秒BLoggedOn采样，主线程读取原子诊断值：`avatarHelper`／`voiceHelper`的`appId`、`loggedOn`、`samples`、`sampleAgeMs`、`failures`。`loggedOn=-1`表示未知／传输失败，0表示该辅助管道观测到离线，1表示已登录；过旧样本不能当成当前状态。这些字段不代表游戏票据已被服务器批准。初始化结果不再等工作线程退出才发布。

## 验证和交付

Clang x86/C++20/O3/ThinLTO/AVX2/precise独立构建采用单worker、低优先级。核心、实际ABI适配器、真实子进程传输、DLL取消卸载及新增生产helper隔离测试均通过。新增测试使用严格的假Steam DLL，在实际helper进程内验证父身份保持、子身份清理、AppID非零拒绝、真实数据传输、登录状态变化、样本时间和仅释放自有管道；未允许的认证／回调槽一旦调用就失败。该测试不连接Steam、不使用麦克风，不属于在线玩法验收。

本地证据：`build/steam-disconnect-20261016/{observation02.json,process-env.json,live-abi.json,build03.log,build05.log,HANDOFF.md,manifest.json}`。最初三次构建在测试DLL的链接命令／导出声明处失败，记录保留；不将其写成全部构建首次通过。历史analysis证据已删除。

主调度已集成CMake测试补丁，生产GoldCraft/helper和完整84项CTest通过，IPC2 DLL/helper已随26项联合候选部署。保持VPN，在同一服务器至少跨越多个既有约两分钟失败窗口；观察Steam进程登记不再把helper当AppID10、辅助管道AppID0、有效登录状态、游戏真实认证持续通过，以及TAB／K、语音、普通服、重连／自然退出。若仍掉服，按同一时刻的服务器拒绝码、Steam连接状态及样本年龄继续定位，不能忽略认证失败。
