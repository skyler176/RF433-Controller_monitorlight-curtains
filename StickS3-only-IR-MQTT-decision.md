# StickS3 单机红外 + MQTT 接入 NovOS 决策记录

> 状态：已被 `RF433-capture-development-plan.md` 取代。原装旋钮 PCB 显示其高概率
> 使用 433.92 MHz ASK/OOK，而非红外。本文作为历史决策保留，不再指导后续实施。

日期：2026-07-13

## 决策

可以先不使用 CoreS3。对 CX9-B-A 屏幕挂灯，`M5StickS3` 单机已经具备完成下面三件事所需的硬件：

1. 通过内置红外接收器学习原装遥控器信号。
2. 通过内置红外发射器回放信号，控制挂灯。
3. 通过 ESP32-S3 的 Wi-Fi 连接 MQTT broker，与 NovOS 侧通信。

因此当前推荐链路是：

```text
NovOS / novos-adapter-mqtt
            |
            v
       MQTT broker
            |
            v
        M5StickS3
      MQTT <-> IR
            |
            v
   Humanex CX9-B-A
```

CoreS3 降为可选调试控制台，不进入 V1 必选链路。只有后续确实需要大屏触控、独立桌面状态面板或本地调试 UI 时，再把它加入系统。

## 一句判断

StickS3 能替代的是“CoreS3 作为联网控制器”的角色；它不能消除红外单向通信带来的状态不确定性，也不能保证现有 NovOS 标准灯 MQTT 契约无需调整就能直接使用。

## 官方硬件事实

以下信息来自 M5Stack StickS3 官方文档：

- 产品 SKU：`K150`。
- 主控：`ESP32-S3-PICO-1-N8R8`。
- 存储：8 MB Flash + 8 MB PSRAM。
- 无线：Wi-Fi，可运行 MQTT 客户端。
- 电池：250 mAh；持续联网部署建议 USB 常供电，不把电池续航作为 V1 前提。
- 内置红外发射器和红外接收器。
- `IR_TX = GPIO46`。
- `IR_RX = GPIO42`。
- 红外接收必须使用 ESP32 的 RMT 外设，不能按普通 GPIO 解码方式实现。
- 红外接收时必须关闭扬声器功放；官方示例使用 `M5.Speaker.end()`。
- 学习时发射端和接收端应尽量正对，距离大于 30 cm；过近可能导致异常。
- M5Unified 默认初始化会关闭 `EXT_5V_EN`，这会同时关闭 Grove、Hat EXT_5V 和 IR TX/RX 供电并把相关引脚切为输入；无外部 5V 输入时，红外初始化应调用 `M5.Power.setExtOutput(true)` 恢复 IR TX/RX 供电。
- `EXT_5V_EN` 设为输出模式后，只允许从 USB 或 Hat2-Bus 的 `5VIN` 给设备供电；不要再从 Grove 或其它输出接口反向供电，避免短路损坏。

官方 Arduino 示例给出了 NEC 协议的 RMT 收发代码，但目前不知道 CX9-B-A 是否使用 NEC。第一轮学习必须同时保存原始 RMT pulse，不能只保存 NEC 的 `address` / `command`。

官方资料：

- StickS3 产品文档：https://docs.m5stack.com/en/core/StickS3
- StickS3 IR 收发示例：https://docs.m5stack.com/en/arduino/m5sticks3/ir_nec
- StickS3 M5PM1 电源控制：https://docs.m5stack.com/en/arduino/m5sticks3/m5pm1
- StickS3 原理图：https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/K150_Stick_S3_PRJ_V0.6_20251111_2025_11_17_16_10_24.pdf

官方 IR 示例的最低版本要求：

- M5Stack Board Manager `>= 3.2.5`
- M5Unified `>= 0.2.12`
- M5GFX `>= 0.2.18`

## 从 Agent-Monitor 复用什么

已检查 `Agent-Monitor` 项目当时的代码。它已经在 CoreS3 固件中实现了可移植到 StickS3 的 MQTT 运行骨架。

可以复用的机制：

- `WiFiClient + ArduinoMqttClient`。
- broker host、port、username、password、client ID 的本地配置方式。
- MQTT Last Will：异常断线时发布 retained `offline` presence。
- 连接成功后发布 retained `online` presence。
- 周期性 heartbeat/presence。
- MQTT 连接和订阅状态分开维护。
- 断线后从 500 ms 到 8 s 的指数退避重连。
- 主循环持续调用 `mqttClient.poll()`。
- JSON payload 使用 ArduinoJson 编解码。
- command/action 不 retain，状态/presence retain。

需要修改而不是照搬的部分：

- 不使用 `agent-monitor/v1/...` topic；那是 Agent Monitor 的产品协议。
- 不使用 `DisplayState`、approval action、触摸和 CoreS3 UI 代码。
- 挂灯命令与状态按 NovOS 设备契约设计。
- Agent-Monitor 固件的 action/presence 使用 QoS 0；NovOS 当前灯设备契约要求 command/state 使用 QoS 1，StickS3 挂灯固件应按后者实现。
- HTTP fallback 对当前 V1 不是必需项。串口 + MQTT 已足够完成分层验证，除非后续调试证明 HTTP 有明确价值。

