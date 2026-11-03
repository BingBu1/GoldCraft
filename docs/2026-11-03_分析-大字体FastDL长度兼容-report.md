# 大字体 FastDL 长度兼容复核

匹配沙盒 B 的 GoldSrc 10210 可以经原生 HTTP 完成回调保存大于 16 MiB 的普通资源。资源列表的 24 位 `nDownloadSize` 不限制该回调的正文分配或写入长度。当前 22,309,736 字节字体被内置 HTTP 的旧单文件上限拒绝，需要修正普通资源 HTTP 上限，无需为这份字体扩大资源消息字段或改动喷漆上传协议。

## 证据与方法

目标仅为项目内 `sandbox/cs-client-b/Half-Life/hw.dll`，x86，SHA-256 `9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295`。使用现有函数目录、已保存的匹配下载反汇编和本轮精确 PE 字节；没有启动 IDA、加载原 DLL、编译或部署。完整证据位于 `build/csol-large-fastdl-20261103`。

| Evidence | Finding | Path |
|---|---|---|
| `101A7690`、当前两端扩展资源解析源码 | wire size 为 24 位，存 resource+48 | 资源列表 → 预计缺失字节 |
| `101A67B0`、`101C75D0` | HTTP 队列只接文件名，不传 resource 指针或声明大小 | 缺文件 → 独立 HTTP 请求 |
| `101C5640` 完整回调 | Steam HTTP v2C 返回完整 32 位正文大小，同值用于分配、取正文与 FS_Write | HTTP 完成 → 正文 → GAMEDOWNLOAD |
| `101C8470`、匹配 IFileSystem 接口 | 原 FS_Write 将完整 int size 送 v58，无 24 位裁切 | 原回调 → 原包装 → 文件系统 Write |
| `101A8DF0`、`101C7D20` | 普通 generic 重连只判断 FileSize 是否为 -1，不和 wire size 比较 | 重连 → 已存在文件接受 |
| `101A497F..101A49DF`、专服 `SV_ProcessFile` | 宣称大小等值校验在 `!` 自定义/HPAK 分支 | 自定义喷漆单独处理，保留原校验 |

复现：`python -X utf8 -B build/csol-large-fastdl-20261103/verify.py`。脚本保存 12 段原指令与原字节，在 Unicorn 中执行原完成回调和原 FS_Write 包装，Steam API、分配器及文件系统端点为明确夹具边界。9 组用例通过，覆盖 1 字节、16 MiB−1、16 MiB、实际字体、404、零长度、请求失败、I/O 失败与目标已存在。

实际字体用例中，Steam body size、分配大小、取正文长度、IFileSystem Write 长度及夹具最终文件尺寸均为 **22,309,736**。输入与临时落盘 SHA-256 都是 `ac175bfd890532c8b65bc030359bd0cb92930607888872f8806aa2983d87eac7`。成功用例的临时正文副本验证后自动清理。该结果是原指令受控回放，未经过真实 Steam HTTP 网络，不能当作客户端入服验收。

## 24 位字段的实际用途

原 `CL_ParseResourceList` 在 `101A7703` 压入 24，调用读位函数后，`101A770F` 写入资源 `+48`。当前 GoldCraft 扩展解析也保留 24 位。

ReHLDS `MSG_WriteBits` 对超范围值采用**饱和钳制**，不是按低 24 位取模。字体的完整大小 22,309,736 在资源消息中因此为 16,777,215，而不是 5,532,520。当前专服内存中的 `gc_precache.resources` 仍保存原 int 大小；HTTP 发布直接读取这个完整值，消息编码不会回写资源记录。

`101B7320` 累加 `resource+48` 得到资源字节统计。`CL_StartResourceDownloading` 的普通 generic 分支调用 `FileSize(name)`，仅判断返回值是否为 -1；缺失时置标记并把声明大小加到预计下载字节。它没有要求本地真实文件长度等于声明大小。

`101A67B0` 对缺文件选择 HTTP 时，只将规范化后的文件名传给 `101C75D0`。后者分配 `C0C` 字节队列记录，保存 URL、文件名、服务器地址及 request handle，没有资源大小字段输入。

HTTP 期间进度另走 SteamHTTP v3C 的 `GetHTTPDownloadProgressPct`，不是以资源声明的 24 位长度截断正文。24 位字段仍可能让总资源/游戏通道的预计字节量低估，故不能笼统声称整个协议中的该字段“没有用”；本结论限定为普通 HTTP 完整体传输路径。

## 完成回调和落盘

`101C5640` 检查回调有效、无 I/O failure、请求成功，以及状态码 200/304。`101C5713` 调用 Steam HTTP vtable+2C，得到 32 位 body size 存到栈局部 `ebp-11C`，拒绝零长度。

同一局部值在 `101C578C` 用作缓冲区分配大小，`101C57C7` 用作 vtable+30 的正文读取长度，最后 `101C585A` 用作 FS_Write 长度。整个回调没有与 resource+48 比较，也没有 `FFFFFF` 掩码或 16 MiB 上限分支。

目标路径 ID 为 `GAMEDOWNLOAD`，打开模式 `wb`。已存在文件不覆盖。`101C8470` 检查包装层 count 为 1，随后把原始 size 作为 int 传入文件系统 vtable+58，未裁切。这里仍受原接口 int/分配器容量约束，不能由 22 MB 通过就推广到任意 GB 文件。

原回调没有检查正文读取与写入返回值后再决定成功；这是现存引擎行为。本次不扩展成下载器重写。真实联机验收必须对完整 GET、客户端下载后的大小及 SHA 作比较，不能只看到 HTTP 200 或文件存在就算成功。

## 与喷漆/HPAK 的区别

`CL_ProcessFile` 在 `101A4989` 先检查名称首字节是否为 `!`；普通文件直接跳过后面的声明大小等值检查。仅自定义路径在 `101A499F` 读取 resource+48，与 `netchan.tempbuffersize` 比较，随后还要求 RES_CUSTOM 才进入 HPAK。

匹配 ReHLDS 源 `SV_ProcessFile` 同样首先拒绝非 `!` 上传，再按 MD5 找已请求的自定义资源，比较完整上传尺寸并校验 decal，最后写入 `custom.hpk`。客户端上传资源声明读取 `MSG_ReadLong`，并不是服务器普通资源列表那一个 24 位字段。高清喷漆独立 HTTP 上传另有自己的边界，不应随着普通资源下载上限一起放宽。

## 最小兼容方案和交付边界

普通资源单体上限可独立设为 64 MiB，与现有 Store 总缓存预算一致，同时保留实际发布清单、路径白名单、加载预算、文件大小/时间复核和完整 HTTP Content-Length。启动器 payload 与喷漆限制保持独立；不能因为共享常量扩大而顺带放宽这些入口。

此次只读核验没有修改生产源、运行端、共享状态文档或 Git index。父任务负责上限和精确 scheme 路径修复，验证代理负责 HTTP 200/Range、全长 SHA、冷客户端真实下载和入服。9 组原指令回放不能替代这些联机验证。

早期夹具的缓冲区清零范围和大正文映射范围修正后，9 组验证全部通过；前者曾覆盖夹具局部值，后者曾与模拟回调 trampoline 重叠，均为夹具故障，不是产品首败。中断实验遗留一个空目录 `build/csol-large-fastdl-20261103/callback-sink-7agd_d00`，清理被自动审批以 `blocked by policy` 拒绝，保留待用户处理。可在项目根目录的 CMD 执行 `rmdir "build\csol-large-fastdl-20261103\callback-sink-7agd_d00"`；非递归命令只会删除空目录。
