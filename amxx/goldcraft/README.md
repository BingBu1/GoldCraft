# GoldCraft 形态与按键插件

`goldcraft.sma` 负责形态切换、权限校验和菜单。`goldcraft_controls.sma` 单独负责入服后的 F6 绑定，不依赖 Minecraft 是否已经配对。

真人进入服务器一秒后，按键插件发送 `bind "F6" "goldcraft_menu"`。Bot、HLTV、已断线或连接身份已变化的槽位不发送；断线时取消延迟任务。服务端 `mc_bind_f6` 默认为 `1`，设为 `0` 可关闭后续自动绑定。

F6 在此服务器中指定为形态菜单键，自动绑定会替换原 F6 命令，其他按键不变。AMXX 无法读取客户端现有绑定；这不是“仅未绑定时写入”。关闭自动绑定不会恢复玩家以前的 F6 命令。

客户端可能过滤服务器发来的 `bind`。插件不会关闭该过滤器；玩家可在控制台执行 `bind F6 goldcraft_menu`，或聊天输入 `/forms` 打开同一菜单。单独输入 `goldcraft_menu` 能打开而 F6 不响应，说明菜单命令已可用，应先检查 `bind F6`。

在仓库根目录编译部署：

```powershell
.\tools\Build-AMXX.ps1 -Plugins goldcraft_controls -Deploy -PluginList plugins-goldcraft-controls.ini
```

手工安装只需复制 `build/amxx/plugins/goldcraft_controls.amxx` 到服务器 `cstrike/addons/amxmodx/plugins/`，并复制 [独立清单](configs/plugins-goldcraft-controls.ini) 到 `cstrike/addons/amxmodx/configs/`。保留原 `plugins.ini`。

**地图重载后才加载新插件，`sv_restart` 不重载 Pawn。** 在方便结束当前地图时用 `changelevel cs_assault`，再检查 `amxx plugins` 中 `GoldCraft controls` 为 `running`，让玩家重连并实按 F6 验收。项目的换图清理规则仍会执行。
