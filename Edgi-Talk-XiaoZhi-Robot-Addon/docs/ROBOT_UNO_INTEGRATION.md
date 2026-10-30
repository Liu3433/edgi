# M55 / UNO 机器人集成（更新于 2026-10-28）

已完成源码集成与本轮两端优化，未执行烧录或物理舵机联调。协议仍为版本 1。
本轮修改了 UNO 接收器、M55 回复解析及小智 MCP 工具错误处理/中文说明。
接线图、语音口令和完整操作见 [ROBOT_AI_QUICKSTART.md](ROBOT_AI_QUICKSTART.md)。
针对用户实机反馈的“没有点头动作”，又修复了工具发现链路，见 [MCP 发现修复](ROBOT_MCP_DISCOVERY_FIX.md)。

## 1. 已核对的连线与 UART 归属

核对依据：Edgi-Talk 基础/核心原理图、SDK BSP 的 `cycfg_routing.h`，以及实际底板丝印。

| 路径 | 实际连接 |
|---|---|
| M55 TX | SCB5 / P17.1 → 底板 U6 电平转换 → CN5 物理 8 脚 → HW-221 A1/B1 → UNO D2 RX |
| M55 RX | UNO D3 TX → HW-221 B2/A2 → CN5 物理 10 脚 → 底板 U6 → SCB5 / P17.0 |
| 电平转换供电 | HW-221 VCCA=排针 3.3V，VCCB=5V，OE=3.3V 常使能 |
| 地 | Edgi、HW-221、UNO、舵机电源共地 |
| 舵机 | UNO D9=Pan，D10=Tilt；独立的合适舵机电源 |

这里的软件设备是 **uart5**。`uart1` 的 P9.2/P9.3 对应其他信号，不能把它当作
CN5 的 8/10 脚。生成代码中的 `CYBSP_DEBUG_UART_M33` 是 SCB5 的现有名称，
名称不代表必须由 M33 驱动。

M55 继续使用 `uart2` 输出日志和 MSH。HW-221 的 OE 按提供的接线图常接 3.3V；
本实现没有擅自占用额外 GPIO。如果实际 OE 只接了下拉、没有拉高，则 UART 不会通。

## 2. 必要的一次性 M33 修正

配套 M33 工程的原始配置会把 `uart5` 作为控制台，
FinSH 还会开启其接收中断。这样两核会争用同一个 SCB、FIFO 和时钟。

已同步修改该 M33 工程的 `.config` 与 `rtconfig.h`：关闭 `BSP_USING_UART5`、
`RT_USING_CONSOLE`、`RT_USING_FINSH`/`RT_USING_MSH` 及相关命令行配置。
保留启动 M55 的 `SOC_Enable_CM55`、启动代码、启动地址及 IPC 配置。
M33 原来的串口日志和命令行将不可用；日常调试使用 M55 的 uart2。

`integration/m33-release-uart5.patch` 保存此次配置差异，已经应用到上述 M33 工程，
无需重复应用。M33 已重新编译。首次部署需要更新 M33，之后通常只更新 M55。

## 3. 软件结构

| 文件 | 职责 |
|---|---|
| `applications/robot/robot_link.c/.h` | 无 RTOS 依赖的串口协议、握手、ACK、心跳、断线恢复 |
| `applications/robot/robot.c/.h` | 独立 RT-Thread 线程、动作序列、状态/情绪入口、MSH 命令 |
| `applications/robot/SConscript` | `BSP_USING_ROBOT` 编译入口 |
| `applications/xiaozhi/xiaozhi.cpp` | 状态切换统一通过 `xz_set_state()` 通知机器人 |
| `applications/xiaozhi/ui/xiaozhi_ui.c` | 表情变化通知机器人，覆盖 LLM 和屏幕 MCP/IoT |
| `applications/xiaozhi/mcp/mcp_server.cpp` | AI 可调用的四个机器人工具 |
| `libraries/HAL_Drivers/drv_uart.c` | ISR 使用非阻塞 FIFO 读取；初始化错误返回失败而非断言停机 |

