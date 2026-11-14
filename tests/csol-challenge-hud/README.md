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
