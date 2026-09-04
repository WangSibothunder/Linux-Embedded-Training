# UART2UART 双向串口通信

## 2.0 更新：同一开发板的两路 RS-485 互测

默认打开新增的“双口对测”页；原有功能保留在“单串口工具”页，也可以加 `--single` 直接进入。

本次查明的原因：旧界面只打开一个端口；当时选中的 `/dev/ttymxc6` 是 RS-232，不是 A1/B1 或 A2/B2。
依据《ELF1、ELF1S 软件教程》156～157、429 页及硬件教程 PDF 第 44 页：

| ELF1 接口 | Linux 设备 | 用途 |
| --- | --- | --- |
| A1 / B1 | `/dev/ttymxc1` | UART2，RS485_1 |
| A2 / B2 | `/dev/ttymxc2` | UART3，RS485_2 |
| TX / RX | `/dev/ttymxc6` | UART7，RS-232 |
| DEBUG | `/dev/ttymxc0` | 系统控制台，不用于此次互测 |

接法为 **A1→A2、B1→B2**。RS-485 的 A/B 是差分线，不是 TX/RX，不能把同一接口的 A、B 短接作为回环。
ELF1 上的 MAX13487E 硬件自动控制收发方向，本次不需要改驱动、设备树或 GPIO。其他板卡/手动方向收发器不能直接套用这一结论。

操作顺序：

1. 保持两根测试线连接，关闭其他占用这两个端口的程序。
2. 默认两个端口分别为 `ttymxc1`、`ttymxc2`，波特率 115200，固定 8N1、无流控。
3. 点击“打开两个端口”；打开后两侧都持续监听，但不会自动发送。
4. 点“验证 1 → 2”：程序从接口1发送唯一字符串，只有接口2实际收到完整字符串才显示通过。
5. 等待按钮恢复，再点“验证 2 → 1”。两方向应分别通过；日志保留两次结果。
6. 自定义发送时，在左侧输入内容并点“发送 1 → 2”，数据应显示在**右侧接收区**；反向对应左侧接收区。

双口页的文本/HEX 共用格式选择，不自动追加换行；单次限 4096 字节。
发送时会暂时禁止反向发送，留出按波特率估算的半双工切换时间；本页不自动回显、不定时连续发。
它不能协调其他设备的总线访问，测试时不要让其他节点同时发送。
双口页每侧原始缓存最多 64 KiB，文本显示最近 32768 个 UTF-16 字符单元，HEX 显示最近 8 KiB；接收文件保存仍在原单串口页提供。
一侧打开失败会关闭另一侧，一侧断开会关闭双口；活动模式未关闭串口时不能切换到另一页，避免内部抢占端口。

通过 MobaXterm 已开启 X11 转发的 SSH 会话启动：

```bash
QT_QPA_PLATFORM=xcb /usr/bin/uart2uart
```

新增 `tools/rs485check.pro` / `rs485check.cpp`，用于明确指定两端口的命令行硬件验收。
**该工具会真实发送 10 次交替方向测试数据，只可对已确认接线的测试端口执行**，用法：

```bash
/tmp/uart2uart-rs485check-20260903 /dev/ttymxc1 /dev/ttymxc2
```

实际测试与部署记录见 `docs/DUAL_PORT_TEST_REPORT.md`。下方原有功能说明主要描述“单串口工具”页。

参考 `03-例程源码/03-0 Qt例程源码/02_SerialPort` 的 Qt Widgets + QSerialPort 方案重新实现。
原参考工程未修改。本工程使用 **Qt 5.6 及以上的 Qt 5、C++11、qmake**；不是 Qt 6 工程。

## 1. 是否需要编写驱动？

本次需要的是 **Qt 界面程序 + 用户态串口收发模块，不需要另写串口内核驱动**，前提是系统已经提供且启用了该串口的驱动。

- `src/mainwindow.cpp`：连接参数、收发界面、定时发送、日志、接收保存。
- `src/serialsession.cpp`：通过 QSerialPort 打开串口、异步读写、队列和错误处理。它不是 `.ko` 内核驱动。
- 操作系统驱动：由板卡 BSP / Linux / USB 串口适配器驱动提供，应用使用其设备节点。

