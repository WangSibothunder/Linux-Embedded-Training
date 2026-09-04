# UART2UART 验证记录

这是 **1.0 历史记录**。2.0 的构建、真机双口测试和部署结果请看 `DUAL_PORT_TEST_REPORT.md`；下方“尚未验证”描述的是 1.0 当时的状态。

验证日期：2026-09-03。

## 构建与产物

- Ubuntu 虚拟机桌面：Qt 5.6.2，x86_64，已通过 `scripts/build-linux.sh` 实际编译和链接。
- 开发板交叉编译：Qt 5.6.2，现有 Freescale/NXP `fsl-imx-x11/4.1.15-2.0.0` SDK，Cortex-A7 / ARMv7 / hard-float，已通过 `scripts/build-imx6ull.sh` 实际编译和链接。
- ARM ELF 动态加载器：`/lib/ld-linux-armhf.so.3`；需要匹配 SDK 的 Qt 和系统动态库。
- 现有 SDK 有 `oe-device-extra.pri` 缺失提示，但生成的 Makefile 使用了 ARM 交叉编译器，最终编译/链接成功，并已检查 ELF 架构；未修改 SDK。
- 构建产物复制回 Windows 后核对 SHA-256 与虚拟机输出一致。

| 产物 | SHA-256 |
| --- | --- |
| `bin/linux-x86_64/uart2uart` | `17e0ae74bed565c03d9a5df1bdec2be5cac44daa5e8d2fee5d3ec778494cc451` |
| `bin/imx6ull-armhf/uart2uart` | `ad1087a77876ca4852d5b39766e70d60aaa76aaf979951f1e6356e2abdd10bce` |

## 自动测试

执行的是随工程交付的 `scripts/run-tests.sh`，使用 QtTest 5.6.2 和 Linux PTY 虚拟串口。
结果：**11 passed / 0 failed / 0 skipped**，其中 9 项业务测试，另有初始化和清理各 1 项。

| 用例 | 已验证内容 |
| --- | --- |
| `hexRoundTrip` | 全部 00～FF 字节的 HEX 往返、大小写及空白处理 |
| `invalidHex` | 奇数位、0x、非法字符、逗号、中文输入拒绝；空输入解析 |
| `lineEndings` | 不追加、LF、CR、CRLF 的实际字节 |
| `splitUtf8` | 中文和 emoji 在每个可能的字节位置分段后恢复；解码重置；NUL |
| `unopenedAndInvalidPort` | 未打开时不能发送；无效端口打开失败且不变为已连接 |
| `bidirectionalBinary` | 真实 QSerialPort 与 PTY 双向同时读写全部 256 种字节；关闭后重新打开 |
| `queueLimitsAndLargeWrite` | 空/超大数据拒绝；单次 64 KiB；4 次共 256 KiB 排队；溢出拒绝；完整输出和 TX 计数 |
| `disconnectClosesSession` | PTY 对端断开后关闭会话、报告错误并清空待发状态 |
| `guiSendReceiveAndControls` | GUI 打开/关闭、中文双向传输、不自动回显、非法 HEX 不发送、HEX+CRLF、定时控制状态、接收切换/清空、800×480 布局 |

原始结果见 `test-log.txt`，交叉构建日志见 `arm-build-log.txt`。
`界面预览.png` 为 Linux 离屏平台上的真实 GUI 渲染；使用系统 DejaVu Sans 和 Droid Sans Fallback 字体完成视觉检查，字体未打包到工程中。

## 尚未验证的部分

- 未上传、启动或烧录到 `192.168.0.232` 的真实开发板。
- 未连接真实 TTL UART / USB 转串口，未测电压、引脚、线序及实际波特率误差。
- 未验证板上 Qt 动态库、平台插件、中文字体、触摸屏和显示环境与产物是否匹配。
- PTY 不模拟 UART 电气时序、奇偶校验错误、实际 RTS/CTS 硬件流控或 USB 热插拔的全部行为。
- 10 秒无进展看门狗已实现，但本次未单独做真实流控阻塞/10 秒超时验收。
- 原始接收保存、1 MiB 缓存淘汰为代码实现，本次未做实际设备长时间连续接收或保存文件对话框自动化测试。
- Windows Kit 构建和运行未测试；没有交付 Windows `.exe`。

## 真机验收建议

1. 根据板卡原理图确认空闲 UART、引脚、电压和节点，不要使用正在输出系统日志的控制台口。
2. 按 README 配置兼容的 TTL 接线和一致的串口参数。
3. 先各方向发送一条已知文本，再检查 `00 01 7F 80 FF` 的 HEX 接收结果；只向可安全接收测试数据的设备发送。
4. 再测试中文、换行、低频定时发送、关闭/重新打开、保存原始数据。
5. 最后根据实际带宽逐步增加数据量，验证对端帧解析/应答；TX 计数本身不能代替对端确认。

本工程未修改参考例程，也未更改串口驱动、设备树、网络、系统权限或启动服务。
