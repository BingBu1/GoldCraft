# CS 内 Minecraft 文本输入

输入修复候选把文字与按键分开处理。中文输入法提交、小键盘数字、Caps Lock、键盘布局和 Shift 符号取自 SDL 的真实文本事件；编辑键、鼠标和 Ctrl 快捷键继续使用独立按键事件。移除旧的 ASCII 拼字，避免重复字符，并避免每帧释放游戏按键时清空 Shift 后把 `@` 变成 `2`。

实现位于 `native/client/host_text.cpp` 与 `native/include/goldcraft/text_input.hpp`，不继续堆入主插件。使用固定 MetaHookSv 所带 SDL3 的公开 `SDL_WaitEventTimeout` 接口，在 SDL2 兼容层把文本截成 32 字节前取得完整提交。候选词和预编辑文字通过匹配的 VGUI2Extension 接口显示；输入法确认期间不把 Enter、Escape、退格等再次送给 Minecraft 界面。候选显示内容只暂存在内存，诊断仅输出计数。

文字只发往当前前台进程、SDL 窗口、世界和已显示的菜单代次；切菜单、失焦或打开 CS 菜单/控制台会取消原输入法组合并恢复原来的文本输入状态。Minecraft 端继续校验世界、菜单和实际 Screen，校验 Unicode 标量，按原版 UTF-16 分发，并保留 NeoForge 字符输入 pre/post 回调。

## 验证边界

生产 UTF-8/session 单元、实际 SDL 适配器和生产 KeyEvent 抽取测试不打开游戏窗口、不模拟真人输入。`tools/Test-HostText.py` 编译实际 HostUi/Wire，以观察型 Screen 适配器验证中文、符号、小键盘文本、补充平面字符、非法字符、过期世界/菜单、快捷键和 Mod 字符回调。候选需配套客户端 DLL 与 MC Core；未替换当前游戏进程，不表示真人输入法和候选词画面已验收。

下一次联合更新后，在聊天、指令、物品搜索和告示牌中实测微软拼音及用户常用输入法：中文选词与长句、Shift+2、Caps Lock、小键盘 Num Lock 开关和运算符、Ctrl 快捷键；确认 Enter 选词不发聊天、Escape 取消组合不关闭界面、切菜单/失焦无串字。原生 CS 控制台与普通服务器仍需现场对照。
