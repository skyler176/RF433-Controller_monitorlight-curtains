# CX9-B-A 433.92 MHz 采集与开发计划

日期：2026-07-14

## 决策

以 433.92 MHz ASK/OOK 单向遥控作为当前工作假设，但必须先完成物理层实测，
再修改固件和 NovOS adapter。

一台 CoreS3 或 StickS3 即可承担：

```text
RF capture <-> protocol analysis <-> RF replay <-> MQTT bridge
```

裸 CoreS3/StickS3 只有 2.4 GHz Wi-Fi/Bluetooth，没有 433.92 MHz 射频前端，
因此至少需要外接接收和发射 Unit。

## 推荐采购清单

### 本项目核心器件

| 数量 | M5Stack 产品 | SKU | 用途 |
| --- | --- | --- | --- |
| 1 | Unit RF433R | U113 | 433.92 MHz ASK 接收 |
| 1 | Unit RF433T | U114 | 433.92 MHz ASK 发射 |
| 1 | Unit Hub | U006 | 可选：一个 Grove 口同时连接 RX 和 TX |

RF433R 和 RF433T 都会使用：前者负责学习，后者负责回放。Unit Hub 不是协议
必需件；开发初期可以手动拔插，最终需要同时监听和发射时再使用 Hub 或 Grove Y
线。三个产品均自带 HY2.0-4P Grove 线。

### 可选但推荐

- RTL-SDR 接收棒和 433 MHz 天线：用于确认真实载波、带宽和调制，避免 RF433R
  的固定带宽或 1 kbps 接收能力遗漏信号。
- 8 通道逻辑分析仪：用于观察 RF433R 输出，或在确认引脚后观察原遥控板射频
  发射芯片的数字输入。
- Grove 转 Dupont 线：仅在需要接逻辑分析仪或绕过 Unit Hub 时使用。

不要购买 LoRa433 Unit。LoRa 是另一种调制和数据链路，不能直接学习普通
433.92 MHz ASK/OOK 遥控器。

## 主控选择

### CoreS3：开发期推荐

优点：

- 320 x 240 触屏更适合显示脉宽、重复次数和采集列表。
- 16 MB Flash、8 MB PSRAM 和 microSD 更适合保存大量原始样本。
- M5-Bus 和 Grove 扩展空间更大。

PORT.A 连接映射：

```text
Yellow / GPIO2 -> RF433T RF_TX
White  / GPIO1 <- RF433R RF_RX
Red    / 5V    -> 两个 Unit 供电
Black  / GND   -> 共地
```

### StickS3：最终常驻推荐

优点：

- 体积小，现有 MQTT 固件骨架已在 StickS3 上构建通过。
- 内置屏幕和两个按键足以完成选择、学习、发送和状态显示。

PORT.CUSTOM 连接映射：

```text
Yellow / GPIO9  -> RF433T RF_TX
White  / GPIO10 <- RF433R RF_RX
Red    / 5V     -> 两个 Unit 供电
Black  / GND    -> 共地
```

初始化时需要：

```cpp
M5.Power.setExtOutput(true);
```

## 为什么一个 Unit Hub 可以同时接收和发送

Unit Hub 的三个端口是四根线直接并联，不是总线转换器：

- RF433T 只占用黄线，白线为 NC。
- RF433R 只占用白线，黄线为 NC。
- 两者共享 5V 和 GND。

因此不会发生 GPIO 冲突。软件中分别把 TX pin 配置为输出、RX pin 配置为输入。

## 阶段 1：确认物理层

先只连接 RF433R，原装旋钮距离接收器 0.5 至 1 米。

验收：

- 所有控制动作在 RF_RX 上产生明显脉冲。
- 空闲噪声与真实发送可以可靠区分。
- 如果 RF433R 无法稳定接收，不直接判定遥控器不是 433.92 MHz；改用 RTL-SDR
  扫描 433.92 MHz 附近并确认数据速率。

## 阶段 2：建立采集矩阵

每个动作至少采集 10 个独立样本：

| 类别 | 动作 |
| --- | --- |
| 旋钮 | 顺时针一格、逆时针一格、连续慢转、连续快转 |
| 旋钮按键 | 短按、长按 |
| S1 | 短按、长按 |
| S2 | 短按、长按 |
| S3 | 短按、长按 |
| 边界 | 灯关闭时旋转、亮度最高/最低时继续旋转 |

每个样本记录：

- 捕获时间和动作标签。
- 高低电平持续时间，单位微秒。
- 完整发送持续时间。
- 帧数、帧间隔和每帧重复次数。
- 灯的实际行为。
- 遥控器和灯的距离。

建议产物：

```text
rf-captures/
  raw/
  normalized/
  cx9-ba-command-catalog.json
```

## 阶段 3：协议判断

按以下顺序分析：

1. 聚类短脉冲、长脉冲和同步间隔。
2. 判断编码方式，例如 PWM、pulse distance 或 Manchester。
3. 比较同一动作 10 次样本是否完全一致。
4. 找出固定不变的遥控器 ID、随动作变化的 command 和可能的校验字段。
5. 断开遥控器电池后重复采样，确认 ID 或计数器是否改变。
6. 判断旋钮是相对 `up/down`，还是携带绝对档位。

若每次按键 payload 都变化，先判断是否只是计数器或校验变化，不立即认定为
滚动码。

## 阶段 4：原始回放

第一轮不重写协议，只将已经归一化的高低电平持续时间通过 RF433T 原样输出。

验收：

- 单个动作连续回放 20 次，挂灯正确响应至少 18 次。
- 明确安全发送间隔。
- 原装遥控器仍能正常控制。
- 灯断电重启后不需要未知的重新配对流程。

## 阶段 5：RF MQTT bridge

协议验证后，复用现有固件的 MQTT、重连、presence、NVS 和屏幕框架，将：

```text
IR RMT capture/send
```

替换为：

```text
RF433R pulse capture + RF433T pulse send
```

事件语义保持：

```json
{
  "result": "sent",
  "hardware_confirmed": false,
  "state_is_assumed": true
}
```

## 阶段 6：NovOS 建模

如果协议只有 toggle、brightness up/down 或色温循环，使用命令型能力：

```text
rf.remote/power_toggle
rf.remote/brightness_up
rf.remote/brightness_down
rf.remote/cct_cycle
```

只有当协议和实物行为证明能从未知状态可靠到达绝对目标时，才升级为：

```text
light.onoff
light.brightness
light.cct
```

## 第一阶段完成标准

- 433.92 MHz 或其它实际频率已有测量证据。
- 所有旋钮和按键动作均有 10 次样本。
- 同类样本的稳定字段和变化字段已经标注。
- 至少一个命令通过 RF433T 回放成功率达到 90%。
- 原始数据已保存在项目内，可脱离采集设备复现。
