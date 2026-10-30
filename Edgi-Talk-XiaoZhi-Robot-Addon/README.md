# Edgi-Talk XiaoZhi 机器人增量工程

这是一个面向 **Edgi-Talk M55 + Arduino UNO** 的轻量级源码包，用于实现小智 AI 驱动的 StackChan 风格机器人动作。M55 运行 RT-Thread、小智 AI、显示与机器人状态机；UNO 仅负责解析串口动作命令并输出两个舵机 PWM。

本仓库是“增量工程”，**不是完整 SDK 镜像**：请先获取官方 Edgi-Talk SDK，再将本仓库中的覆盖文件同步到对应的 M55/M33 工程。这样不会把数百 MB 的 SDK、工具链、构建缓存和固件二进制提交到 GitHub。

## 硬件连接

| Edgi-Talk CN5 | HW-221 / TXS0108E | Arduino UNO | 作用 |
| --- | --- | --- | --- |
| 3.3 V | VCCA (UA) | — | 电平转换器 A 侧供电 |
| 5 V | VCCB (UB) | 5 V | 电平转换器 B 侧供电 |
| 物理引脚 8，TXD / P17.1 / UART5_TX | A1 ↔ B1 | D2 (RX) | M55 向 UNO 发送命令 |
| 物理引脚 10，RXD / P17.0 / UART5_RX | A2 ↔ B2 | D3 (TX) | UNO 向 M55 回传状态 |
| GND | GND | GND | 必须共地 |
| — | — | D9 | Pan 舵机信号 |
| — | — | D10 | Tilt 舵机信号 |

UNO 舵机应由独立、足够电流的 5–6 V 电源供电，**电源地必须与 UNO、Edgi-Talk 共地**；不要用 Edgi-Talk 的 5 V 引脚直接承担 SG996 舵机电流。

## 快速使用

1. 克隆或下载 [官方 Edgi-Talk SDK](https://github.com/RT-Thread-Studio/sdk-bsp-psoc_e84-edgi-talk)，其中需包含 `Edgi_Talk_M55_XiaoZhi` 与 `Edgi_Talk_M33_IPC` 工程。
2. 在本仓库根目录执行下面的覆盖脚本（路径换成自己的 SDK 工作区）。

   ```powershell
   .\scripts\apply-overlays.ps1 `
     -M55Project 'C:\path\to\Edgi_Talk_M55_XiaoZhi' `
     -M33Project 'C:\path\to\Edgi_Talk_M33_IPC'
   ```

3. 依照 `patches/m55-websocket-fin.patch` 对 M55 WebSocket 文件应用补丁；在 Git SDK 工作区中可执行：

   ```powershell
   Set-Location <Edgi_Talk_M55_XiaoZhi目录>
   git apply <本仓库目录>\patches\m55-websocket-fin.patch
   ```

4. 用 Arduino IDE 打开 `arduino/EdgiTalkUnoServo/EdgiTalkUnoServo.ino`，选择 Arduino UNO 并烧录。
5. M33 中禁用了 UART5 控制台以释放该串口；先烧录 M33，再编译、烧录 M55。
6. 按 `docs/ROBOT_AI_QUICKSTART.md` 为小智 AI 配置并发现 `self.robot` 工具，然后对设备说“请点点头”“向左看一下”等动作意图。

## 仓库结构

```text
arduino/       UNO 舵机执行器
m55-overlay/  M55 覆盖文件：机器人线程、MCP 工具、UART5 与配置
m33-overlay/  M33 覆盖文件：释放 UART5
patches/       无需整文件复制的上游微小补丁
docs/          接线、部署、比赛提交材料和论坛发布稿
tests/robot/   UNO 协议、MCP 与 WebSocket 回归测试
scripts/       覆盖安装脚本
```

## 不应提交的内容

不要提交完整的 `libraries/`、`rt-thread/`、`tools/`、`build/`、`Debug/`、`.settings/`、`.project`、`.cproject`，以及 `.hex`、`.elf`、`.map` 等构建产物。不要提交 Wi-Fi 密码、设备令牌、私钥或个人账号配置。

详细的功能、部署、测试和比赛文档见 [`docs`](docs/README.md)。发布前请为你的原创增量代码选择许可证；官方 SDK 与第三方组件仍遵循其各自许可证，见 [`NOTICE.md`](NOTICE.md)。
