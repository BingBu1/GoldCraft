# 两刀完成音效 SDK 回归

执行真实 `CCSOLMelee::DelayedAttack`，比较青龙偃月刀与旋风 SKULL-9 的空挥、墙体、单目标和多目标音效。覆盖随机声样本、声道、音调、音量、衰减、伤害顺序和引擎回调中取消或释放实体。

`sdk.cpp` 包含完整生产 TU。运行器只把类的一个 `private:` 改为 `public:` 以设置待执行状态；方法体不替换。未开放访问的正式源码另外完整编译。其余 GameDLL 对象取自已核验基线，逐份记录散列；受控引擎边界来自公共真实 SDK 夹具。

在项目根目录运行，输出目录必须不存在：

```powershell
python tests/csol-melee-sound/run_sound.py --baseline build/csol-menu-boss-restart-20261104/server-full-01 --edges build/csol-boss-resource-precache-20261104/sdk02/source/tests/csol-scenario-rules-session --output build/csol-melee-regression-20261104/sound/reproduce
```

参数 `--lto-cache` 可复用此任务的工作区编译缓存。测试保持 x86、C++20、O3、ThinLTO、AVX2 和精确浮点；链接限制单线程。基线必须包含 `build-commands.json`、完整对象 provenance、冻结源码、输入清单和环境；不将这些 SDK 副本或游戏二进制随公开源码提交。

原证：原 MP SHA-256 `39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2`，完成函数 `110B7740/110B8040/110D2B80/110D3560`，声表 `11D07D70`，类型16和10。细节保存在本机 `build/csol-melee-regression-20261104/sound/ida-sound-proof.json`；测试预期按实际分支解读，不能从 WAV 文件名反推。

这不是实机录音、网络声音传播或最终 DLL 部署证据。原模型引用而资源库缺少的 `dragonsword_stab.wav` 继续按用户决定省略，不能用完成阶段的 `stab_hit.wav` 替代起手声。
