# 松能 CX9-B-A 屏幕挂灯接入 NovOS 调研

> 状态：已被 `RF433-capture-development-plan.md` 取代。2026-07-14 根据原装
> 无线旋钮 PCB 照片，原文“已知为红外遥控器”的前提不成立。以下内容保留为
> 历史调研记录，其中单向状态语义仍然有效，IR 物理层和硬件方案不再作为当前结论。

日期：2026-07-09

## 结论摘要

当前公开网页没有找到可靠的 `松能 CX9-B-A` 官方技术文档、开放 API、红外协议码表或说明书。基于“可通过红外遥控器控制”这一已知事实，先把它建模为：

> 普通屏幕挂灯 + 单向红外遥控接口

这意味着接入 NovOS 的核心不是调用厂商 API，而是：

1. 学习遥控器每个按键的红外码。
2. 用一个红外发射硬件复现这些码。
3. 把红外按键能力包装成 NovOS adapter。
4. 根据码表能力决定是否能升级为标准 `light.*` trait。

最保守的 V1 设计应该先做“红外命令型设备”，不要直接假装成有真实反馈的智能灯。

## 已检索信息

检索关键词包括：

- `松能 CX9-B-A 屏幕挂灯 红外 遥控器 技术 参数`
- `CX9-B-A 松能`
- `松能 CX9 屏幕挂灯`
- `Humanex CX9-B-A monitor light infrared remote control`
- `松能 屏幕挂灯 遥控器`

结果：

- 没有找到可核验的 CX9-B-A 官方规格页。
- 没有找到公开红外码表。
- 没有找到局域网、蓝牙、Wi-Fi、Matter、MQTT、HomeKit 等智能接入资料。
- 可以参考通用红外学习/发射方案，而不是厂商专有协议。

有用的通用资料：

- ESPHome `remote_receiver`：用于接收和 dump 红外码。
  - https://esphome.io/components/remote_receiver/
- ESPHome `remote_transmitter`：用于发送红外码。
  - https://esphome.io/components/remote_transmitter/
- Home Assistant Broadlink integration：可学习和发送 IR/RF 命令。
  - https://www.home-assistant.io/integrations/broadlink/
- IRremoteESP8266：ESP8266/ESP32 红外收发库。
  - https://github.com/crankyoldgit/IRremoteESP8266

## 第一性原理判断

红外遥控不是“设备在线协议”，而是“按键广播”。

发送端只是在空气中发一段调制光信号，灯收到就执行。发送端通常不知道：

- 灯有没有真的收到。
- 灯现在是开还是关。
- 当前亮度是多少。
- 当前色温是多少。
- 是否被原装遥控器、手动按键或断电重启改变过状态。

所以在 NovOS 里，红外灯的 adapter 不能天然承诺真实 `reported`。除非额外加入传感器或设备本身有回传通道，否则 `reported` 只能是 adapter 的 shadow state。

## NovOS 建模边界

NovOS 的稳定心智模型：

- `novosd` 是控制面权威。
- `adapter` 是协议翻译层。
- `DeviceDescriptor` 描述设备。
- `Trait` 描述能力。
- `Attribute` 维护 `desired / reported`。
- `set` 写入 desired，由 adapter 执行后发布 reported。

灯的标准 trait：

- `light.onoff/on`
- `light.brightness/brightness`
- `light.cct/color_temperature`

但 CX9-B-A 是否能映射到这些标准 trait，取决于红外遥控器按键类型。

## 遥控器能力分类

### 情况 A：遥控器有绝对按键

例如：

- 独立开 / 独立关
- 25% / 50% / 75% / 100% 亮度
- 3000K / 4000K / 5000K / 6500K 色温

这种情况可以较好地映射为 NovOS 标准灯：

- `light.onoff/on`: `true | false`
- `light.brightness/brightness`: 0-100，按档位吸附
- `light.cct/color_temperature`: 按色温档位吸附

如果用户要求 37% 亮度，adapter 可以吸附到最近档位，比如 40%，返回 `adjusted`。

### 情况 B：遥控器只有相对按键或循环按键

例如：

- 电源 toggle
- 亮度 +
- 亮度 -
- 色温循环
- 模式循环

这种情况不应该直接伪装成完整 `light.brightness` / `light.cct`，因为 adapter 无法从任意状态可靠到达目标值。

更合理的 V1 是暴露命令型 trait，例如：

- `ir.remote/power_toggle`
- `ir.remote/brightness_up`
- `ir.remote/brightness_down`
- `ir.remote/cct_cycle`
- `ir.remote/cct_warmer`
- `ir.remote/cct_cooler`

后续如果通过实验确认设备状态可预测，再升级为标准 `light.*`。

## 推荐硬件方案

### 方案 1：ESP32 + 红外接收头 + 红外 LED

优点：

- 本地可控。
- 便宜。
- 可用 ESPHome 或 IRremoteESP8266。
- 后续可通过 MQTT/HTTP 接 NovOS。

