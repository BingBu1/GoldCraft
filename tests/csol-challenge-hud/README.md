# 挑战阶段提示

生产状态只接收服务器的CsoTime消息。客户端只递减当前阶段的显示秒数，归零仍等待下一份服务器状态；个人死亡/ResetHUD不拥有地图时钟。

Git Bash中的纯协议、状态、布局与图标检查；`ARKARI_CLANG_CL` 指向本机固定版本的编译器：

```bash
python -B tests/csol-challenge-hud/run.py \
  --compiler "$ARKARI_CLANG_CL" \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --output build/challenge-hud-protocol-new
```

真实UI/GDI消费者采用实际客户端Ninja编译命令，控制引擎与VGUI Surface边界；需要本机已有的字体与编译输出，不分发字体：

```bash
python -B tests/csol-challenge-hud/run_owner.py \
  --client-build build/csol-client-crc/client \
  --environment build/csol-season2-trace-20261109/compiler-env.json \
  --font sandbox/cs-server/Half-Life/cstrike/resource/font/df_gb_y9.ttf \
  --vgui-library build/csol-client-crc/client/csol_damage_vgui.lib \
  --output build/challenge-hud-ui-new
```

完整服务端消息检查复用稳定GameDLL对象，使用tests/csol-challenge-phase/sdk.cpp的enabled、disabled、failed、replay、join场景。执行前核对所选完整构建的link-inputs.json，不复用已漂移的对象。实际网络、GPU像素、听感、多人和CPU/帧时另行验收。

无人隔离专服可以验证时间CVAR确实在同图应用，使用匹配的MP/PDB和已有资源计划：

```bash
python -B tests/csol-challenge-hud/run_cold.py \
  --mp build/csol-decoy-mapid-20261113/server4/regamedll/mp.dll \
  --pdb build/csol-decoy-mapid-20261113/server4/regamedll/mp.pdb \
  --engine sandbox/cs-server/Half-Life/swds.dll \
  --resource-plan build/csol-challenge-environment-cold-resource-plan-20261114-01.json \
  --map zs_deadend --live-time-controls \
  --output build/time-controls-dedicated-new
```

该选项从时间关闭状态冷启动`cs-dione-probe`，验证完整HUD闭包、实际加载映像/PDB、同一规则对象和地图runtime generation。通过真实RCON依次开启、修改17/19秒、拒绝负时长、关闭和重开；无效值必须观察到实际Think的诊断字段，不以尚未执行的旧值通过。控制台临时修改不得改写统一JSON。所有临时运行文件独立替换并最终恢复；不启动B、不接管界面、不改正式服。

此处没有加入玩家或人为启动回合，`round=0`明确保留。它证明真实引擎CVAR应用和冷准备，不证明战斗阶段的计时/重玩、环境/BGM实际播放、客户端画面或多人通过；这些仍由真实规则SDK和后续对局分别验收。
