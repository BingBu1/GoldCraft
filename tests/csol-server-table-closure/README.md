# 灾变服务端资源闭包

`tools/csol_server_table_closure.py` 对选定冻结源码、已验证的本地部署和 common 资源进行只读检查，将旧基线 ZIP 与后续更新 ZIP 按安装顺序合并，输出仍缺失或内容不同的打包输入。真实 ZIP 成员必须存在；用于省略的资源还会读取 ZIP 内字节核 SHA-256。

包含地图表、升级和角色技能表、实际角色下载 manifest、购买菜单 JSON 引用的贴图、已接入武器的明确预缓存资源。未从旧更新包获得覆盖证据的标准游戏资源会保守列入补充，不能凭本地存在或 HTTP 状态假定远端已有。

运行 Python 回归：

```powershell
.tools/hlam-venv/Scripts/python.exe -X utf8 -B tests/csol-server-table-closure/test_closure.py
```

检查真实资源时传入 `--source-root`（与部署 DLL 对应的冻结源目录）、`--deployed`（已验证服务端 cstrike）、`--common`、`--map`、依次重复的 `--archive`（旧基线在前）和新的 `--output` JSON 路径。用 `--dependency-list` 添加匹配的生产 `csol_resources --relocatable-json` 解析结果，覆盖模型外部纹理、SEQ 和事件资源；保留解析器身份和运行回执。输出的 `files` 可合入更新包的输入表；它不复制资源、编译或部署。

验收边界：文件身份及闭包检查不等同原 C++ 表解析、正式服启动或真人入服通过。生成的角色、菜单、武器配置仍须和实际部署回执核对；不把正在开发的客户端资源混入稳定补包。