只有 robot 线程读写 uart5。网络、按键、音频、UI 和 MCP 回调只提交意图，
不等待串口回复。手动姿态采用最新请求覆盖旧请求，不积压过时动作。

## 4. 两端协议合同

- 38400 baud，8 数据位、1 停止位、无校验位，无 RTS/CTS。
- ASCII，M55 发 LF；UNO 的 CRLF 回复也能识别。
- 一次只允许一条在途指令，400ms 回复超时；两次发送起点至少间隔 200ms。
- 没有动作时每 1000ms 发送 PING；动作命令本身也刷新 UNO 的 5 秒看门狗。
- Pan 20..160，Tilt 30..150，中心 90/90。与 UNO 常量一致，尚未经过实物机械标定。
- 协议只有格式、范围及回复目标匹配检查，**没有 CRC 或序列号**。不能宣称可以检测所有线路误码。

```text
M55 → UNO: PING\n
UNO → M55: PONG,PAN=90,TILT=90\r\n
M55 → UNO: CENTER\n
UNO → M55: OK,PAN=90,TILT=90\r\n
              ↑ 至此才标记 online=1

M55 → UNO: PAN=100,TILT=120\n
UNO → M55: OK,PAN=100,TILT=120\r\n
```

第一次探测前会额外发送 LF，清除 UNO 上可能残留的半条命令。
即使 UNO 启动时的 READY 已丢失，也能通过 PING 建立连接。
启动探测中由清除半包产生的 ERR 不作为成功回复；仍须收到 PONG 和回中 ACK。

超时、动作 ERR、ACK 目标不符、READY 重启通知、SAFE 回中通知都会使链路离线，
取消旧动作并重新探测。离线期间手动动作返回错误。恢复后重新回中，不重放断线前动作。
UNO 的 5 秒保护不依赖 M55 工作。`OK` 仅表示目标角度已接收，不代表物理到位。

## 5. 交互行为

自动模式默认打开，动作只在事件发生时触发，不持续空闲摆动。

| 事件 | 动作 |
|---|---|
| 进入聆听（含按键、唤醒词进入聆听） | 90/100 |
| 进入说话 | 小幅点头：90/100 → 90/80 → 90/100 → 90/90 |
| 空闲或连接状态变化 | 回中 |
| happy / laughing / excited | 小幅点头 |
| sad / sleepy | 90/80 |
| curious / thinking / confused | 100/95 |
| neutral | 回中 |

动作序列每步至少间隔 700ms，并等待之前的请求完成。UNO 执行自身的缓动。
Pan/Tilt 的正反方向取决于安装，不把数值增加直接命名为向左/向上。
手动命令优先，自动表情暂让 10 秒；`robot auto off` 可持续关闭自动动作。
关闭自动动作会取消剩余序列，保留已发出的目标；显式动作和心跳仍可使用。

AI 工具：

- `self.robot.set_pose`：参数 `pan`、`tilt`。
- `self.robot.action`：参数 `action`，支持 `center` / `nod` / `shake`。
- `self.robot.set_auto`：参数 `enabled` 布尔值。
- `self.robot.get_status`：查看在线、目标、UNO 已确认目标和错误计数。

工具返回 queued 只代表提交成功。模型是否选择调用工具仍取决于小智服务端；
无须联网也可以通过下列 MSH 命令验证串口和动作。

## 6. 固件与首次部署

| 固件 | 当前产物 |
|---|---|
| M55 | 使用覆盖后的 M55 工程重新编译生成 |
| M33 非安全应用 | 使用 `m33-overlay/` 覆盖后的 M33 工程重新编译生成 |
| UNO | `arduino/EdgiTalkUnoServo/EdgiTalkUnoServo.ino` |

M33 烧录镜像的地址处理必须以所使用 SDK、IDE 与板卡烧录工具的要求为准；仅更新非安全应用时应保留已经正常工作的安全启动固件。

