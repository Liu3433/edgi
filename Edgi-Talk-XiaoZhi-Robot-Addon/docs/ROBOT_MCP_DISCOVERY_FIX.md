# “小智说没有点头动作”排查与修正（2026-10-28）

用户已确认：对 Edgi-Talk 麦克风说话，且先前的 M55 和 UNO 均已更新。
本轮没有获得设备现场日志，不能断言全部故障已消除；以下是源码中确认的缺陷及修复。

## 是否需要网页配置

当前实现使用设备端 MCP：M55 注册 `self.robot.action` 等工具，小智服务端通过
`initialize`、`tools/list` 发现，再用 `tools/call` 调用。正常情况下无需在网页逐条创建
“点头”“摇头”，也无需填写外部 MCP 接入点。网页角色提示词只能帮助模型选择已经发现的工具，
不能替代工具注册和通信。

依据：[小智项目设备 MCP 使用说明](https://github.com/78/xiaozhi-esp32/blob/main/docs/mcp-usage_zh.md)。

## 源码中确认的问题

1. **完整消息缺少 FIN**：`wsock_write()` 对长度达到 1420 字节的消息不设置 FIN，
   但它实际只发送一个完整帧，没有后续 continuation 帧。MCP 工具列表原预算为 8000 字节，
   很容易触发此问题，使接收方等待未发送的后续片段。这是设备支持工具但服务端未能发现工具的一种直接原因。
2. **会话 ID 截断**：接收 hello 时只复制 9 个字符，缓冲区只有 12 字节。
   如果服务端提供较长 ID，后续回复使用的会话值就不正确。改为 64 字节缓冲区并完整复制；
   缺失、空值和超长 ID 明确拒绝，不静默截断。
3. **诊断日志失效**：`rt_kprintf(TAG, "Received method: ...")` 实际只打印 TAG，
   看不到方法名称；发送返回值也未检查。现在打印收到的方法、分页数量、字节数及发送结果。

本轮还将工具列表结果限制为每页 1024 字节，包含 `nextCursor`，服务端继续请求下一页。
单个工具超大或游标无效时明确报错；不会静默丢工具。

MCP 回调位于 lwIP 接收流程，底层 `wsock_write` 已使用 TCPIP core 锁。
这里没有再等待音频互斥锁，以免形成 core 锁和音频锁的反向等待。

## 你需要执行的步骤

1. **重新编译并烧录覆盖后的 M55 工程到 M55**。
   UNO 和 M33 没有源码变更时无需重复更新。
2. 整板重启、联网。M55 调试串口应看到版本标记：

   ```text
   [MCP] hello accepted; session_length=...; robot firmware=mcp-pages-v2
   [MCP] Add tool: self.robot.action
   ```

3. 随后观察服务端发现工具的过程，具体页数/字节数取决于工具：

   ```text
   [MCP] rx method=initialize
   [MCP] tx bytes=... result=0
   [MCP] rx method=tools/list
   [MCP] tools/list total=... page=... bytes=... next=...
   [MCP] tx bytes=... result=0
   ... 服务端按 nextCursor 继续请求 ...
   [MCP] tools/list total=... page=... bytes=... next=<end>
   ```

   注意：`Add tool` 仅代表本机注册；`tx result=0` 仅代表本地发送成功，不能单独证明云端模型已经使用了工具。

4. M55 终端执行 `robot status`，应为 `online=1`；执行 `robot auto off`、`robot nod` 验证实体动作。
   然后说“小瑞”，进入聆听后说“请点点头”。应出现：

   ```text
   [MCP] rx method=tools/call
   [robot MCP] action=nod result=0
   ```

5. 若工具分页发送完成、终端动作正常，但语音仍不调用工具，再检查所绑定智能体/服务端的工具调用能力和角色提示词。
   提示词可加：“用户要求点头时，调用设备工具 self.robot.action，参数 action=nod；不要只口头描述动作。”

没有新版本标记：先排查烧录目标或固件文件。没有 initialize/tools/list：排查服务端 MCP 会话。
有 tools/list 但未请求后续页：排查服务端分页支持。有 tools/call 且执行失败：查看 robot status 的链路状态。

## 本地验证范围

- 15 项实际 UNO/M55 协议互操作测试。
- 5 项 MCP/会话测试：包含分页不遗漏或重复、UTF-8 字节预算、长会话 ID 保存和异常输入拒绝。
- 1 项实际 `wsock_write` 回归测试，替换 TCP/pbuf 为模拟实现：覆盖 0、20、125、126、1419、1420、1421、4096、8000 字节，检查 FIN、掩码、声明长度与负载。
- 发布前请对最终生成的固件校验地址边界与校验和；不要把本地构建日志和二进制固件提交到仓库。
- 未执行本次固件烧录，尚未验证服务端实际发现与语音工具调用。

固件 SHA256：`a5526bd20e6e1ee0c4da53a0311779bb644c187453bd078fe90d190777186a86`。
