# 武器槽位与菜单测试

`menu.cpp` 使用匹配的 MetaHookSv SDK 头文件，调用完整生产
`weapon_inventory.cpp` 和 `weapon_slots.cpp`。消息与命令通过初始化时真实注册的
回调进入；没有在夹具中重写 `Cycle`、`Cancel`、`ShowMenu` 或选枪算法。

在工作区根目录运行（输出目录必须尚不存在）：

```powershell
pwsh -NoProfile -File tools/Test-CSOLWeaponMenu.ps1 `
  -OutputDirectory build/csol-weapon-menu-check `
  -Python .tools/hlam-venv/Scripts/python.exe
```

113 项检查覆盖（含先前 68 项镜像/槽位检查的扩展）：

- 无限时菜单只允许 0 键退出，`cancelselect` 后 1–4 不再被旧菜单抢占。
- `hud_fastswitch 0` 先选择、左键确认，确认只消费攻击位；`1` 在每槽一把枪时立即切换。
- 菜单打开时无效 1–4 仍交给原处理器且保留菜单，合法 0 键释放槽位。
- 菜单超时、服务端清空、武器选择取消、空闲取消和库存重置。
- 模拟原生 `CHudMenu` 的显示状态；原 `cancelselect` 只转发 Escape，不自行清菜单。
- 取消菜单必须先释放镜像，再向原 ShowMenu 回调发送五字节全零包，最后转发 Escape。
- 原取消回调同步打开另一菜单时，新菜单不会被后续清理误删。

引擎时间、网络命令、HUD 资源和原菜单回调是可控边界。测试不覆盖真实客户端
`CHudMenu` 内存布局、其他插件键盘拦截或实机菜单焦点；这些仍需运行时验证。

`-InventorySource` 可指定工作区内的历史生产文件，并以 `-ExpectFailure` 验证旧版。
Runner 独立保存整份生产 TU 的原始字节、精确编译命令、输入 SHA、编译日志与运行
退出码到输出目录，不覆盖生产源文件。

`session_clock.hpp` 在已有完整客户端夹具内验证会话与武器状态。运行：

```powershell
pwsh -NoProfile -File tools/Test-CSOLSessionClock.ps1 `
  -OutputDirectory build/csol-session-clock-check `
  -Python .tools/hlam-venv/Scripts/python.exe -FullClient
```

专项直接编译生产 session、inventory、weapons、pickup、crosshair、beamgun、cannon
和武器协议 TU。65 项检查覆盖同地图内 0.01、0.5、1、1.5、8、99 秒时间回退，
消息时间领先渲染时间，视频重建、ResetHUD、断连/同图重连与换图前置注册。
同时检查真实定义、库存、当前武器、HUD 所有者和 1/2 槽确认；断连与 ResetHUD
必须撤回旧扩展武器施加的原生 HUD 隐藏位。`-FullClient` 接着运行原有客户端
整组测试，两个退出码单独保存。它不会调用独立的菜单 runner。

时间与地图名称由引擎边界提供合成样本。这能够复现代码路径，不能据此声称
沙盒 B 实际发生了同样幅度的校时；仍需对照运行时诊断。