本机提供的 `linux-4.1.15-elf1/drivers/tty/serial/imx.c` 已有 i.MX UART 驱动实现，相关内核选项是 `CONFIG_SERIAL_IMX`。这只能证明 BSP 含有驱动源码，不能证明当前板上镜像已经启用某一个 UART。
若板上没有对应 `/dev/ttymxcN`，应先检查设备树的 `status`、引脚复用、内核配置及启动日志；不要靠新写一个驱动或手动 `mknod` 绕过这些问题。

本次没有修改内核、设备树、启动控制台或串口权限，也没有重新烧录开发板；2.0 已按用户要求更新板上的应用程序。

## 2. 已实现功能

- 两台机器都可以运行一份程序，各自选择本机端口；可以同时发送和接收，不区分主从。
- 支持枚举/手动输入端口：如 `/dev/ttymxcN`、`/dev/ttyUSB0`、`COM3`。
- 波特率可选或输入，数据位 5/6/7/8、校验 None/Even/Odd/Mark/Space、停止位 1/2、流控 None/RTS-CTS/XON-XOFF。实际支持由设备决定。
- 文本发送使用 UTF-8；接收有跨数据块的 UTF-8 解码，避免一个中文字符分两次到达时乱码。
- HEX 发送和显示；HEX 示例 `01 A2 FF 00`，也支持 `01A2FF00`。允许空白分隔，不接受 `0x`、逗号或奇数个数字。
- 可追加 LF、CR、CRLF；HEX 模式下追加的也是真实换行字节。
- 单次发送、定时发送、收发字节计数、错误日志、原始接收数据 `.bin` 保存。
- 打开失败检查、异步收发、发送排队上限、10 秒发送无进展自动关闭、设备异常断开处理。
- 参数记忆；启动时不会自动打开端口、发送数据或恢复定时发送。接收数据不会自动回发，避免两台机器形成回显循环。
- `--fullscreen` 全屏启动，已在 800×480 离屏渲染下检查布局。

## 3. 文件说明

| 文件 | 用途 |
| --- | --- |
| `UART2UART.pro` | Qt Creator 打开的主工程 |
| `src/` | 完整 C++ 源码，界面使用布局代码构建，无需 `.ui` 文件 |
| `scripts/build-linux.sh` | Ubuntu 虚拟机桌面版本构建 |
| `scripts/build-imx6ull.sh` | 使用板卡 SDK 构建 ARM 版本 |
| `scripts/run-tests.sh`、`tests/` | QtTest 自动测试；Linux 串口测试仅使用 PTY 虚拟端口 |
| `bin/linux-x86_64/uart2uart` | 已编译 Ubuntu x86_64 程序，动态依赖本机 Qt 5.6.2 |
| `bin/imx6ull-armhf/uart2uart` | 已编译 Cortex-A7 / ARMv7 / hard-float 开发板程序 |
| `docs/TEST_REPORT.md` | 实测结果、限制和硬件验收清单 |
| `docs/界面预览.png` | 虚拟串口测试时的真实界面渲染，不是开发板运行照片 |

两个 Linux 程序不能作为 Windows `.exe` 双击运行；ARM 程序也不能直接在 x86_64 Ubuntu 中运行。
随包不包含 Qt 动态库、平台插件或字体，目标系统必须有匹配的运行环境。

## 4. 最快使用方法

### 在现有 Ubuntu 虚拟机运行

把整个 `UART2UART` 文件夹复制到虚拟机，例如 `~/UART2UART`。Windows E 盘路径不是 Linux 路径，不要求 VMware 共享文件夹一定可用。

```bash
cd ~/UART2UART
chmod +x bin/linux-x86_64/uart2uart
./bin/linux-x86_64/uart2uart
```

这份桌面程序使用 `/opt/Qt5.6.2/5.6/gcc_64/lib` 的 Qt 库。若要在其他环境使用，请用该环境的 Qt 5 重新编译并部署依赖。
如果使用 USB 转串口，需要在 VMware 中将该 USB 设备连接给虚拟机，再在虚拟机中选择实际出现的 `/dev/ttyUSB*` 或 `/dev/ttyACM*`；不要让宿主机同时占用同一设备。

