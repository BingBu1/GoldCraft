# 灾变账户 hook 与消息测试

`tools/Test-CSOLAccountHooks.py` 提取完整生产 AddAccount wrapper/OrigFunc 和 guard 栈，
包含真实 ReGameDLL hookchain 头及 registry 实现。SDK scope/access/实体 serial 和消息
出口是可控边界，测试不等于完整 ReGameDLL 或 Pawn VM。

在配置 x86 Clang 环境后运行：

```powershell
python tools/Test-CSOLAccountHooks.py --clang <clang-cl.exe> --output build/account-hooks-add
python tools/Test-CSOLAccountHooks.py --clang <clang-cl.exe> --output build/account-hooks-noadd --no-add
```

每组 162 项覆盖 callNext/callOriginal、取消、改额、规则替换、access撤销、实体代次、
断连、FL_KILLME、递归和 post-hook 保护。普通模式保持原 hook 行为。非 ADD 组不
声称支持灾变 RESET 理由；当前正式构建固定启用 REGAMEDLL_ADD。

`Test-CSOLMoneyMessages.py` 使用同一完整 wrapper 和保护栈生成消息，再执行匹配
客户端原 Money reader 的机器码核对；26 组覆盖初始7500、上限32000、重置、购买、
奖励、整数边界和普通CS自定义金额。实际客户端画面、Pawn hook和专服仍需实测。
