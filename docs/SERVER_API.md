# AMXX 与 ReAPI 接入

运行链为 ReHLDS → Metamod-R → ReGameDLL_CS / AMX Mod X。GoldCraft AMXX 模块通过 `CreateInterface` 获取本项目 ReGameDLL 的服务器控制 API；不会向 Pawn 暴露裸指针或猜测实体偏移。匹配源码版本见 `sources.lock.json`，官方运行包及哈希见 `tools/Prepare-AMXX.ps1`。

| 组件 | 当前固定版本 |
|---|---|
| Metamod-R | 1.3.0.149 |
| AMX Mod X | 1.9.0.5303 |
| ReAPI | 5.29.0.358 |
| ReHLDS / ReGameDLL API | 3.15 / 5.30 |
| GoldCraft Pawn API | 1 |

## 使用形态接口

包含 `goldcraft.inc`，以 AMXX 玩家槽位调用：

| 接口 | 结果 |
|---|---|
| `gc_api_version()` | 可用时返回 1，服务器接口不可用返回 0 |
| `gc_get_form(id)` | `GC_FORM_CS` / `GC_FORM_MINECRAFT`，断开返回 -1 |
| `gc_is_paired(id)` | 是否存在匹配的 Minecraft 配对 |
| `gc_set_form(id, form)` | 1 已切换、0 未变化、-1 参数无效、-2 未配对、-3 不可用、-4 代次耗尽 |
| `goldcraft_form_changed(id, previous, current)` | 实际形态变更后的 forward |

`gc_set_form` 在权威游戏服务器执行，撤销旧移动更新权限。实际示例是 `amxx/goldcraft.sma`：注册 `goldcraft_menu`、`/mc`、`/cs`、`amx_gc_form` 和 `gc_amxx_status`，并通过匹配的 ReAPI `RG_CBasePlayer_Spawn` 后置钩子观察出生。菜单使用 AMXX ShowMenu/menuselect，选择仍受服务器配对和状态检查。

```powershell
.\tools\Build-AMXX.ps1 -Plugins goldcraft
```

ReAPI 的 native 参数、成员类型和 hook 回调以下载的对应源码及 Pawn 声明为准。版本不匹配时先更新锁文件、检查 API 并重新验证，不能用另一个版本的内存偏移替代声明。

## 编译后重载

标准 Pawn 插件通常在下一次地图加载时重新载入。`sv_restart` / `sv_restartround` 是回合重启，不会重新载入 `.amxx`；暂停/取消暂停也不会重新读取新字节码。开发中替换 Pawn 文件后通过 `changelevel cs_assault` 换图可以保留 ReHLDS 进程，但会清空当前地图会话的 MC 建筑。

更换 AMXX 模块、ReAPI、ReGameDLL 或 ReHLDS 二进制时使用停止并重启流程。沙箱部署器在目标进程运行时拒绝覆盖 DLL。完整验证仍需覆盖两客户端形态隔离、死亡/重生和换图后的重新配对。

`goldcraft_test.sma`、`goldcraft_headless_test.sma` 是本地测试夹具。它们提供设定生命、位置和真实武器伤害的测试入口，不属于对外服务器的默认配置。
