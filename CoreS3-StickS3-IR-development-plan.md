# CoreS3 + StickS3 接入 CX9-B-A 屏幕挂灯开发计划

> 状态：已被 `RF433-capture-development-plan.md` 取代。本文的 IR 学习和发射阶段
> 不再执行；CoreS3/StickS3 的联网、UI 和 MQTT 分工仍可作为历史参考。

日期：2026-07-09

> 2026-07-13 更新：CoreS3 已从 V1 必选链路移除。StickS3 可单机完成红外学习、红外发射和 MQTT 联网。最新决策、Agent-Monitor MQTT 复用边界及 NovOS 状态语义见 `StickS3-only-IR-MQTT-decision.md`；本文保留为包含可选 CoreS3 调试台的完整阶段计划。

## 硬件结论

这次确认后的关键结论：

- `M5StickS3` 是 M5Stack 官方产品，SKU `K150`。
- StickS3 内置 `IR transmitter + IR receiver`，可以同时做红外学习和红外发射。
- StickS3 的红外引脚：
  - `IR_TX = GPIO46`
  - `IR_RX = GPIO42`
- StickS3 红外接收有两个官方限制：
  - 必须使用 ESP32 RMT 外设，不能用普通 GPIO decode。
  - 使用红外接收时必须关闭 speaker amplifier，否则红外接收无法正常工作。
- CoreS3 本体不内置红外，但屏幕更大、触控更好，适合作为调试台、控制台或 NovOS 本地入口。

所以推荐组合是：

> StickS3 = 红外学习 / 红外发射执行器  
> CoreS3 = 调试控制台 / 用户交互面板 / 可选 NovOS 边缘入口  
> NovOS adapter = 运行在 Mac / Linux 上，负责把 NovOS trait 翻译成 StickS3 的 HTTP 或 MQTT 命令

## 推荐职责分工

### StickS3

StickS3 放在屏幕挂灯附近，对准灯的红外接收窗口。

职责：

- 学习 CX9-B-A 原装遥控器红外码。
- 保存红外码表。
- 发射红外码控制挂灯。
- 通过 Wi-Fi 暴露 HTTP 或 MQTT 控制接口。
- 维护本地 shadow state。
- 在小屏上显示当前状态、Wi-Fi、最近命令、学习结果。

### CoreS3

CoreS3 不直接负责红外。

职责：

- 开发期作为调试控制台。
- 显示 StickS3 当前在线状态、码表状态、最近命令。
- 触屏按钮触发测试命令，比如开关、亮度+、亮度-、色温切换。
- 后续可作为 NovOS 桌面入口设备的一部分。

如果只想先打通挂灯，CoreS3 可以暂时不参与。先用电脑通过 HTTP/MQTT 控制 StickS3。

### NovOS Adapter

adapter 运行在 NovOS 所在机器上。

职责：

- 声明 `humanex-cx9-ba-monitor-light` 设备。
- 暴露 `ir.remote` 或 `light.*` trait。
- 把 NovOS 的 set/invoke 转成 StickS3 的 HTTP/MQTT 命令。
- 根据 StickS3 返回结果更新 reported。
- 明确标记 `assumed_state`，避免把红外单向控制伪装成真实设备回传。

## 第一阶段：StickS3 单机红外学习

目标：

验证 StickS3 能学习 CX9-B-A 原装遥控器。

开发内容：

1. 初始化 StickS3。
2. 关闭 speaker amplifier。
3. 打开 IR_RX，使用 ESP32 RMT 接收红外。
4. 按原装遥控器按键。
5. 在串口和屏幕显示：
   - 协议类型
   - address
   - command
   - raw pulse
   - 重复次数
6. 每个按键采集至少 3 次。

需要记录的按键：

- power / power_toggle
- brightness_up
- brightness_down
- cct_warmer
- cct_cooler
- mode_cycle
- auto / favorite / eye-care，如果遥控器有这些键

输出文件：

```text
ir-codes/cx9-ba-codes.json
```

码表结构建议：

```json
{
  "device": "humanex-cx9-ba-monitor-light",
  "model": "CX9-B-A",
  "learned_with": "M5StickS3",
  "codes": {
    "power_toggle": {
      "protocol": "unknown",
      "address": null,
      "command": null,
      "raw": []
    }
  }
}
```

验收标准：

- 每个遥控器按键都能被 StickS3 识别。
- 同一个按键 3 次采集结果一致，或 raw 差异在可接受范围。
- 红外接收距离大于 30cm，避免官方提示的过近异常。

## 第二阶段：StickS3 单机红外发射

目标：

验证 StickS3 能复现遥控器控制挂灯。

开发内容：

1. 读取或写死 `cx9-ba-codes.json`。
2. 通过 StickS3 按键触发发射。
3. 每次发射后屏幕显示：
   - command
   - send result
   - last_sent_at
4. 测试每个命令的成功率。

测试项目：

- 单次发射是否有效。
- 连续发射最小间隔。
- 对准角度要求。
- 最远控制距离。
- 灯断电重启后状态是否记忆。
- 原装遥控器改变状态后，StickS3 shadow 是否会失真。

验收标准：

- 每个命令 10 次发射成功率大于 90%。
- 连续两次命令之间的安全间隔明确，比如 300ms 或 500ms。
- 明确哪些命令是绝对状态，哪些命令只是 toggle / cycle。

## 第三阶段：StickS3 网络控制接口

目标：

让电脑、CoreS3、NovOS adapter 都能通过网络控制 StickS3。

推荐先做 HTTP，后做 MQTT。

### HTTP V1

接口：

