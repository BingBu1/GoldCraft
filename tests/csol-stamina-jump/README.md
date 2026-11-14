# 原生成功起跳与体力

测试执行当前生产PM_Jump的完整提取正文，保留死亡、游泳、冻结、禁止跳跃、空中、按住按钮和蹲姿过渡门。只替换函数名，纹理、音效和跳跃速度/重力 helper 是明确的引擎边界；实际角色/地图/生命服务、体力和消息使用已封存完整GameDLL的生产对象。

先从当前源生成适配器：

```bash
python -X utf8 -B tests/csol-stamina-jump/extract.py --output build/jump-adapter-new
```

在完整且输入稳定的当前服务器构建完成后，用其`link-inputs.json`：

```bash
python -X utf8 -B tests/csol-emergency-kick/run.py \
  --seal build/csol-stamina-jump-full-20261114-01/server/link-inputs.json \
  --adapter-dir build/scenario-sdk-current-20261108-05 \
  --fixture-include build/jump-adapter-new \
  --output build/jump-sdk-new --sealed-production \
  --fixture tests/csol-stamina-jump/sdk.cpp \
  --groups normal low noon duplicate client air held water waterjump dead duck prevent freeze retire map-cancel new-life
```

其中seal和引擎适配器必须替换为本机当前已验证的对应构建，不能随意复用旧对象。测试冻结条件前显式注册真实`freezetime_jump`对象，因为窄世界夹具没有完整执行GameDLLInit的全部CVAR登记。

`run_native.py`另外运行既有fluid/status/native-water真实PM提取测试；这些夹具不创建灾变体力owner，新增体力API使用明确的空owner边界，不能将它们当成真实扣费测试。

这些测试不启动图形客户端，不证明整套CoF奔跑/地速、真实玩家预测、游戏FPS或多人通过。
