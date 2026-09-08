# Smart Agriculture Server

Windows 宿主机上的 Spring Boot + Moquette 服务端，提供：

- MQTT `0.0.0.0:1883`
- Web 控制台 `http://192.168.0.100:8080/`
- REST 与 SSE 接口（与 `MQTT_SERVER_API.md` 一致）
- 每设备最近 100 条内存遥测记录

## 构建与启动

需要 JDK 11+ 和 Maven：

```powershell
mvn clean package
.\start-server.ps1
```

如果 Java 没有加入 PATH，可显式指定：

```powershell
.\start-server.ps1 -Java 'C:\path\to\java.exe'
```

直连模式下，板端 `/etc/smart-agriculture/config.ini` 中 MQTT 主机应为
`192.168.0.100`，并在 Windows 防火墙中仅向 `192.168.0.0/24` 放行 TCP 1883。

如果不希望修改防火墙，可先运行 `start-mqtt-tunnel.ps1`，再将板端 MQTT
主机设为 `127.0.0.1`。本次联调使用的就是该模式。
