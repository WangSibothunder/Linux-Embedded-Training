# 智慧农业综合项目

本项目以 ELF1 / i.MX6ULL 开发板为终端、PC 为服务端，实现环境采集、设备控制、通信实验和网页监控。

## 目录

- `Terminal/`：Qt 5.6.2 ARM 终端源码。
- `Server/`：Spring Boot 2.6.13 + Moquette 0.17 服务端源码。
- `PWMFanDriver/`：PWM7 风扇内核模块、设备树及独立测试界面源码。
- `docs/MQTT_SERVER_API.md`：MQTT、REST 和 SSE 协议说明。

## 已实现功能

- AHT20 温湿度、BH1726 光照采集。
- LED1 开关、PWM 风扇 0～4 档控制。
- MQTT 遥测上报、命令订阅、心跳和断线重连。
- 宿主机 REST API、每设备最近 100 条历史和 SSE 实时推送。
- 浏览器监控及 LED、风扇远程控制。
- `/dev/ttymxc1` 与 `/dev/ttymxc2` 双 RS-485 收发。
- `can0` 与 `can1` 双 SocketCAN 收发。

窗帘协议保留，但因当前没有电机和位置反馈硬件，终端会明确拒绝执行。

## 系统数据流

```text
传感器/执行器 ⇄ Qt Terminal ⇄ MQTT Broker ⇄ Spring Boot ⇄ HTTP/SSE 网页
```

默认设备 ID 为 `elf1-001`，默认上报周期为 2 秒。

## 快速入口

- Qt 交叉编译与板端运行：[`Terminal/README.md`](Terminal/README.md)
- 服务端构建与启动：[`Server/README.md`](Server/README.md)
- 风扇驱动编译与部署：[`PWMFanDriver/README.md`](PWMFanDriver/README.md)
- 完整接口：[`docs/MQTT_SERVER_API.md`](docs/MQTT_SERVER_API.md)

仓库和 ZIP 均只包含源码及说明，不包含编译产物、日志、密钥或账号凭据。
