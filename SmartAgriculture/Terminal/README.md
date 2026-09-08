# SmartAgricultureTerminal

ELF1 / i.MX6ULL 的 Qt 5.6 智能农业终端。当前工程集成：

- AHT20 温湿度采集（`/dev/aht20`）
- BH1726 光照采集（按 input 名称 `lightsensor` 自动定位）
- LED1 本地与 MQTT 控制
- PWM7 风扇 0～4 档、本地与 MQTT 控制
- MQTT 3.1.1 上报、命令订阅、Keep Alive 和指数退避重连
- 双路 UART / RS-485 文本收发
- CAN 双通道页面（使用已部署的 `/usr/local/sbin/ip` 配置位速率）

窗帘硬件未配置，收到 `curtain` 命令时会明确拒绝执行。

## 交叉编译

```bash
source /opt/fsl-imx-x11/4.1.15-2.0.0/environment-setup-cortexa7hf-neon-poky-linux-gnueabi
mkdir -p build
cd build
qmake ../SmartAgricultureTerminal.pro
make -j4
```

## 板端运行

```bash
smartagriculture --config /etc/smart-agriculture/config.ini
```

默认 MQTT 服务端为宿主机直连网卡地址 `192.168.0.100:1883`，设备 ID 为 `elf1-001`。

当前 Windows 公用网络防火墙未放行 1883，因此联调时也可以在宿主机运行
`SmartAgricultureServer/start-mqtt-tunnel.ps1`，并把板端 MQTT 地址设为 `127.0.0.1`。
该隧道仍然把数据送到宿主机服务端，不需要板端访问互联网。
