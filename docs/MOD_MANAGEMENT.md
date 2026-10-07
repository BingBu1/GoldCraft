# 在 X Minecraft Launcher 集中管理 Mod

当前只管理本机 A、B 两个 MC 客户端及一个 MC 专用服务器。Minecraft 版本固定 **1.21.1**，加载器固定 **NeoForge 21.1.256 / FML 4.0.45**。现有个人 Minecraft 安装、存档和 X 管理的其他实例不会成为同步目标。

## 一次准备

完成 [构建与部署](BUILD.md) 后，运行：

```powershell
.\tools\Prepare-XmclModpack.ps1 -Loader neoforge
```

关闭 X Minecraft Launcher，然后运行 `sandbox/modpack-neoforge/Add-to-XMCL.cmd`。脚本只向现有 X 实例注册表追加外部主实例，备份原表并保留其他实例和当前选择；X 运行时拒绝并发写入。重新打开 X，选择新增的 GoldCraft 1.21.1 实例管理 Mod。实例的版本标识和库目录来自工作区内已校验的官方安装结果。

| 路径 | 用途 |
|---|---|
| `sandbox/modpack-neoforge/GoldCraft-1.21.1-NeoForge` | 在 X 中管理的当前 Mod 主实例 |
| `sandbox/neoforge-cs-client-a` | A 的实际运行目录 |
| `sandbox/neoforge-cs-client-b` | B 的实际运行目录 |
| `sandbox/neoforge-server` | 专用 MC 服务端运行目录 |
| `sandbox/modpack-neoforge/rules.json` | 运行端分配及需要共享的配置 |

主实例用于版本和 Mod 内容管理。完整 CS/MC 联动应由生成的 `Sync-and-Start.cmd` 启动；X 单独启动主实例不会自动启动 ReHLDS 和配对的 CS 客户端。

## 安装、更新和移除

只在主实例操作一次。随后运行 `sandbox/modpack-neoforge/Sync-and-Start.cmd`，或：

```powershell
.\tools\Sync-Modpack.ps1 -ValidateOnly -Loader neoforge
.\tools\Sync-Modpack.ps1 -Restart -Start -Loader neoforge
```

先验证双方依赖与版本，确认没有冲突后再停止实例、备份、同步增删更新并重启。核心 GoldCraft 的新构建也纳入同一事务。目标文件若被手工修改，工具报告冲突并保留。Mod 不能靠 MC 资源重载热装载；需要 JVM 重启。完整集群重启会按地图策略清空本轮建筑。

## 客户端和服务端专用 Mod

NeoForge 的 `dependencies.side` 限定一条依赖在何端生效，**不是整个 Mod 的安装端声明**。默认分配到两端；确定仅一端可用的 Mod，要按实际 Mod ID 配置 `sideOverrides`。例如 `rules.json` 的结构如下，示例 ID 需要换成所安装 Mod 的真实 ID：

```json
{
  "format": 1,
  "sideOverrides": {
    "example_client_mod": "client",
    "example_server_mod": "server"
  },
  "sharedConfigs": ["config/example-common.toml"]
}
```

`sharedConfigs` 默认为空；只列需要统一管理且已存在于主实例的具体配置文件。不要填目录，也不要放配对凭据、账号或每个玩家的按键。A/B 个人设置与服务器私有配置独立保留。

依赖预检调用固定 FML 的真实 ModFileParser、ModSorter 及 JarJarSelector。支持 `javafml` 和 `lowcodefml`，其他语言加载器目前明确拒绝；不会根据文件名猜兼容性。预检通过只能说明已检查的依赖关系成立，不能证明任意 Mod 的渲染/Mixin/玩法一定兼容 GoldCraft。

支持通过 manifest 声明的 `GAMELIBRARY` / `LIBRARY` 容器，并由真实 JarJar 选择嵌套依赖。容器在管理清单中的 `library:<模块名>` 是分配规则标识；实际加载的 Mod ID 仍取自 FML。依赖判定包含 FML 的 `VersionSupportMatrix`：例如当前加载器允许部分声明支持 1.21 的 Mod 在 1.21.1 运行。

从旧 1.21 主实例升级时，保留旧目录，将新版本 Mod 放入 1.21.1 主实例并重新预检。不要同时把旧、新两个 JAR 安装到同一主实例；重复 Mod ID 会被拒绝。A/B 和服务器自己的按键、账号、桥接凭据及存档路径继续保留。

## 当前验证范围

1.21.1 主实例已准备，GoldCraft、JEI 19.57.0.451 和 MezzConfig 0.6.8 已同步到本机 A/B/服务端，三端文件哈希一致、独立配置保持不变。正式运行环境的独立服务端已真实加载这三个 Mod，并核对了实际版本；28 项依赖与文件事务测试通过。

升级后的 X 实例登记、图形客户端 JEI 界面，以及真实三个 JVM 的增删更新回归尚待完成。此前 1.21 的双客户端启动或 Fabric 运行记录不能替代这些验收。
