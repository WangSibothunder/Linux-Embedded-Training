# MQTT 服务端接口文档

> 项目：`E:/DeskTop/elfboard/11_test/Mqtt_server`  
> 服务端：Spring Boot 2.6.13 + Moquette Broker 0.17  
> HTTP 默认端口：`8080`  
> MQTT 默认端口：`1883`

## 1. 服务说明

该服务端同时提供两类接口：

1. **MQTT 接口**：供嵌入式设备连接、上报遥测数据、接收控制命令。
2. **HTTP/SSE 接口**：供网页或外部系统查询设备状态、查询历史数据、下发命令、订阅实时事件。

当前服务端通过 MQTT 标准事件判断设备上下线：

- 设备发送 `CONNECT`：服务端标记设备在线。
- 设备发送 `DISCONNECT`：服务端标记设备离线。
- 设备异常断电、断网、心跳超时：Moquette 触发 `ConnectionLost`，服务端标记设备离线。

设备历史数据策略：

- 每个设备独立保存最近 `100` 条遥测记录。
- 如果有 40 台设备，最多保存约 `40 × 100 = 4000` 条遥测记录。
- 设备离线不会删除最新状态和历史记录。
- 当前版本使用内存保存业务遥测数据，服务重启后业务遥测历史会丢失。

---

## 2. 基础配置

配置文件：

```text
E:/DeskTop/elfboard/11_test/Mqtt_server/src/main/resources/application.properties
```

当前配置：

```properties
server.port=8080
mqtt.enabled=true
mqtt.host=0.0.0.0
mqtt.port=1883
mqtt.allow-anonymous=true
mqtt.data-path=E:/DeskTop/elfboard/11_test/Mqtt_server/mqtt-data
mqtt.telemetry-topic-prefix=devices
mqtt.devices=40
mqtt.max-payload-bytes=65536
mqtt.history-limit=100
mqtt.sse-queue-capacity=256
spring.devtools.restart.enabled=false
```

字段说明：

| 配置项 | 默认值 | 说明 |
|---|---:|---|
| `server.port` | `8080` | Spring Boot HTTP 服务端口 |
| `mqtt.enabled` | `true` | 是否启动内嵌 MQTT Broker |
| `mqtt.host` | `0.0.0.0` | MQTT Broker 监听地址 |
| `mqtt.port` | `1883` | MQTT TCP 监听端口 |
| `mqtt.allow-anonymous` | `true` | 是否允许匿名 MQTT 客户端连接 |
| `mqtt.data-path` | `.../mqtt-data` | Moquette 持久化数据目录 |
| `mqtt.telemetry-topic-prefix` | `devices` | 设备 Topic 前缀 |
| `mqtt.devices` | `40` | 允许记录的设备数量上限 |
| `mqtt.max-payload-bytes` | `65536` | 单条 MQTT payload 最大字节数 |
| `mqtt.history-limit` | `100` | 每台设备最多保存的历史遥测条数 |
| `mqtt.sse-queue-capacity` | `256` | SSE 异步推送队列容量 |

---

## 3. 通用约定

### 3.1 设备 ID 规则

所有设备 ID 必须满足：

```text
[A-Za-z0-9_-]{1,64}
```

合法示例：

```text
device-001
sensor_01
boardA01
```

非法示例：

```text
device/001
设备001
device 001
sensor.01
```

建议嵌入式设备的 MQTT `clientId` 与 Topic 中的 `{deviceId}` 保持一致。

### 3.2 时间字段

接口中的时间戳均使用毫秒时间戳：

```text
Unix epoch milliseconds
```

示例：

```text
1710000000000
```

---

## 4. MQTT 接口

## 4.1 设备连接 Broker

嵌入式设备连接 MQTT Broker：

```text
Host: 服务端 IP
Port: 1883
Client ID: device-001
Keep Alive: 建议 30 秒
Clean Session: 建议 true
Username: 当前可不填
Password: 当前可不填
```

设备成功连接后，服务端会收到 MQTT `CONNECT`，并将设备标记为在线。

服务端不会要求设备额外发布“上线 JSON”。

---

## 4.2 设备正常断开

设备正常关机、重启、主动断网、进入低功耗前，应发送 MQTT 标准 `DISCONNECT` 报文。

