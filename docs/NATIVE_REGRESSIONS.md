# 原生 CS 回归记录

2026-10-07。当前以单个沙箱 B、ReHLDS 和 6 个 SyPB 僵尸 Bot，在 `sy_zombie2_Bloodmoon` 排查原生 CS 问题。原始游戏安装只读。独立 GPU 测试与服务器输出只证明列出的范围，完整游戏视觉、原始鼠标输入和转向时人物可见性仍待验收。

## 已定位并修复的代码路径

| 证据 | 结论 | 调用路径与处理 |
|---|---|---|
| E01：匹配 CS 客户端调用公开 `StudioRenderShadow`，引擎经 TriangleAPI 绘制带 alpha 的四边形；真实 GPU 测试中旧路径覆盖了 8,224 个光照缓冲分量 | F01：TriAPI 只写片元输出 0，多个 GBuffer 附件同时启用会产生未定义写入，能够形成不透明方块 | P01：`StudioRenderShadow → TriangleAPI → triapi_End → triapi_shader`。新公开接口 hook 在阴影/深度 pass 跳过贴片，颜色 pass 只写 diffuse，并恢复原输出映射 |
| E02：服务器 `gc_zp_locale` 对购买标题返回中文，对 `ZOMBIENAME Classic Zombie` 和武器名返回 `ML_NOTFOUND`；匹配 AMXX 源码在键的首个空格处截断 | F02：原翻译文本已在文件中，但运行时注册键与查询键不同 | P02：`CLang → CTextParsers::ParseFile_INI → GetLangTransKey/%L`。生成无空格别名，同时修正职业、HUD、道具、模式和购买菜单的查询 |
| E03：沙箱配置开启 `r_drawlowerbody` 和附件，Renderer 根据它在第一人称绘制自身模型 | F03：低头看见自身下半身来自已开启的渲染功能 | P03：沙箱生成配置明确关闭这两个选项，保留原键位和实际模型投影阴影 |

E01 的硬件行为可能因驱动而异，因为未声明的片元输出没有规定值。回归同时使用实际 TriAPI shader 和显式污染其他附件的测试 shader；生产隔离代码在两种情况下都必须保留地面光照、法线、材质和深度。该测试使用隐藏 OpenGL 窗口，不控制游戏窗口。

阴影修复通过公开的 `engine_studio_api_t` 函数指针安装，不新增硬编码地址或扫描规则。精确引擎分析和运行日志仅保存在本地，仓库发布源码补丁与复现测试。

## 复现和验证

按[构建说明](BUILD.md)准备锁定源码及工具链，然后运行：

```powershell
.\tools\Build-Renderer.ps1
python .\tools\Prepare-ZombiePlague.py
python .\tools\Verify-ClangBuilds.py
python .\tools\Verify-SourcePatches.py
```

Renderer 构建执行 6 项 CTest；`studio_shadow_gl_tests` 验证透明角落、圆心变暗、三个其他颜色附件与深度不变、完整及部分输出映射恢复、阴影/深度 pass 排除、普通前向路径和异常恢复。没有 OpenGL 环境时该项标为跳过，不能记录为通过。

中文的实际安装与服务器查询见[僵尸测试](ZOMBIE_TESTING.md)。本次实际服务器已返回“普通僵尸”“普通人类”“M4A1 步枪”“感染炸弹”“感染模式”“僵尸瘟疫”；客户端菜单布局和操作尚需实测。

## 尚未确认的问题

- 开启 `m_rawinput 1` 后无法转动视角：沙箱与正常安装的 SDL 版本不同，但尚未建立因果证据；未通过关闭原始输入或随意替换 SDL 宣称修复。
- 其他人物随视角消失：需要实际渲染阶段与裁剪结果证据；未关闭视锥裁剪来掩盖问题。
- 主副武器：已扩大遗漏的 `clientdata.viewmodel` 位宽，实际切换显示待验证。
- 室内亮度、漏光及动态阴影：需要 CS 真实场景对比，独立 MRT 测试不能替代。
- Minecraft 图形端：NightFall 专服配置访问已有定向防护；YSM/Epic Fight 自定义人物渲染器防护及 HUD 独立矩阵栈已构建，27 项 JUnit 通过，实际组合渲染尚未验收。

继续测试时应先验收原生 CS，再接入 Minecraft，并保留单客户端加 Bot 的当前测试安排。
