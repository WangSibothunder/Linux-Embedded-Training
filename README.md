# Linux 嵌入式实训

基于 ELF1 / i.MX6ULL 开发板的 Linux 嵌入式实践代码。

## UART2UART 串口通信程序

当前仓库只包含一个 Qt 程序 `UART2UART`，提供两个功能页面：

- **单串口工具**：串口配置、文本/HEX 发送接收、定时发送和接收数据保存。
- **双口对测**：同时打开两路 RS-485，支持手动双向发送和真实对侧接收验证。

工程入口：[UART2UART/UART2UART.pro](UART2UART/UART2UART.pro)。
详细说明：[UART2UART/README.md](UART2UART/README.md)。
此前实测记录：[双口测试报告](UART2UART/docs/DUAL_PORT_TEST_REPORT.md)。

## 编译

需要 Linux、Qt 5（已在 Qt 5.6.2 验证）、Qt SerialPort、qmake 和 C++11 编译器；不是 Qt 6 工程。

```bash
cd UART2UART
# Ubuntu 桌面版；可通过 QMAKE 指定 Qt 5 的 qmake 路径
bash scripts/build-linux.sh
# ELF1 开发板 ARM 版；需要安装匹配的交叉编译 SDK
bash scripts/build-imx6ull.sh
```

ARM 输出：`UART2UART/build/imx6ull-armhf/uart2uart`。
Qt Creator 需先加载 SDK 环境，参见 `scripts/open-qtcreator-imx6ull.sh`。

## 开发板部署

以下 IP 为此次实训开发板地址，请按实际环境调整：

```bash
scp UART2UART/build/imx6ull-armhf/uart2uart root@192.168.0.232:/usr/bin/
```

在已启用 X11 转发的 SSH 会话中运行：

```bash
QT_QPA_PLATFORM=xcb /usr/bin/uart2uart
```

双口测试接线为 A1→A2、B1→B2，对应 `/dev/ttymxc1` 和 `/dev/ttymxc2`。不要短接同一接口的 A/B，不要占用 `/dev/ttymxc0` 系统控制台。

## 仓库内容说明

本仓库保留源码、构建脚本、测试代码、说明和历史测试记录。不包含预编译二进制、Qt Creator 个人 Kit 配置、缓存或账号凭据；子目录说明中提到的 `bin/`、本机路径和备份文件属于此前本地部署记录，并不随本仓库提供。

本次仅整理上传，未修改程序逻辑，也未重新执行开发板收发实验。原始本地工程保持不变。