嵌入式端伪代码：

```c
mqtt_disconnect();
```

服务端收到 `DISCONNECT` 后，会将设备标记为离线，并通过 SSE 推送 `device-status` 事件。

服务端不要求设备额外发布“离线 JSON”。

---

## 4.3 设备异常断开

以下场景设备通常无法主动发送任何消息：

- 设备断电
- 设备死机
- Wi-Fi 断开
- 网线拔出
- 网络不可达

此时服务端依赖 MQTT Keep Alive 超时机制判断掉线。Moquette 检测到连接丢失后，会触发 `ConnectionLost`，服务端将设备标记为离线。

嵌入式端必须持续执行 MQTT 客户端库的 `loop`、`yield` 或等价网络处理函数，否则可能无法按预期发送心跳或处理断线。

---

## 4.4 设备发布遥测数据

**方向：设备 -> 服务端**

设备发布 Topic：

```text
devices/{deviceId}/telemetry
```

示例：

```text
devices/device-001/telemetry
```

建议发布参数：

```text
QoS: 0，网络不稳定时可使用 1
Retain: false
Payload: JSON
```

### Payload 字段

| 字段 | 类型 | 必填 | 说明 |
|---|---|---:|---|
| `temperature` | number | 是 | 温度，单位 °C，必须是有限数字 |
| `humidity` | number | 是 | 湿度，范围 `0-100` |
| `light` | number | 是 | 光照，单位 lux，必须 `>= 0` |
| `curtainPosition` | integer | 否 | 遮阳帘开合度，范围 `0-100` |
| `lightOn` | boolean | 是 | 灯光是否开启 |
| `fanOn` | boolean | 是 | 风扇是否启动 |
| `timestamp` | number | 否 | 设备上报时间，毫秒时间戳 |

### 示例 Payload

```json
{
  "temperature": 25.6,
  "humidity": 61.2,
  "light": 320,
  "curtainPosition": 75,
  "lightOn": true,
  "fanOn": false,
  "timestamp": 1710000000000
}
```

如果设备没有可靠 RTC，可以不发送 `timestamp`：

```json
{
  "temperature": 25.6,
  "humidity": 61.2,
  "light": 320,
  "curtainPosition": 75,
  "lightOn": true,
  "fanOn": false
}
```

### 服务端处理逻辑

服务端收到遥测消息后会：

1. 校验 Topic 是否匹配 `devices/{deviceId}/telemetry`。
2. 校验 `{deviceId}` 是否满足 `[A-Za-z0-9_-]{1,64}`。
3. 校验 payload 大小是否超过 `mqtt.max-payload-bytes`。
4. 解析 JSON。
5. 校验温度、湿度、光照、遮阳帘、灯光、风扇字段。
6. 保存该设备最新状态。
7. 保存该设备最近 100 条历史记录。
8. 将设备标记为在线。
9. 通过 SSE 推送 `telemetry` 和 `device-status` 事件。

---

## 4.5 设备订阅控制命令

**方向：服务端 -> 设备**

嵌入式设备需要订阅：

```text
devices/{deviceId}/commands
```

示例：

```text
devices/device-001/commands
```

服务端通过 HTTP 命令接口发布 MQTT 消息到该 Topic。

建议订阅参数：

```text
QoS: 0 或 1
```

当前服务端发布命令时使用：

```text
QoS: 1
Retain: false
```

---

## 4.6 服务端发布的命令 Payload

### 灯光命令

打开灯光：

```json
{
  "type": "light",
  "value": true
}
```

关闭灯光：

```json
{
  "type": "light",
  "value": false
}
```

### 风扇命令

启动风扇：

```json
{
  "type": "fan",
  "value": true
}
```

停止风扇：

```json
{
  "type": "fan",
  "value": false
}
```

### 遮阳帘命令

设置遮阳帘开合度为 75%：

```json
{
  "type": "curtain",
  "value": 75
}
```

`curtain.value` 范围必须是：

```text
0-100
```

---

## 5. HTTP REST 接口

HTTP 基础地址：

```text
http://{serverIp}:8080
```

例如：

```text
http://127.0.0.1:8080
```

---