```text
GET  /health
GET  /state
POST /command
POST /learn/start
POST /learn/stop
GET  /codes
```

命令示例：

```json
{
  "command": "brightness_up"
}
```

状态示例：

```json
{
  "device": "humanex-cx9-ba-monitor-light",
  "online": true,
  "assumed_state": true,
  "on": null,
  "brightness": null,
  "color_temperature": null,
  "last_command": "brightness_up",
  "last_sent_at": "2026-07-09T21:00:00+08:00"
}
```

### MQTT V2

Topic 建议：

```text
humanex/cx9-ba/command
humanex/cx9-ba/state
humanex/cx9-ba/event
```

command payload：

```json
{ "command": "cct_warmer" }
```

state payload：

```json
{
  "assumed_state": true,
  "last_command": "cct_warmer",
  "last_sent_at": "2026-07-09T21:00:00+08:00"
}
```

验收标准：

- 电脑能通过 `curl` 控制挂灯。
- CoreS3 能通过 HTTP 调 StickS3。
- StickS3 断线重连后还能恢复服务。

## 第四阶段：CoreS3 调试控制台

目标：

用 CoreS3 做一个屏幕更大的调试界面。

CoreS3 UI 页面：

- Device status
- Wi-Fi status
- StickS3 IP
- 最近命令
- 码表是否完整
- 测试按钮：
  - Power
  - Brightness +
  - Brightness -
  - CCT warmer
  - CCT cooler
  - Mode cycle

通信方式：

```text
CoreS3 -> HTTP -> StickS3
```

验收标准：

- 不用电脑也能通过 CoreS3 控制挂灯。
- CoreS3 能显示 StickS3 当前 `/state`。
- 控制失败时显示错误，而不是静默失败。

## 第五阶段：NovOS Adapter

目标：

把 CX9-B-A 作为 NovOS 设备出现。

### V1：命令型 adapter

在没有确认绝对状态前，优先做命令型：

```text
humanex-cx9-ba-monitor-light/ir.remote/power_toggle
humanex-cx9-ba-monitor-light/ir.remote/brightness_up
humanex-cx9-ba-monitor-light/ir.remote/brightness_down
humanex-cx9-ba-monitor-light/ir.remote/cct_warmer
humanex-cx9-ba-monitor-light/ir.remote/cct_cooler
```

这个版本最诚实，因为红外是单向控制。

### V2：标准灯 adapter

只有当实验确认可以可靠到达绝对状态时，才升级为：

```text
light.onoff/on
light.brightness/brightness
light.cct/color_temperature
```

如果只能按档位控制，adapter 应返回 `adjusted`，例如：

```text
用户请求 brightness=37
设备只能支持 20/40/60/80/100
adapter 实际发 40
reported=40
outcome=adjusted
```

如果只有 toggle / cycle，不要暴露成标准灯 trait。

## 开发技术栈

### StickS3 firmware

推荐：

- PlatformIO
- Arduino framework
- M5Unified
- M5PM1
- IRremoteESP8266 或 ESP-IDF RMT

官方 PlatformIO 基础配置：

```ini
[env:m5stack-sticks3]
platform = espressif32@6.12.0
board = esp32-s3-devkitc-1
framework = arduino
board_build.arduino.partitions = default_8MB.csv
board_build.arduino.memory_type = qio_opi
build_flags =
    -DESP32S3
    -DBOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue
    -DCORE_DEBUG_LEVEL=5
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
lib_deps =
    M5Unified=https://github.com/m5stack/M5Unified
    M5PM1=https://github.com/m5stack/M5PM1
```

需要在项目里补充红外库和网络库。

### CoreS3 firmware

推荐：

- PlatformIO
- Arduino framework
- M5Unified
- HTTP client

CoreS3 只做控制台时不需要红外库。

### NovOS adapter

推荐：

- 初期用 Rust adapter，HTTP client 调 StickS3。
- 如果想快速验证，也可以先写一个 Python bridge，但最终最好回到 NovOS 现有 adapter 风格。

## 目录建议

```text
Humanex-monitorlight/
  CX9-B-A-monitorlight-research.md
  CoreS3-StickS3-IR-development-plan.md
  ir-codes/
    cx9-ba-codes.example.json
  firmware/
    sticks3-ir-bridge/
    cores3-monitorlight-console/
  adapter/
    novos-adapter-humanex-monitorlight/
```

## 最小开工顺序

1. 先只开发 StickS3，不碰 CoreS3，不碰 NovOS。
2. 让 StickS3 成功学习一个按键，比如 `power_toggle`。
3. 让 StickS3 成功发射这个按键，灯有反应。
4. 扩展到全部遥控器按键。
5. 给 StickS3 加 HTTP `/command`。
6. 用电脑 `curl` 控制挂灯。
7. 再做 CoreS3 控制台。
8. 最后写 NovOS adapter。

这个顺序的好处是每一步都有物理验收，不会把红外码、固件、网络、NovOS adapter 四个问题混在一起调。

## 官方资料依据

- StickS3 官方商品页和文档确认：ESP32-S3-PICO-1-N8R8、8MB Flash、8MB PSRAM、1.14" LCD、Wi-Fi、麦克风、扬声器、内置 IR transmitter + IR receiver。
- StickS3 官方文档确认：`IR_TX = GPIO46`、`IR_RX = GPIO42`。
- StickS3 官方文档提示：红外接收必须使用 ESP32 RMT，且接收时要关闭扬声器功放。
- CoreS3 官方文档确认：CoreS3 是 ESP32-S3 主控，带 2.0" 触控屏、Wi-Fi、摄像头、双麦、扬声器、IMU、RTC，适合做控制台和调试入口。
