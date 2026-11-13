# 首张灾变地图编号

sdk.cpp运行真实CSOL_InitGameModes和CVAR注册，覆盖启动参数及重复初始化。引擎函数表与文件系统是明确SDK夹具边界，不是图形客户端入服验收。

在工作区Git Bash运行，使用已封存的当前完整服务端对象：

```bash
python -B tests/csol-emergency-kick/run.py \
  --seal build/csol-map-bootstrap-full-20261113-01/server/link-inputs.json \
  --adapter-dir build/scenario-sdk-current-20261108-05 \
  --sealed-production --fixture tests/csol-map-bootstrap/sdk.cpp \
  --groups minus plus precedence absent zero boundary missing empty negative overflow garbage fraction plus-sign \
  --output build/map-bootstrap-sdk-new
```

输出须为新的工作区build目录；封存输入漂移时应拒绝，不覆盖旧证据。当前工作树的目录/RulesProfile领域仍有独立发布闭包，不能以该夹具通过宣称所有地图或公开HEAD完整可玩。