## 5.1 获取设备列表

```http
GET /api/devices
```

说明：

- 返回所有连接过或上报过的设备状态。
- 包含在线和离线设备。
- 设备离线后不会从列表中删除。

响应示例：

```json
[
  {
    "deviceId": "device-001",
    "clientId": "device-001",
    "online": true,
    "lastSeen": 1710000000000,
    "lastReportAt": 1710000000000,
    "latest": {
      "temperature": 25.6,
      "humidity": 61.2,
      "light": 320,
      "curtainPosition": 75,
      "lightOn": true,
      "fanOn": false,
      "timestamp": 1710000000000
    }
  },
  {
    "deviceId": "device-002",
    "clientId": "device-002",
    "online": false,
    "lastSeen": 1710000100000,
    "lastReportAt": 1710000005000
  }
]
```

---

## 5.2 获取设备状态列表

```http
GET /api/devices/status
```

说明：

- 当前与 `GET /api/devices` 返回一致。
- 用于语义化获取设备状态列表。

响应同 `GET /api/devices`。

---

## 5.3 获取单个设备状态

```http
GET /api/devices/{deviceId}
```

示例：

```http
GET /api/devices/device-001
```

响应示例：

```json
{
  "deviceId": "device-001",
  "clientId": "device-001",
  "online": true,
  "lastSeen": 1710000000000,
  "lastReportAt": 1710000000000,
  "latest": {
    "temperature": 25.6,
    "humidity": 61.2,
    "light": 320,
    "curtainPosition": 75,
    "lightOn": true,
    "fanOn": false,
    "timestamp": 1710000000000
  }
}
```

如果设备不存在，返回 `404`。

---

## 5.4 获取设备最新遥测

```http
GET /api/devices/{deviceId}/telemetry/latest
```

示例：

```http
GET /api/devices/device-001/telemetry/latest
```

响应示例：

```json
{
  "temperature": 25.6,
  "humidity": 61.2,
  "light": 320,
  "curtainPosition": 75,
  "lightOn": true,
  "fanOn": false,
  "timestamp": 1710000000000
}
```

说明：

- 只返回最新一条遥测数据。
- 如果设备存在但还没有上报遥测，当前返回 `404`。
- 如果设备不存在，返回 `404`。

---

## 5.5 获取设备历史遥测

```http
GET /api/devices/{deviceId}/telemetry?limit={limit}
```

示例：

```http
GET /api/devices/device-001/telemetry?limit=100
```

查询参数：

| 参数 | 必填 | 默认值 | 说明 |
|---|---:|---:|---|
| `limit` | 否 | `100` | 返回最近多少条记录，最大 `100` |

响应示例：

```json
[
  {
    "temperature": 25.1,
    "humidity": 60.8,
    "light": 300,
    "curtainPosition": 70,
    "lightOn": true,
    "fanOn": false,
    "timestamp": 1710000000000
  },
  {
    "temperature": 25.6,
    "humidity": 61.2,
    "light": 320,
    "curtainPosition": 75,
    "lightOn": true,
    "fanOn": false,
    "timestamp": 1710000010000
  }
]
```

说明：

- 每台设备最多保存最新 100 条历史记录。
- `limit` 大于 100 时，服务端仍最多返回 100 条。
- 设备离线后，历史记录仍可查询。
- 如果设备不存在，返回 `404`。

---

## 5.6 向设备下发命令

```http
POST /api/devices/{deviceId}/commands
Content-Type: application/json
```

示例：

```http
POST /api/devices/device-001/commands
Content-Type: application/json
```

说明：

- HTTP 客户端调用该接口后，服务端会向 MQTT Topic `devices/{deviceId}/commands` 发布命令。
- 当前只有设备已经连接过或上报过，服务端存在该设备状态时，才允许下发命令。
- 如果设备不存在，返回 `404`。
- 如果命令格式错误，返回 `400`。

### 打开灯光

请求体：

```json
{
  "type": "light",
  "value": true
}
```

响应：

```json
{
  "status": "published"
}
```

### 关闭灯光

```json
{
  "type": "light",
  "value": false
}
```

### 启动风扇

```json
{
  "type": "fan",
  "value": true
}
```

