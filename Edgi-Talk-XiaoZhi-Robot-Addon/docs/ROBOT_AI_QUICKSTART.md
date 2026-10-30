# 用 Edgi-Talk 的小智 AI 控制 UNO 舵机

本轮更新：2026-10-28。代码和固件已经准备；尚未代你烧录或完成实机语音验证。

**后续修正：** 针对“两端已烧录，但小智说没有点头动作”，已修复 M55 的工具发现通信缺陷。
请使用带 `mcp-pages-v2` 标记的新 M55 固件，见 [修复及验证步骤](ROBOT_MCP_DISCOVERY_FIX.md)。此轮只需更新 M55。

## 1. 本次具体修改

- UNO：拒绝 NUL、二进制字节、行内 CR 和重复 CR；仅接收 LF 或 CRLF 结尾。字节间隔超过 150ms 的半条命令丢弃到下一个 LF，避免拼接旧命令。接收缓冲区溢出后清空并重新同步；每轮最多处理 64 字节，给缓动和失联回中留下执行机会。
- M55：同样严格检查回复换行，防止损坏的 ACK 被当作有效回复；修正碎片接收时字节预算可能下溢的问题，确保每轮最多读取 256 字节；保留 PING/PONG、回中确认、心跳、超时恢复。
- 小智 MCP：工具说明增加中文“点头、摇头、回中、设置角度、关闭自动动作”的语义对应；离线等执行错误返回 `isError=true`；角度越界或小数直接拒绝，不再悄悄截断或夹到边界。补充 `[robot MCP]` 调用日志；JSON-RPC 参数错误包含错误码并正确转义。
- 保持原接线、38400 8N1 和协议版本 1；本轮未再修改 M33。M33 必须已经使用释放 uart5 的版本。

## 2. 接线

![功能接线图](robot_wiring.png)

图是功能接线示意，不是插头朝向图；Edgi 的数字指 **CN5 物理脚号**，不是 GPIO 编号。

| 起点 | 终点 |
|---|---|
| Edgi CN5 物理 8 / TX | HW-221 A1 |
| HW-221 B1 | UNO D2 / RX |
| UNO D3 / TX | HW-221 B2 |
| HW-221 A2 | Edgi CN5 物理 10 / RX |
| Edgi CN5 物理 1 或 17 / 3.3V | HW-221 VCCA；OE 按原接线接同一 3.3V |
| UNO 5V 逻辑电源 | HW-221 VCCB |
| Edgi CN5 物理 6 / GND | HW-221 GND、UNO GND、舵机电源负极、两舵机 GND |
| UNO D9 对应信号 S | Pan 水平舵机信号线 |
| UNO D10 对应信号 S | Tilt 俯仰舵机信号线 |
| 独立舵机电源正极 | 两舵机的电源正极 V+ |

Edgi 和 UNO 可分别用各自 USB 供电；**不需要把两块板的 5V 输出互接**。不要通过 Edgi 排针或 UNO 5V 给两个舵机供电。舵机供电电压按具体型号铭牌/手册确认，电流容量需覆盖同时启动或堵转；不能仅凭“SG996”名称假定参数。

你先前的 UNO 扩展板图片未成功加载，因此这里不猜测它的 S/V/G 排列、外接电源端子或跳线位置。若扩展板 V 排与 UNO 5V 相通，不能直接把独立电源正极接上；先确认其供电隔离方式，或仅从扩展板引出 S/G，舵机正极直接接独立电源。

