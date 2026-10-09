# AMXX 管理员

正常专服使用 `sv_lan 0`，管理员按 SteamID 认证。`admin.amxx` 负责读取 `users.ini`；单独填写账号而没有加载此插件，不会授予权限。账号记录和口令只放服务器运行目录，不进入 Git。

## 构建与加载

源码来自 AMXX **1.9.0.5303** 的 Admin Base，保留作者和许可；`admin.sma`、`lang/`、`LICENSE.txt` 均保存在本目录。公开仓库提供 [补丁](../../patches/amxx-admin-modern.patch) 和 [校验清单](../../patches/amxx-admin-modern.json)，完整副本由已准备的固定 AMXX 包重建。

```powershell
python tools/Prepare-AdminBase.py
./tools/Build-AMXX.ps1 -Plugins admin -Deploy
python tools/GoldSrc-Command.py 'changelevel cs_assault'
```

首次 `Initialize-AMXX.ps1` 自动构建并启用 Admin Base，安装缺少的语言字典，保留现有账号及自定义字典。更新 Pawn 需要换图；仅修改 `users.ini` 则执行 `amx_reloadadmins` 即可重载在线玩家权限。`sv_restart` 不加载新 Pawn。

SteamID 账号使用认证标记 `ce`（按 SteamID、无需密码），完全管理员的访问标记为 `abcdefghijklmnopqrstuv`，不包含普通用户标记 `z`。从已认证连接的 `status` 取得真实 SteamID；通过 `amx_addadmin` 添加时将 SteamID 用双引号包围，避免 GoldSrc 把冒号拆为参数。不要用共享的 `STEAM_ID_LAN`、玩家昵称或局域网地址代替身份。

ZP 玩家菜单中的管理员入口及 Nade Modes 的 `amx_nmm` 使用这些权限。Menus Front-End 是可选的；未安装时仍能直接打开 `amx_nmm`。

## 验证范围

Admin Base 使用 `create_cvar`、`get_players_ex`、`find_player_ex`、文件句柄 API 和不重叠的路径拼接；保留官方认证匹配语义。新增账号时不输出密码。

`Exercise-AMXXModernization.py` 在独立服务器验证合成账号的加载、标记、重复添加、重载及未匹配 Bot 不获权限，测试后恢复账号文件。它不模拟或证明真人 Steam 登录；实际授权须在真人连接后确认。