### 停止风扇

```json
{
  "type": "fan",
  "value": false
}
```

### 设置遮阳帘开合度

```json
{
  "type": "curtain",
  "value": 75
}
```

命令校验规则：

| type | value 类型 | value 范围 |
|---|---|---|
| `light` | boolean | `true` 或 `false` |
| `fan` | boolean | `true` 或 `false` |
| `curtain` | number | `0-100` |

---

## 6. SSE 实时事件接口

SSE 用于网页实时接收设备遥测和设备上下线状态。

---

## 6.1 订阅全部设备事件

```http
GET /api/telemetry/stream
Accept: text/event-stream
```

说明：

- 返回 `text/event-stream`。
- 推送全部设备的遥测事件和设备状态事件。
- 网页首页使用该接口实时刷新设备列表和详情。

---

## 6.2 订阅单个设备事件

```http
GET /api/devices/{deviceId}/telemetry/stream
Accept: text/event-stream
```

示例：

```http
GET /api/devices/device-001/telemetry/stream
```

说明：

- 只推送指定设备的事件。
- 如果设备不存在，返回 `404`。

---

## 6.3 SSE 事件：telemetry

事件名：

```text
telemetry
```

事件 ID：

```text
{deviceId}
```

事件数据示例：

```json
{
  "temperature": 25.6,
  "humidity": 61.2,
  "light": 320,
  "curtainPosition": 75,
  "lightOn": true,
  "fanOn": false,
  "timestamp": 1710000000000
}
```

SSE 原始格式示例：

```text
id: device-001
event: telemetry
data: {"temperature":25.6,"humidity":61.2,"light":320,"curtainPosition":75,"lightOn":true,"fanOn":false,"timestamp":1710000000000}
```

触发时机：

- 设备向 `devices/{deviceId}/telemetry` 发布合法遥测后触发。

---

## 6.4 SSE 事件：device-status

事件名：

```text
device-status
```

事件 ID：

```text
{deviceId}
```

事件数据示例：

```json
{
  "deviceId": "device-001",
  "clientId": "device-001",
  "online": true,
  "lastSeen": 1710000000000,
  "lastReportAt": 1710000000000,
  "latest": {
    "temperature": 25.6,
    "humidity": 61.2,
    "light": 320,
    "curtainPosition": 75,
    "lightOn": true,
    "fanOn": false,
    "timestamp": 1710000000000
  }
}
```

触发时机：

- MQTT 客户端连接成功：`online=true`
- MQTT 客户端正常断开：`online=false`
- MQTT 客户端连接丢失：`online=false`
- MQTT 客户端上报遥测并绑定设备 ID：通常 `online=true`

---

## 7. 错误响应

统一错误响应格式：

```json
{
  "error": "错误信息"
}
```

常见状态码：

| HTTP 状态码 | 场景 |
|---:|---|
| `400` | 请求参数错误、命令格式错误、设备 ID 格式错误 |
| `404` | 设备不存在，或设备尚无最新遥测 |
| `500` | 服务端内部异常 |

示例：设备不存在：

```json
{
  "error": "Device not found: device-999"
}
```

示例：命令格式错误：

```json
{
  "error": "Invalid request"
}
```

---

## 8. 嵌入式端接入完整流程

嵌入式端启动后：

```text
1. 初始化网络
2. 连接 MQTT Broker：host={serverIp}, port=1883, clientId=device-001, keepAlive=30
3. 订阅命令 Topic：devices/device-001/commands
4. 按设备自己的周期读取传感器
5. 发布遥测到 Topic：devices/device-001/telemetry
6. 持续执行 MQTT loop/yield，维持 Keep Alive
7. 断线后自动重连，重连继续使用同一个 clientId
8. 正常关机或重启前发送 MQTT DISCONNECT
```

嵌入式端最小伪代码：

```c
mqtt_connect("192.168.1.100", 1883, "device-001", 30);
mqtt_subscribe("devices/device-001/commands", 0);

while (running) {
    mqtt_loop();

    if (time_to_report()) {
        mqtt_publish(
            "devices/device-001/telemetry",
            "{\"temperature\":25.6,\"humidity\":61.2,\"light\":320,\"curtainPosition\":75,\"lightOn\":true,\"fanOn\":false}",
            0,
            false
        );
    }
}

mqtt_disconnect();
```