### 两台机器接线与界面操作

1. 先确认两端接口类型和逻辑电平。对兼容电平的 TTL UART：A-TX 接 B-RX，A-RX 接 B-TX，GND 接 GND；不要互接两个输出 TX，也不要随意连接 VCC。
2. TTL、RS-232、RS-485 不是同一种电气接口，不能直接混接。需要相应收发器/转换器。ELF1 的自动选向 RS-485 接线见前面的 2.0 说明；其他需要手动方向控制的收发器未适配。
3. 两端都运行程序（对端也可以是其他串口助手或设备程序），选择各自本机的端口。开发板 IP 地址不是串口名称。
4. 两端统一设为 `115200 / 8 数据位 / 无校验 / 1 停止位 / 无流控` 作为初始参数。
5. 点“打开串口”，确认右上角“已连接”，否则查看“运行记录”。先不要勾选定时发送。
6. A 发送 `Hello B`，B 应收到；再 B 发送 `Hello A`，A 应收到。HEX 模式可用 `00 01 7F 80 FF` 验证非文本字节。
7. 需要连续发送时再勾选“定时发送”：勾选后立即发送一次，之后按设定间隔重复；发送内容在定时期间锁定。

本程序传输的是原始字节流，没有自动加帧头、CRC、Modbus、应答、重试或文件传输协议。UART 一次收到的数据块不一定对应对端一次发送的数据包。
如果需要命令控制设备，后续要根据对端协议增加打包和解析；请勿向实际设备盲发测试字节。

### 开发板部署（后续自行更新时使用；2.0 本次已上传）

本次 ARM 程序由现有 `fsl-imx-x11/4.1.15-2.0.0` SDK 的 Qt 5.6.2 编译。适用于匹配该 ABI/库环境的 ELF1 / i.MX6ULL 镜像，不保证所有 ARM 板通用。
在虚拟机项目根目录，可手动复制到之前使用的板卡地址：

```bash
scp bin/imx6ull-armhf/uart2uart root@192.168.0.232:/tmp/uart2uart
```

然后在开发板本地已有图形会话的终端中执行：

```bash
chmod +x /tmp/uart2uart
/tmp/uart2uart --fullscreen
```

SSH 登录不一定继承板上 `DISPLAY` / Qt 插件环境；优先沿用原有 Qt 例程的启动方式，不要盲设 `DISPLAY=:0`。
若镜像使用 framebuffer 且确认有 Qt `linuxfb` 插件及可用 `/dev/fb0`，可用：

```bash
QT_QPA_PLATFORM=linuxfb /tmp/uart2uart --fullscreen
```

若镜像使用 X11、EGLFS 或其他显示栈，应使用对应环境；不要为了启动本程序直接杀掉原有桌面。
`/tmp` 通常非永久保存。本次只是部署用户程序，不涉及烧录内核/rootfs。

## 5. 重新编译

### Qt Creator

打开 `UART2UART.pro`，选择 Qt 5 Kit，要求 Core、Gui、Widgets、SerialPort 模块及 C++11 编译器。
桌面 Kit 生成桌面版本，交叉 Kit 才生成开发板版本。不要混用两种构建目录。
Windows 编译需匹配所选 Qt 的 MSVC 或 MinGW Kit，本次未编译/测试 Windows 可执行文件。

### Ubuntu 命令行

```bash
cd ~/UART2UART
bash scripts/build-linux.sh
./build/linux-x86_64/uart2uart
```

如果 Qt 安装路径不同：

```bash
QMAKE=/你的Qt5/bin/qmake bash scripts/build-linux.sh
```

### i.MX6ULL 交叉编译

```bash
cd ~/UART2UART
bash scripts/build-imx6ull.sh
```

默认 SDK 环境脚本：
`/opt/fsl-imx-x11/4.1.15-2.0.0/environment-setup-cortexa7hf-neon-poky-linux-gnueabi`。
可通过 `SDK_ENV` 覆盖。不要把桌面 Qt 的 `.so` 拷到 ARM 开发板。
现有 SDK 的 qmake 会提示缺少 `oe-device-extra.pri`，本次实际编译和链接成功；未修改系统 SDK 文件。更换板卡或 SDK 后应重新验证。