建议：

- ESP32 先负责学习和发送红外码。
- NovOS adapter 不直接控制 GPIO，而是通过 MQTT/HTTP 调 ESP32。
- 这样 adapter 保持“语义翻译”，硬件细节留给 bridge。

### 方案 2：Broadlink RM4

优点：

- 成品硬件，学习红外比较方便。
- Home Assistant 支持成熟。

缺点：

- 多一层 Home Assistant / Broadlink 依赖。
- 对 NovOS 来说不是最直接的设备接入路径。

适合快速验证码表，不一定适合作为最终产品化方案。

## 推荐系统结构

```text
NovOS Agent / CLI
        |
        v
novosd
        |
        v
novos-adapter-ir-light
        |
        v
ESP32 IR Bridge
        |
        v
CX9-B-A 屏幕挂灯
```

ESP32 IR Bridge 可以先提供 MQTT 接口：

```text
humanex/cx9-ba/command
humanex/cx9-ba/state
```

命令示例：

```json
{ "command": "brightness_up" }
```

或：

```json
{ "on": true }
```

状态示例：

```json
{
  "assumed_state": true,
  "on": true,
  "brightness": 60,
  "color_temperature": 4000,
  "last_command": "brightness_up",
  "last_sent_at": "2026-07-09T20:00:00+08:00"
}
```

`assumed_state: true` 很重要，表示这是 adapter/bridge 推测状态，不是设备回传。

## Adapter V1 设计

### 设备 ID

```text
humanex-cx9-ba-monitor-light
```

### Adapter 名称

```text
novos-adapter-ir-light
```

或者更具体：

```text
novos-adapter-humanex-monitorlight
```

### V1 trait 建议

如果还没有确认绝对码表：

```text
ir.remote@1.0.0
```

commands:

- `power_toggle`
- `brightness_up`
- `brightness_down`
- `cct_warmer`
- `cct_cooler`
- `mode_cycle`

attributes:

- `assumed_state`: bool
- `last_command`: string
- `last_sent_at`: string

### V2 trait 建议

如果确认能可靠到达绝对状态：

```text
light.onoff@1.0.0
light.brightness@1.0.0
light.cct@1.0.0
```

吸附策略：

- 亮度只支持档位时，声明 `step` 或离散 enum。
- 色温只支持档位时，声明可用色温集合。
- 用户请求不可达值时返回 `adjusted`。

## 红外码表采集步骤

1. 准备红外接收硬件。
   - ESP32 + 红外接收头。
   - 或 Broadlink RM4。

2. 逐个按遥控器按钮。
   - 每个按钮至少采集 3 次。
   - 记录协议类型、地址、命令值或 raw pulse。

3. 建立码表文件。

```json
{
  "device": "humanex-cx9-ba-monitor-light",
  "model": "CX9-B-A",
  "transport": "ir",
  "codes": {
    "power_toggle": {
      "protocol": "unknown",
      "raw": []
    },
    "brightness_up": {
      "protocol": "unknown",
      "raw": []
    },
    "brightness_down": {
      "protocol": "unknown",
      "raw": []
    }
  }
}
```

4. 用红外发射器逐个回放。
   - 验证是否稳定触发。
   - 测试最小发送间隔。
   - 测试重复码是否需要特殊处理。

5. 记录设备状态行为。
   - 断电后是否记忆上次状态。
   - 电源键是 toggle 还是独立 on/off。
   - 色温是循环还是固定档位。
   - 亮度是否有边界反馈。

## V1 验收标准

最低可用版本：

- 能通过 NovOS 发出红外命令。
- 每个命令都有清晰 descriptor。
- adapter 不承诺无法验证的真实 reported。
- CLI 能执行：

```text
novos-cli invoke humanex-cx9-ba-monitor-light/ir.remote/brightness_up
novos-cli invoke humanex-cx9-ba-monitor-light/ir.remote/cct_warmer
```

如果实现为标准灯 trait，则需要：

```text
novos-cli set humanex-cx9-ba-monitor-light/light.onoff/on true --converged
novos-cli set humanex-cx9-ba-monitor-light/light.brightness/brightness 60 --converged
novos-cli set humanex-cx9-ba-monitor-light/light.cct/color_temperature 4000 --converged
```

但只有在 adapter 能证明目标状态已经真实生效时，才应该支持强语义的 `--converged`。

## 当前建议

下一步不要直接写 Rust adapter。先做码表和硬件验证：

1. 购买或准备 ESP32 红外收发硬件。
2. 抓取 CX9-B-A 原装遥控器所有按键码。
3. 回放验证成功率。
4. 判断它属于“绝对码表灯”还是“相对命令灯”。
5. 再决定 adapter 暴露 `light.*` 还是 `ir.remote`。

这个顺序可以避免 NovOS 语义被红外设备的单向、不可靠状态污染。
