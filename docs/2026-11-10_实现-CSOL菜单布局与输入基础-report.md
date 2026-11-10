# CSOL 菜单布局与输入基础

本阶段只交付两项可独立检查的基础代码。购买菜单、选人页和武器卡片仍在集成，没有据此宣布整套菜单还原、部署或实机通过。

## 范围与样本

用户要求依据原版 DLL 还原菜单，并保持重复点击流畅。只分析工作区副本；未操作受保护安装和正在进行的真人对局。

| 原版 client | 架构／基址 | SHA-256 |
| --- | --- | --- |
| 较早样本 | x86／01900000 | f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd |
| 当前样本 | x64／180000000 | 0c71f6dcfda0a0c630f27e5fba1ebbcd404821811407d658a37b5206c6150538 |

以下地址均为 VA，不能跨版本套用。原始反汇编和逐字节核对留在私有 `analysis/csol-menu-client-20261110`、`analysis/current-cso-menu-20261110`，不随源码发布。当前样本最终布局证据的 180 个窗口已与对应 PE 核同。

## Evidence → Finding → Path

| 证据 | 结论 | 对应源码 |
| --- | --- | --- |
| E1：x86 01DE6070／01DE5FE0；当前 180696C20 | 独立 X/Y 比例；宽屏留白只加在根面板；位置和尺寸分别截断，子面板逐层截断 | F1：`native/csol/menu_layout.hpp` 的 Viewport；已实现并离线检查 |
| E2：当前 1816CB120、1816CE860；x86 02CA7790→02CA7E70 | 页签宽为实际文字宽＋14 像素，间隔 1 像素；背景两端使用纹理原生像素宽 | F2：TeamTabs／TabSlices；已实现并离线检查 |
| E3：x86 02C958F0；当前 1813CE310 的五项进度／mirror 设置 | float 运算后转 double，加 0.0001 再截断；不是四舍五入 | F3：ProgressMirror；已实现并覆盖边界 |
| E4：x86普通 Button 按下捕获、armed 松开执行；TabButton 背景选择分支 | 同目标松开才执行；已选／悬停／按下共同选择页签图组 | F4：`native/csol/client/menu_input.hpp`；宿主事件与轮询合并适配 |

调用路径 P1 为“原版布局参数 → 比例与逐父截断 → 同一实际矩形用于绘制和命中”。P2 为“宿主按键事件或轮询 → 捕获状态 → armed 松开 → 菜单消费者”。事件后的同周期旧轮询不能覆盖事件，下一周期恢复轮询。这部分是宿主适配，不冒充 CSOL 原接口。

## 复现与验证

从项目根目录的 Git Bash 执行，参数指向已配置的 x86 编译器环境：

```bash
python tests/csol-menu-layout/run.py --compiler "$CSOL_COMPILER" --environment "$CSOL_COMPILER_ENV" --output build/menu-layout-review
python tests/csol-menu-input/run.py --compiler "$CSOL_COMPILER" --environment "$CSOL_COMPILER_ENV" --output build/menu-input-review
```

实际记录为 `build/csol-menu-layout-20261110-01` 的 289 项和 `build/csol-menu-input-20261110-01` 的 286 项，编译、运行均成功，所有输入哈希前后不变。检查包含非整数比例、父子截断、边界命中、释放到控件外、拖回、同周期重复回调、两次点击和打开时按住鼠标。发布前重新核对当前源码仍与收据一致。

基础代码只使用定长数据和算术，输入处理不分配内存，不执行磁盘／网络／图形查询。没有实际游戏帧时间采样，不能据此宣称 FPS 提升。菜单消费者的网络等待、原版点击间隔、文字控件、裁剪、焦点以及完整 DLL／实机画面对照仍属后续验收。

## 阶段记录

2026-11-10 先核对样本和原指令，再实现两个 helper，随后完成上述真实 x86 检查；本次将它们独立提交，其余菜单工作保留在工作区。整套菜单候选尚未部署，完整灾变目标继续。
