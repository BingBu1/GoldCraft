# VGUI 字体资源刷新回归

测试实际 x86 `VGUI2Extension` 对象中的 `FontManager`、`Win32Font`、`FontTextureCache`、`Scheme2` 和 `ResourceRefresh`。只替换宿主边界：文件系统、surface、纹理上传和 KeyValues 符号服务。字体创建、字形栅格化、私有字库、注册释放均使用真实 Windows GDI；不会启动或操作游戏。

测试使用本机 Arial / Times 字体生成两个同 family、不同内容的临时 TTF，并另建未安装的内存字体 family。生成字体只保存在指定 `build` 输出中，不发布游戏或系统字体。

覆盖以下行为：

- 同 HFont/HScheme 重读新 scheme、字体内容和 owner 配方；`StageFont` 的提交/取消。
- 同 family 的多个 owner 同时失效；共享字形页上的无关字体保持。
- 40 轮刷新不增加 HFont、原生字体或纹理页；释放选中的 GDI 对象、旧 KeyValues。
- 空首入服、忙事务、线程、失效 token、ABI 长度、路径校验。
- 缺文件、坏 TTF、缺 family、不支持的 Border 方案；失败持续暂停，恢复旧 VFS 后 Abort。
- 撤 overlay 后正常系统字体继续可用，CSOL 字体休眠；取消重入和再次进入沿用句柄。
- `AddFontMemResourceEx` 的非枚举字体能经真实 VGUI 创建与替换；缺字库时不误用系统替代字体。
- 正常退出只释放自己拥有的文件字体注册；其他 owner 的注册仍存在。
- 真正加载最终 DLL，通过工厂调用新接口全部方法，并保留原 Scheme2/Surface2 工厂接口。

在已初始化项目 x86 Clang 开发环境且构建了 VGUI2Extension 后运行：

```powershell
python -m pip install --target build/vgui-font-test-python fonttools
python tests/vgui-resource-refresh/run.py `
  --build build/vgui2extension-clang-Release `
  --output build/vgui-font-test-run `
  --fonttools build/vgui-font-test-python
```

`--output` 必须是尚不存在的工作区 `build` 子目录。可以用 `--environment` 传入本地已保存的编译环境 JSON；该文件不属于发布物。脚本冻结测试源码、记录对象 SHA、执行 x86 测试，并校验输入未变。

`--old-cache` 用于链接保留的原版本构建：只运行实际旧 `LoadSchemeFromFile` 缓存反例，预期失败退出 1。这个选项不能指向已替换的构建对象，否则不构成原代码对照。

组件测试不能代替入服 HTTP 更新、真实 OpenGL 上传和游戏中各菜单的画面验收。字体方案限定为当前 manifest 允许的纯字体 CSOLHudScheme；不覆盖任意有 Border 或不同 alias 拓扑的 VGUI 方案。