## 6. 常见问题与驱动排查

在开发板本地可先只读检查：

```bash
ls -l /dev/ttymxc* /dev/ttyUSB* /dev/ttyACM*
dmesg | grep -Ei 'ttymxc|ttyUSB|ttyACM|uart|serial'
cat /proc/cmdline
```

- 没有端口：先确认板卡 UART 的引脚复用/设备树状态；USB 串口要确认适配器识别及驱动。列表里没有时也能手动输入真实节点，不能输入一个凭空假设的节点。
- 端口被占用：检查串口助手、getty、调试终端。`console=ttymxc...` 表示可能是系统控制台，应优先使用空闲业务 UART。不要在没有备用登录方式时禁用控制台。
- 权限不足：用 `ls -l` 看节点属主/组。Ubuntu 常见串口组是 dialout；由管理员按实际情况授予组权限，重新登录生效，不建议对所有串口执行 `chmod 777`。
- 能打开但无数据：检查 TX/RX 交叉、共地、电平、两端参数，以及是否选错物理引脚/设备节点。程序“已连接”仅表示本机打开串口成功，不表示线缆或对端已连通。
- 有 HEX 但文本乱码：两端文本必须使用相同编码，本程序固定 UTF-8；非文本协议用 HEX 看，不做 GBK 自动猜测。
- `Unknown module(s) in QT: serialport`：当前编译 Kit 没有 QtSerialPort 开发模块。应补齐当前 Kit/SDK 的模块，而不是改用错误架构的 Qt 库。
- `error while loading shared libraries`：开发板缺少匹配架构的 Qt 运行库。需要 Qt5Core/Gui/Widgets/SerialPort 及其依赖；请沿用该镜像对应的 Qt 部署包。
- 平台插件加载失败：确认 X11/linuxfb/eglfs 与镜像匹配，用 `QT_DEBUG_PLUGINS=1` 启动查看原因。
- 中文显示方框：检查板上中文字库及 Qt 字体路径。字体目录应同时提供中文和拉丁字形；按镜像设置 `QT_QPA_FONTDIR`，不要只看程序是否编译通过。

## 7. 边界与可靠性

- 一次发送最多 64 KiB，含追加的换行；本地总待发最多 256 KiB。达到上限拒绝新数据，并停止定时发送。
- 接收保存仅保留最近 1 MiB 原始数据，超出后丢弃最旧数据并显示丢弃计数；不是无限连续录制器。
- 文本显示最近约 64K UTF-16 字符单元，HEX 显示最近 8 KiB；保存范围比显示范围大。
- RX 是应用累计读取字节数；TX 是 QSerialPort 报告写出的字节数，不是对端应答。清空接收仅重置 RX 相关内容，不重置 TX。
- 取消定时只停止后续入队，已排队数据继续发送；关闭端口丢弃剩余本地待发数据。10 秒连续无发送进展会自动关闭，不自动重发，防止重复执行对端命令。
- 无硬件流控时若对端发送快于接收处理能力，硬件/系统层仍可能丢数据；若需要端到端可靠性，应增加序号、校验和应答协议。
- 高频定时发送应按带宽限制配置。例如 115200、8N1 的理论上限约 11520 字节/秒，实际应留余量。

## 8. 自动测试

```bash
cd ~/UART2UART
bash scripts/run-tests.sh
```

Linux 上使用 `openpty()` 创建虚拟端口验证真实 QSerialPort 读写，不向物理 UART 发数据。测试参数使用隔离的临时配置，不覆盖正常应用设置。
可设置 `UART2UART_SCREENSHOT=/tmp/uart-preview.png` 让 GUI 测试保存截图。离屏环境缺字时需要有效的 `QT_QPA_FONTDIR`，包含中文和拉丁字体。
详见 `docs/TEST_REPORT.md`。官方接口资料：[Qt QSerialPort 文档](https://doc.qt.io/qt-6/qserialport.html)；本工程为兼容 Qt 5.6，保留了旧版错误信号连接分支。