OE 常接 3.3V 延续你的原方案。正式的上掉电控制应让 OE 在两侧电源稳定前保持低电平、稳定后再使能，具体硬件方案需另行落实；本固件没有占用 GPIO 控制 OE。TI 文档规定 OE 低时端口为高阻，A/B 电源分别参考 VCCA/VCCB：[TXS0108E 数据手册](https://www.ti.com/lit/ds/symlink/txs0108e.pdf)。不要将 OE 接 5V，也不要把 6V 舵机电源接 VCCB。

UNO SoftwareSerial 不能同时收发，因此 M55 保留单条在途指令和发送间隔；38400 并不能消除 Servo 中断与软件串口的时序干扰，抖动和误码仍需实机验证：[Arduino SoftwareSerial 文档](https://docs.arduino.cc/learn/built-in-libraries/software-serial)。连线尽量短，信号线远离舵机大电流回路。

## 3. 烧录顺序

1. Arduino IDE 打开 `arduino/EdgiTalkUnoServo/EdgiTalkUnoServo.ino`，开发板选 **Arduino Uno / ATmega328P**，选对应 USB 端口后上传。如果提示 `Servo.h` 不存在，在库管理器安装 Arduino 的 **Servo**；SoftwareSerial 随 Arduino AVR Boards 平台提供。不要用只会循环扫动的 `EdgiTalk_Servo_Test.ino`。
2. 如果还没有更新过释放 uart5 的 M33，请编译并烧录 `m33-overlay/` 覆盖后的 M33 非安全应用。这一步通常只做一次。旧 M33 会与 M55 争用 SCB5，影响通信。
3. 编译并烧录覆盖后的 M55 工程，然后整板重启。使用 IDE 生成镜像自带的地址；保留原有安全固件、文件系统和 Wi-Fi 数据，不做整片擦除。

UNO 的 D0/D1 未用于此链路；Arduino IDE 的 USB 串口监视器不能直接给 D2/D3 发送动作命令。下面的 `robot` 命令输入在 **M55 的 uart2 调试串口 / MSH（115200）**。

## 4. 先验证串口与小角度动作

先让机构有运动余量；默认 Pan 20..160、Tilt 30..150 是软件限制，尚未按你的实际机构标定。先测 90/90 附近，不要第一次就试极限角度。

```text
robot status
robot auto off
robot center
robot pose 95 95
robot status
robot nod
robot shake
```

逐条执行，姿态命令间等待约 1 秒，点头/摇头后等待约 3 秒。`robot auto off` 能避免说话、聆听和表情自动动作干扰测试。

应看到 `UNO online: PONG and CENTER acknowledged` 和 `online=1`。小角度命令完成通信后，`target=95,95` 与 `UNO_ack=95,95` 应一致；timeouts/errors 不应持续增长。`UNO_ack` 只是 UNO 已接收目标，**不是舵机实测角度**。

`online=0` 时不继续用语音猜问题：先检查 UNO 程序、M33 uart5 归属、D2/D3、A/B 通道、供电、OE 和共地。软件链路成立后，再进行语音测试。

## 5. 通过小智 AI 控制

链路为：说话 → 语音识别 → 小智模型选择设备 MCP 工具 → M55 排队动作 → uart5 → UNO → D9/D10。

1. 让 Edgi-Talk 联网，并确认小智能正常听懂并回答普通问题。
2. 更新固件后重启或重新连接小智会话，让服务端重新获取工具。启动日志会列出四个 `self.robot.*` 工具，协议 hello 已声明 `features.mcp=true`。
3. 先说 **“小瑞”**，等待屏幕/提示音进入聆听，再说下面的句子。当前本地唤醒模型识别的是 xiaorui，不是随意更换的名字。
4. 第一次验证时先说“关闭自动头部动作”，再发出单个动作请求。这样观察到的点头能更明确地对应工具调用，而非 AI 说话时的自动点头。

| 你说的话 | AI 应调用的工具及参数 |
|---|---|
| 请点点头 | `self.robot.action`，`action="nod"` |
| 请摇摇头 | `self.robot.action`，`action="shake"` |
| 把头回正 / 舵机回中 | `self.robot.action`，`action="center"` |
| 水平舵机设为95度，俯仰舵机设为100度 | `self.robot.set_pose`，`pan=95, tilt=100` |
| 关闭自动头部动作 | `self.robot.set_auto`，`enabled=false` |
| 开启自动头部动作 | `self.robot.set_auto`，`enabled=true` |
| 查询舵机是否在线 | `self.robot.get_status`，无参数 |

这些是自然语言示例，**不是 M55 内写死的关键词匹配**。不需要对麦克风念 `PAN=...`，也不需要为本地设备工具填写额外 MCP 接入地址。服务端必须支持并允许调用设备 MCP 工具；设备固件无法强迫一个不支持工具调用的云端模型执行动作。

如果 AI 总是口头答应却不调用工具，可在你使用的小智智能体“角色说明/系统提示词”中补充下段内容（具体后台菜单随服务端版本而异）：

```text
你是拥有实体头部的机器人。用户要求点头、摇头、回中、设置舵机角度时，
应调用设备提供的 self.robot 工具，不要只用文字描述动作。
点头调用 self.robot.action，action=nod；摇头用 shake；回中用 center。
指定角度调用 self.robot.set_pose，同时提供整数 pan 和 tilt；
pan 允许20至160，tilt允许30至150，中心90/90；超出范围请说明范围，不擅自改值。
开启/关闭自动动作调用 self.robot.set_auto，enabled为true/false。
查询连接调用 self.robot.get_status。
工具返回queued表示已提交，不表示舵机物理动作完成；
offline或isError=true时如实说明失败，不宣称已经完成动作。
未校准安装方向前，不把角度增减直接当作向左、向右、抬头或低头。
```

手动/AI 动作优先于自动表情，自动表情暂让约 10 秒。若希望保持指定姿态，先关闭自动动作；后续需恢复时再说“开启自动头部动作”。关闭自动不等于断开舵机电源，也不关闭显式动作和通信心跳。

## 6. 用日志判断问题在哪一段

以“点点头”为例，M55 调试串口应能看到：

```text
stt:请点点头
[robot MCP] action=nod result=0
```

随后 `robot status` 的确认计数增长，序列结束后目标回到 90/90。`result=0` 表示 M55 接受排队；最终仍需要看 UNO ACK。

| 现象 | 优先检查 |
|---|---|
| 没有 STT 文字或识别错误 | 唤醒、麦克风、网络、普通对话是否正常 |
| 有 STT，但没有 `[robot MCP]` 日志 | 服务端是否获取了设备工具、模型工具调用能力、角色提示词 |
| 有 MCP 日志，但 result 非 0 | `robot status` 是否 online、动作参数是否有效 |
| result=0，但超时计数增长 | 双向串口、电平转换、供电干扰、UNO 是否重启 |
| ACK 正常但舵机不动 | D9/D10 信号、扩展板 S/V/G、独立供电、机械卡阻 |
| 说话时动，命令没调用也动 | 自动动作还开着；先 `robot auto off` 再验证 |

通信停止约 5 秒后，UNO 会向 90/90 缓动回中。恢复通信后 M55 重新握手并回中，不重放断线前的动作。该保护依赖 UNO 仍正常运行和供电。

## 7. 验证范围

自动化测试覆盖两端真实协议代码（UNO 串口/Servo/时间用模拟替身）及 MCP 结果序列化和数值校验。测试并不能替代实机的电平、时序、供电、机械限位及云端工具选择验证。

复现：`tests/robot/run.ps1`、`tests/robot/build_uno.ps1`、`python tests/robot/verify_images.py`。
最终编译大小和测试记录见同目录 `ROBOT_UNO_INTEGRATION.md`。