收到命令处理示例：

```c
void on_mqtt_message(const char *topic, const char *payload) {
    if (strcmp(topic, "devices/device-001/commands") != 0) {
        return;
    }

    // 解析 JSON：{"type":"light","value":true}
    // type=light   -> value 必须是 bool，控制灯光
    // type=fan     -> value 必须是 bool，控制风扇
    // type=curtain -> value 必须是 0-100 数字，控制遮阳帘开合度
}
```

---

## 9. curl 示例

### 获取设备列表

```bash
curl http://127.0.0.1:8080/api/devices
```

### 获取单设备状态

```bash
curl http://127.0.0.1:8080/api/devices/device-001
```

### 获取最新遥测

```bash
curl http://127.0.0.1:8080/api/devices/device-001/telemetry/latest
```

### 获取最近 100 条历史遥测

```bash
curl "http://127.0.0.1:8080/api/devices/device-001/telemetry?limit=100"
```

### 打开灯光

```bash
curl -X POST http://127.0.0.1:8080/api/devices/device-001/commands \
  -H "Content-Type: application/json" \
  -d '{"type":"light","value":true}'
```

### 启动风扇

```bash
curl -X POST http://127.0.0.1:8080/api/devices/device-001/commands \
  -H "Content-Type: application/json" \
  -d '{"type":"fan","value":true}'
```

### 设置遮阳帘 75%

```bash
curl -X POST http://127.0.0.1:8080/api/devices/device-001/commands \
  -H "Content-Type: application/json" \
  -d '{"type":"curtain","value":75}'
```

### 订阅 SSE 实时事件

```bash
curl -N http://127.0.0.1:8080/api/telemetry/stream
```

---

## 10. MQTT Topic 总表

| Topic | 方向 | 发布方 | 订阅方 | 说明 |
|---|---|---|---|---|
| `devices/{deviceId}/telemetry` | 设备 -> 服务端 | 嵌入式设备 | 服务端 Broker interceptor | 设备遥测上报 |
| `devices/{deviceId}/commands` | 服务端 -> 设备 | Spring Boot 服务端 | 嵌入式设备 | 控制命令下发 |

---

## 11. HTTP/SSE 接口总表

| 方法 | 路径 | 说明 |
|---|---|---|
| `GET` | `/` | 设备监控网页 |
| `GET` | `/api/devices` | 获取设备状态列表 |
| `GET` | `/api/devices/status` | 获取设备状态列表，语义化别名 |
| `GET` | `/api/devices/{deviceId}` | 获取单个设备状态 |
| `GET` | `/api/devices/{deviceId}/telemetry/latest` | 获取设备最新遥测 |
| `GET` | `/api/devices/{deviceId}/telemetry?limit=100` | 获取设备历史遥测 |
| `POST` | `/api/devices/{deviceId}/commands` | 向设备发布 MQTT 控制命令 |
| `GET` | `/api/telemetry/stream` | SSE 订阅全部设备实时事件 |
| `GET` | `/api/devices/{deviceId}/telemetry/stream` | SSE 订阅单设备实时事件 |

---

## 12. 注意事项

1. 当前 MQTT 匿名连接为开启状态：`mqtt.allow-anonymous=true`。生产环境建议增加账号密码、ACL 和 TLS。
2. 设备 `clientId` 必须固定唯一，否则服务端会认为是不同设备。
3. 设备正常下线应发送 MQTT `DISCONNECT`。
4. 设备异常下线依赖 MQTT Keep Alive 超时，不需要额外发送离线消息。
5. 设备必须持续执行 MQTT 网络循环函数，例如 `loop` 或 `yield`。
6. 遥测 Topic 必须严格匹配 `devices/{deviceId}/telemetry`。
7. 命令 Topic 必须严格匹配 `devices/{deviceId}/commands`。
8. JSON 布尔值必须使用 `true/false`，不能使用字符串 `"true"/"false"`。
9. JSON 数值字段不能携带单位字符串，例如不能写 `"25.6°C"`。
10. 服务端业务遥测历史当前保存在内存中，重启后会丢失。