首次部署顺序：UNO 通信程序 → 更新一次 M33 非安全应用 → M55 应用 → 整板重启。
使用 Intel HEX 内的绝对地址，不要再叠加偏移。保留安全固件及 Wi-Fi/文件系统分区，
不要整片擦除。

仅做循环扫动的舵机示例程序，
没有 UART 解析功能，不能用于这次联调。

## 7. 实机验收

先检查中心位和机械余量，初次测试让运动路径保持空旷。在 M55 的 USB 调试串口
（uart2，原有 115200 设置）输入：

```text
robot status
robot auto off
robot center
robot pose 95 95
robot status
robot nod
robot shake
robot auto on
```

预期日志：`UNO online: PONG and CENTER acknowledged`。
`robot status` 应为 `online=1`，每次动作后 UNO_ack 应匹配目标；ACK 持续增长。
timeouts/errors 不应持续增加。自动模式下测试唤醒/按键聆听、AI 回复和情绪表情。

断开通信信号但保留公共地，检查 UNO 约 5 秒后开始回中；重新接通信号后应重新握手回中。
单独重启 UNO、单独重启 M55、整板断电启动都应重新建立连接。
实机还需检查软件串口与 Servo 定时中断同时运行时的收发稳定性、舵机抖动和供电压降；
主机模拟测试不能验证这些电气/实时条件。

若 online 一直为 0：先确认已更新 M33，UNO 运行通信程序，然后检查 38400、
TX/RX 交叉、HW-221 OE 和两侧电源、公共地。若错误计数随动作增加，检查舵机供电和接线，
不要仅通过增大超时掩盖问题。

## 8. 编译与测试记录

- M55 完整编译及最终增量编译成功，HEX 已导出。
  MCP 发现修复版 text/data/bss = 6,625,076 / 20,968 / 4,451,472 字节。
- M33 非安全应用编译成功：text/data/bss = 75,484 / 784 / 261,753 字节。
  ELF 中确认没有 uart5 ISR 和 FinSH 命令行入口，仍保留启动函数。
- UNO 使用本机 Arduino IDE 随附 CLI 的完整预处理/构建流程，Arduino AVR core 1.8.7、
  avr-gcc 7.3.0、Servo 和 SoftwareSerial，编译成功：程序 5,868 字节，静态 RAM 274 字节。
- 15 项主机互操作测试通过：实际 UNO 源码参与测试，串口/时钟/Servo 使用模拟替身。
  覆盖漏收 READY、协议/限位/缓动、异常报文、丢 ACK、UNO 超时回中、UNO 重启、
  半包恢复、32 位时间回绕、错误角度 ACK、过期 ACK，以及新增的 NUL/CR 拒绝、
  UNO 半包超时、SoftwareSerial 溢出恢复、M55 损坏换行拒绝、非法流量不刷新看门狗。
- 5 项 MCP/会话测试通过：执行错误的 isError 标志、原有返回值兼容性、严格整数/边界校验、工具分页、完整会话 ID。
- 1 项实际 WebSocket 发送函数回归测试通过，覆盖 1420 字节边界及 8KB 消息的 FIN/掩码/长度。
- HEX 校验和、镜像分区边界、M33 地址重定位数据一致性通过检查。
- 未烧录，未验证物理双向串口、实物运动、语音联网端到端行为。

复现测试（本工程根目录，PowerShell）：

```powershell
./tests/robot/run.ps1
./tests/robot/build_uno.ps1
python tests/robot/verify_images.py
```

使用 RT-Thread Studio 为对应 M55 工程生成配置并构建；工具链路径因安装位置而异。

M33 也应通过 RT-Thread Studio 的对应工程配置构建。若涉及镜像重定位或安全启动分区，请严格按所使用 SDK 和烧录工具文档操作；不要对未知的合并镜像套用地址偏移。

发布仓库只保留源码、配置和测试；构建日志及 `.hex`/`.elf` 文件均不提交。第三方代码可能产生与本项目无关的构建警告，验收以无编译错误、测试和实机功能为准。
