# 资源缓存退休回归

使用实际 x86 MetaHook/Renderer 类型测试模型、SPR、声音与绘制缓存的释放顺序，以及扩展模型在断线后的所有权。它不启动游戏、不操作真实 GL，也不证明完整入服更新已接通。

从已配置的项目 Windows 工具链执行：

```powershell
python tests/resource-retirement/run.py --output build/resource-retirement-new --renderer-command build/renderer-compile-command.json
```

`--renderer-command` 是实际 Renderer 编译命令的 JSON，形如 `{"argv":["clang-cl.exe", "...", "/c", "gl_studio.cpp"]}`，保留当前 SDK 的 include、宏和 x86 选项。可用 `--environment` 提供 Windows 工具链环境 JSON；`--legacy-ref a6bdf98` 额外编译之前的模型缓存实现，要求空 SPR 回归能检出错误。

测试覆盖：先停止使用者再释放主块、外序列和皮肤；SPR Hunk 不误交给 Cache_Free；断线已清空的 SPR 不传给原卸载函数；Renderer 工作项、已被替换但仍有引用的绘制对象、纹理和骨骼缓存退休；当前连接模型范围缩小后仍必须完整枚举扩展 owner。原 v3 路由与 v4 清单暂停/恢复也使用生产解析器验证。