Agent-Monitor 参考位置：

- `firmware/src/main.cpp`：`configureMqttClient()`、`publishMqttPresence()`、`connectMqttIfNeeded()`。
- `firmware/include/config.example.h`：Wi-Fi 与 MQTT 本地配置结构。
- `firmware/platformio.ini`：`ArduinoMqttClient`、ArduinoJson、M5Unified 依赖组合。
- `docs/adr/0004-mqtt-first-device-transport.md`：retained state、presence、Last Will 和重连理由。

## NovOS MQTT 契约边界

相邻项目的 `Light-mqtt/Template 设备侧MQTT接入契约.md` 给出的标准灯 topic 是：

```text
{prefix}/light/{id}/command
{prefix}/light/{id}/state
```

建议为本项目预留：

```text
prefix = humanex-monitorlight
id     = cx9-ba

humanex-monitorlight/light/cx9-ba/command
humanex-monitorlight/light/cx9-ba/state
```

标准灯 payload 示例：

```json
{ "state": "ON", "brightness": 128, "color_temp": 250 }
```

但该契约要求设备在收到命令、真正应用到硬件后，在默认 2 秒内回显“真实完整状态”。红外挂灯通常没有回传通道，StickS3 最多知道“红外已经发出”，不知道挂灯是否收到，也不知道原装遥控器是否在别处改变了状态。

所以不能直接把 shadow state 冒充真实 reported。接入策略必须等码表实验后在下面两条路径中选择：

### 路径 A：遥控器提供绝对命令

如果存在独立 ON/OFF、固定亮度档和固定色温档，StickS3 可以发布带明确 `assumed_state` 的 shadow。NovOS 侧仍需确认现有 adapter 是否支持“假定状态”语义；若不支持，需要补 adapter/descriptor 语义，不能仅靠固件照抄标准 state payload。

### 路径 B：遥控器只有 toggle / up / down / cycle

优先使用命令型能力，例如：

```text
power_toggle
brightness_up
brightness_down
cct_warmer
cct_cooler
mode_cycle
```

这种设备不应直接暴露为可任意设置百分比和 Kelvin 的标准 `light.*`。需要 NovOS 侧提供 `ir.remote` 命令 trait 或为 MQTT adapter 增加对应 dialect/spec。

在完成红外码表分类前，先用项目私有调试 topic，不承诺可被现有标准灯 adapter 直接纳管：

```text
humanex-monitorlight/ir/cx9-ba/command
humanex-monitorlight/ir/cx9-ba/event
humanex-monitorlight/ir/cx9-ba/presence
```

## V1 最小固件职责

StickS3 固件只做以下工作：

1. 初始化 M5Unified/M5PM1，并正确打开 IR TX/RX 所需电源。
2. 学习模式下关闭 speaker amp，使用 RMT 接收 GPIO42。
3. 保存每个按键的协议字段和原始 pulse；同一按键至少采集 3 次。
4. 回放时使用 RMT 从 GPIO46 发射。
5. 连接 Wi-Fi 和 MQTT broker。
6. 订阅 command topic，校验 JSON 和允许的 command 名称。
7. 发射完成后发布 event；只报告 `sent`，不报告未经验证的 `applied`。
8. 发布 retained online/offline presence，并进行退避重连。
9. 把码表和 Wi-Fi/MQTT 密钥分开；密钥不提交到项目公共文件。

## 最小验证顺序

1. **IR RX**：原装遥控器一个按键连续采集 3 次，拿到稳定 raw pulse。
2. **IR TX**：StickS3 本地按键回放该 pulse，挂灯 10 次中成功至少 9 次。
3. **完整码表**：采集全部按键，区分绝对命令与相对命令。
4. **MQTT 通道**：电脑发布一条 command，StickS3 收到后发 IR，并发布 event。
5. **故障行为**：broker 重启、Wi-Fi 断开后能自动恢复订阅；Last Will 能显示 offline。
6. **NovOS 建模**：根据码表分类决定使用标准 `light.*` 还是命令型 `ir.remote`。
7. **NovOS 验收**：从 `novos-cli` 发起命令，MQTT、StickS3、红外三个阶段都有可追踪证据。

## 暂不做

- 不先开发 CoreS3 控制台。
- 不先写完整 NovOS adapter。
- 不假设遥控器一定是 NEC。
- 不先承诺亮度百分比或色温 Kelvin 的绝对控制。
- 不把“MQTT 消息发送成功”写成“挂灯状态已真实生效”。

## 下一步产物

第一项开发产物应是 StickS3 的最小红外学习固件，以及：

```text
ir-codes/cx9-ba-codes.json
```

只有这个文件通过实物采集和回放验收后，才开始定最终 MQTT payload 和 NovOS trait。
