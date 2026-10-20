# Humanex CX9-B-A RF433 接入项目总结

更新时间：2026-07-21

## Zenoh-Pico 更新

StickS3 设备固件已加入并编译通过 zenoh-pico 协议腿，提供命令/信息 queryable、
发射事件、liveliness 和重连，同时保持离线本地遥控可用。项目传输方向已从 MQTT
切换为 Zenoh；独立 NovOS adapter、真实 router 和自动控制器已完成功能链路验收。StickS3 当前
运行固件为 `0.2.1`。详细设计见
`Zenoh-pico-device-integration.md`。

## 当前成果

项目已从错误的红外方向切换到 433.92 MHz ASK/OOK 射频方向，并完成一个可用的
StickS3 本地遥控 MVP：

- CoreS3 完成原装控制器的 RF 采集、触屏录制、SPIFFS 保存和协议分析。
- 八个动作共保留 80 份原始 `.rfraw` 样本。
- 协议已分类为 24 位静态脉宽码，未观察到计数器或滚动码。
- 八个动作码已固化到 CoreS3 和 StickS3 的 RMT 发射固件。
- StickS3 双按键界面已烧录并实测可用：A 选择，B 单次发送，B 长按连续调节。
- 用户于 2026-07-20 确认当前 StickS3 方案可行。

“可行”代表功能性实测通过，不等同于八个动作都已完成 18/20 的可靠性验收。

## 最终硬件分工

| 设备 | 角色 | RF 接线 |
| --- | --- | --- |
| CoreS3 | 采集、分析、调试台 | PORT.B：TX GPIO9，RX GPIO8 |
| StickS3 | 最终本地遥控器 | PORT.CUSTOM：TX GPIO9，可选 RX GPIO10 |
| RF433T | 向灯具发射 | Grove 黄线 |
| RF433R | 开发期接收/环回，可不常驻 | Grove 白线 |

仅做日常控制时，StickS3 直连 RF433T 即可，不需要 Hub 和 RF433R。

## 已解码命令

| 动作 | 指令 | 数据支持 |
| --- | --- | --- |
| 开关 toggle | `0x162A5E` | 10/10 |
| 亮度增加 | `0x162AAA` | 10/10 |
| 亮度降低 | `0x162A5D` | 10/10 |
| 色温 CW | `0x162AAF` | 10/10 |
| 色温 CCW | `0x162A5F` | 9/10 完整帧 |
| S1 | `0x162AC5` | 10/10 |
| S2 | `0x162A5B` | 10/10 |
| S3 | `0x162AA5` | 10/10 |

统一发射参数：1 MHz RMT、MSB first、`0=H173/L547 us`、
`1=H513/L202 us`、帧间隔 `3149 us`、每次 5 帧。

## StickS3 操作

| 输入 | 行为 |
| --- | --- |
| A 短按 | 下一动作 |
| A 长按 650 ms | 上一动作，仍需单独完成实机日志验收 |
| B 短按 | 当前动作发送一次 |
| B 长按 | 亮度/色温每 300 ms 连续发送 |

POWER 和 S1/S2/S3 不允许连发。设备是单向控制，`sent` 不能解释为灯具状态回传。

## 仍未解决的 Linear 工作

以下状态基于 2026-07-20 重新读取的 Linear 项目和当前本地成果：

| Issue | 当前判断 | 剩余验收 |
| --- | --- | --- |
| `LN-96` 载波与调制确认 | 进行中 | 用 SDR/频谱仪测量中心频率、带宽和频偏 |
| `LN-97` 完整动作采集 | 进行中 | 补慢/快连续旋转、边界、距离和灯具行为元数据；处理 CCT CCW 无效样本 |
| `LN-98` 协议分析与码表 | 进行中 | 补 normalized 产物、字段含义、电池移除后复测 |
| `LN-99` RF433T 实物回放 | 进行中 | 八个动作各做 20 次并达到至少 18/20；测最小间隔和掉电/配对行为 |
| `LN-100` StickS3/CoreS3 bridge 固件 | 部分完成 | 本地与联网 MVP 已完成；补 A 长按、长期运行和完整故障矩阵 |
| `LN-101` Zenoh 设备协议腿 | 功能通过 | command/event/live 和运行期约五秒重连已通过；补冷启动、Wi-Fi/路由器重启矩阵 |
| `LN-102` NovOS adapter/descriptor | 功能通过 | descriptor、相对命令、CLI/automation 和 `sent` 语义已接入；补长期稳定性验收 |
| `LN-103` 端到端验收 | 进行中 | 自动空闲关闭、恢复开启、亮度步进已通过；补八命令逐项及故障矩阵验收 |

`LN-95` 硬件准备已完成。`LN-96` 至 `LN-103` 均不应仅因一次功能性测试而关闭。

## 建议后续顺序

1. 完成 `LN-99` 的八动作 18/20 实测表，这是继续联网开发的质量门。
2. 同时用 SDR 完成 `LN-96`，消除 433.92 MHz 仍是器件推断的边界。
3. 收口 `LN-97`、`LN-98` 的数据和协议文档。
4. 对 StickS3 Zenoh 连接做长时、冷启动、Wi-Fi/路由器重启验收，收口 `LN-100`、`LN-101`。
5. 完成八命令逐项端到端与故障矩阵验收，收口 `LN-102`、`LN-103`；继续保持单向灯具没有真实
   reported state 的建模边界。

## 关键产物

- `RF433-protocol-reproduction.md`：协议参数和复现步骤。
- `rf-captures/cx9-ba-command-catalog.json`：机读码表。
- `rf-captures/raw/2026-07-17-cores3-guided-8-actions/`：80 份原始样本。
- `firmware/rf433-bridge/`：CoreS3 采集/回放固件。
- `firmware/sticks3-rf433-remote/`：StickS3 双按键本地遥控固件。
- `StickS3-RF433-migration.md`：硬件和交互迁移决策。
- `Zenoh-pico-device-integration.md`：Zenoh 设备协议及 NovOS adapter 边界。

## 可复现构建

```bash
cd firmware/sticks3-rf433-remote
pio run
```

本轮成功烧录的 factory 镜像 SHA-256：

```text
a804a1d28ff88de11a60147b1727e802ecebe3ac9be5b1d9f29b762bccca51f1
```
