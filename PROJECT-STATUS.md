# Humanex CX9-B-A 屏幕挂灯接入项目状态

更新时间：2026-07-31

## 2026-07-31 Zenoh 半死连接修复

- 当天运行日志出现大量短时闪断，并在 15:56:33 后形成长时间离线。故障期间
  StickS3 的局域网地址仍可 ping，HTTP API 仍返回 `200` 和 `status: ready`，
  Mac 上的 NovOS router 与 CX9 adapter 进程也正常；缺失的是 StickS3 到 router
  的 Zenoh TCP 连接，因此根因收敛到设备端 Zenoh session 未可靠恢复，而不是
  StickS3 断电、Wi-Fi 整体离线、RF433 或 Mac adapter 崩溃。
- 固件升级到 `0.3.3`：每次收到 `/info` 或 `/command` 都刷新入站活动时间；若
  连续 30 秒没有任何入站查询，即使 `z_session_is_closed()` 尚未报告关闭，也会
  主动销毁并重建 Zenoh session。
- 新增只读 HTTP 健康接口 `http://<设备局域网地址>/api/v1/health`，输出固件版本、
  Wi-Fi 状态与 RSSI、Zenoh 推定状态、session 建立次数、watchdog 次数和最近
  Zenoh 入站年龄。该接口用于诊断，不表示灯侧状态回读。
- PlatformIO 构建、实机烧录和 Flash 哈希校验通过；资源占用为 RAM 17.1%、
  Flash 41.0%。人为暂停 Mac 端 CX9 adapter 探测后，watchdog 在约 30 秒触发，
  session 自动重建；恢复 adapter 后，17:48:00 L2 链路重新上线，最近 `/info`
  继续每约 5 秒刷新。测试过程没有发送挂灯控制命令。
- 当前修复证明“半死 session 能自愈”，但不能替代长期在线率统计、Wi-Fi 丢失、
  router 重启、设备重启和冷启动的完整故障矩阵；相关 Linear issues 继续保持
  `In Progress`。

## 2026-07-23 StickS3 双系统部署

- StickS3 固件升级到 `0.3.2`：屏幕挂灯继续使用本地按键和原有
  Zenoh-Pico/NovOS 链路；办公室窗帘使用本地按键和设备直出的 HTTP/HTML，
  不进入 Zenoh 或 NovOS。
- 已编入最终验证的 15 路窗帘波形目录，并通过 PlatformIO 构建、实机烧录和
  Flash 哈希校验；RAM 使用 16.3%，Flash 使用 39.9%。
- 实机通过 DHCP 获取局域网地址。窗帘首页和只读 API 已返回 200，
  无效命令返回 400 且不发射；原屏幕挂灯 `/live` 与 `/info` 同时在线。
- 同网控制以设备的 DHCP 地址为可靠入口；
  `http://office-curtains.local/` 仅保留为 mDNS 辅助入口，手机端解析不够稳定。
  固件继续使用 DHCP，正式部署可按 MAC 在路由器设置地址保留。
- 网页请求改为立即进入 8 项 FIFO 并返回，射频任务后台顺序执行；页面实测约
  196 ms 加载完成，已移除全局按钮禁用和等待光标。
- 剩余验收是 StickS3 断电重启后测试双按键菜单，并逐一验证 5 个窗帘的上升、
  停止和下降。原 DC2702D 遥控器保持停用。

## 2026-07-21 NovOS 自动闭环验收

- StickS3 固件 `0.2.1` 已通过真实 NovOS router 在线；独立
  `novos-adapter-humanex-cx9` 已实现 descriptor、命令 trait、trace/request 关联和 liveliness 诊断。
- `novosd -> adapter -> Zenoh query -> StickS3 -> RF433T` 已持续执行开关、亮度步进和 S2 色温命令；
  adapter 始终保留 `hardware_confirmed: false`，没有伪造灯侧 reported。
- 17:33 的自然 10 分钟空闲触发关灯，17:45 输入恢复触发开灯；恢复同轮双 Dell 从 70% 调到 65%，
  随后三次一分钟间隔的增亮命令把挂灯推定状态恢复到最高档。
- 两次 Zenoh 接收通道短暂关闭均在约五秒内自动恢复，说明运行期重连路径有效；冷启动、路由器重启、
  Wi-Fi 断开和设备重启的系统化故障矩阵仍需单独验收。
- 当前结论是“功能链路与自动场景通过”，不是“灯具物理执行已回读”。八个动作各 18/20 的 RF 实物
  可靠性测试及灯侧真实反馈仍未完成。

## 2026-07-20 Zenoh-Pico 设备协议腿

- StickS3 固件升级到 `0.2.0`，保留已验证的双按键 RF 遥控行为。
- 固件固定使用 zenoh-pico `1.9.0`，已完成 PlatformIO 编译。
- 设备在 `humanex/cx9-ba/<device-id>` 下提供 `command`、`info`、`event`
  和 liveliness，支持 Wi-Fi 与 Zenoh 每五秒重连。
- 本地按键不依赖网络；远程和本地发射都只回报 `sent`，并明确
  `hardware_confirmed: false`。
- 传输方向由 MQTT 改为 Zenoh。设备采用小型原生协议，NovOS 描述符、trait 翻译和
  `drive` 回话仍由下一阶段的独立 adapter 负责。
- Wi-Fi 凭据与 NovOS router 局域网地址是部署输入，未写入仓库；真实 Zenoh 联通测试待做。
- 详细契约见 `Zenoh-pico-device-integration.md`。

## 2026-07-20 StickS3 迁移

- CoreS3 保留为 RF 采集和分析工具，StickS3 作为最终本地遥控器。
- 两台设备同时连接电脑：CoreS3 `/dev/cu.usbmodem1101`，StickS3
  `/dev/cu.usbmodem11101`；端口在重连后可能变化。
- StickS3 `PORT.CUSTOM` 使用 RF433T `TX=GPIO9`；如保留 RF433R，接收脚由
  CoreS3 的 GPIO8 改为 StickS3 的 GPIO10。
- 新固件位于 `firmware/sticks3-rf433-remote/`，包含八个静态码和双按键操作。
- 双按键方案：A 短按下一项、A 长按上一项、B 短按发送一次；亮度和色温按住
  B 每 300ms 连续发送，POWER 与 S1/S2/S3 禁止连发。
- StickS3 固件已构建并通过 `--before no-reset` 烧录，设备重新上线。
- 实时串口已验证 A 短按可循环八个动作，B 短按可发送 POWER、亮度和色温码，
  B 长按可连续发送亮度和色温码；所有 RMT 发射均返回 `result=sent`。
- 用户已确认 StickS3 当前方案可行，记为功能性实测通过；尚未完成八动作各
  18/20 的正式可靠性验收，因此不能关闭 RF 回放质量门。
- 详细决策和验收顺序见 `StickS3-RF433-migration.md`。

## Linear 跟踪

- 项目：`Humanex CX9-B-A Monitor Light RF433 Integration`
- 项目地址：https://linear.app/ln-ai/project/humanex-cx9-b-a-monitor-light-rf433-integration-adeb982ab131
- 状态文档：https://linear.app/ln-ai/document/project-status-and-rf-direction-3eda34af6853
- Issues：`LN-94` 至 `LN-103`
- 已完成：`LN-95 Prepare the CoreS3 RF433 capture and replay hardware`
- 当前主线：`LN-96` 至 `LN-100` 的物理层、数据收口、回放可靠性和 bridge 固件
- Linear OAuth 已于 2026-07-20 恢复，本轮将同步 Zenoh 方向和设备端进展。

Linear 中按“硬件准备 -> 物理层确认 -> 完整采集 -> 协议分析 -> RF 回放 ->
bridge 固件 -> Zenoh 设备协议腿 -> NovOS adapter -> 端到端验收”建立了阻塞关系。

## 当前结论

原项目按红外遥控器设计。根据 2026-07-14 提供的原装无线旋钮 PCB 照片，
该前提已被撤回。

当前高置信判断是：

> CX9-B-A 的原装旋钮通过 433.92 MHz ASK/OOK 单向射频控制挂灯。

用户确认的灯体能力包括开关、亮度调整和五档色温。对应 RF 命令语义尚待实测：
开关可能是 toggle，亮度可能是相对增减，五档色温可能是循环或独立档位。

主要依据：

- 遥控板没有明显红外发射 LED。
- 板上有 `13.560` MHz 晶体；常见 433.92 MHz 发射器使用固定 32 倍频，
  `13.560 x 32 = 433.92`。
- 晶体旁有 6 引脚射频芯片、匹配网络和 PCB 外围走线天线。
- 旋钮编码器、三个按键和 16 引脚控制芯片符合低成本 RF 遥控器结构。

以上仍是基于 PCB 的高置信推断，不是频谱实测结果。项目当前阶段应定义为：

> RF 物理层待确认，准备进行 433.92 MHz 采集、协议分析和回放验证。

当前推荐链路：

```text
NovOS / 后续 adapter
        |
        v
   Zenoh router
        |
        v
CoreS3 或 StickS3
        |
        v
M5Stack RF433T / RF433R
        |
        v
Humanex CX9-B-A
```

## 状态总览

| 模块 | 状态 | 结论 |
| --- | --- | --- |
| PCB 初步分析 | 已完成 | 高概率为 433.92 MHz ASK/OOK 单向遥控 |
| 载波频率确认 | 部分完成 | RF433R 可稳定接收；433.92 MHz 仍是器件与 PCB 推断，未做频谱实测 |
| 遥控协议采集 | 八组已录制 | 每组 10 份；CCT CCW 有 1 份无完整帧，其余动作均有 10/10 完整码 |
| RF 回放 | 功能性通过、正式验收待完成 | CoreS3/StickS3 均可发八个码；仍需每项 18/20 测试 |
| StickS3 本地遥控 | MVP 已完成 | 双按键选择、单次发送和亮度/色温长按连发可用 |
| IR 固件 | 历史实验 | 可构建，但不再作为当前设备控制路线 |
| Zenoh-Pico 设备协议腿 | 真机联通 | info/live、command reply、event correlation 已通过；保留短时延迟观察项 |
| Zenoh 本地配置 | 已完成 | 本机私有配置已连接 `pengfei_test-os` 的 router，凭据未入库 |
| NovOS adapter | 功能链路通过 | 独立 adapter 已接入并由 LaunchAgent 常驻；仍需完整故障矩阵和长期稳定性验收 |

## 2026-07-17 CoreS3 bring-up

- CoreS3 已通过 USB 连接，串口为 `/dev/cu.usbmodem1101`。
- RF probe 已在 `firmware/rf433-bridge/` 构建并烧录成功。
- 实际使用 PORT.B：RF433T `RF_TX=GPIO9`，RF433R `RF_RX=GPIO8`。
- CoreS3 外部 5V 输出已显式开启。
- RF433T 以 500 bps 发送诊断波形，RF433R 完整捕获对应边沿，本地收发环回通过。
- CoreS3 触屏触发已实机验证，连续点击均产生稳定的 23 边沿环回波形。
- Hub、两只 RF Unit、PORT.B 引脚与供电已验证；下一步采集原装旋钮信号。
- RF433R 空闲输出存在大量 ASK 噪声，正式采集需要增加最小脉宽和帧形过滤。
- CoreS3 已提供引导式 RF 学习界面：选择命令槽、点击 `RECORD`、按原装控制器，
  固定时间窗原始边沿保存到 SPIFFS。
- 当前动作矩阵为 `POWER`、`BRIGHT CW/CCW`、`CCT CW/CCW`、`PRESET S1/S2/S3`，
  每个动作目标采集 10 份独立样本。
- 首份 POWER 实机样本已保存为 `/power_01.rfraw`：按键窗口 14429 个原始边沿，
  过滤后保存 5072 个脉冲，未溢出；基线与按键阶段分别约 6799 和 5771 edges/s。
- 本轮实机操作已确认至少保存 POWER 4 份、BRIGHT CW 2 份、PRESET S1 1 份和
  PRESET S2 1 份；其中 `/power_04.rfraw` 达到 5120 脉冲并标记为溢出，分析时需保留该标记。

## 2026-07-17 协议解码与回放固件

- 已从 CoreS3 SPIFFS 导出 60 份完整样本：POWER、BRIGHT CW/CCW、S1/S2/S3
  各 10 份，归档于 `rf-captures/raw/2026-07-17-cores3-guided-complete/`。
- 信号为 24 位脉宽编码：`0 = H173us + L547us`，`1 = H513us + L202us`，
  帧间隔中位数约 `3149us`。
- 六个动作的稳定码分别为：POWER `0x162A5E`、BRIGHT CW `0x162AAA`、
  BRIGHT CCW `0x162A5D`、S1 `0x162AC5`、S2 `0x162A5B`、S3 `0x162AA5`。
- 上述每个码均出现在对应动作的 10/10 样本中，且未出现在另外五组样本中。
- CoreS3 固件已加入 `REPLAY`，通过 GPIO9 上的 RMT 硬件发送 5 帧；色温两组
  因没有本轮完整样本显示 `NO CODE`。
- 固件已构建并烧录，SPIFFS 样本保留，设备心跳正常。下一关是 RF433T 对灯具
  的实物回放测试；屏幕显示 `RF SENT` 只表示发射完成，不表示灯具已执行。

## 2026-07-17 色温补录

- 已导出八个动作共 80 份原始录制，归档在
  `rf-captures/raw/2026-07-17-cores3-guided-8-actions/`。
- CCT CW 稳定码为 `0x162AAF`，完整帧出现在 10/10 样本、共 15 次，其他动作 0 次。
- CCT CCW 稳定码为 `0x162A5F`，完整帧出现在 9/10 样本、共 14 次，其他动作 0 次。
  `cct_ccw_01.rfraw` 没有完整可解码帧，作为无效尝试原样保留。
- 开关键是单一 toggle 指令 `0x162A5E`，不是独立的开/关两条命令。其 24 位
  原始值、脉宽、帧间隔、重复次数和复现流程见 `RF433-protocol-reproduction.md`。
- CoreS3 回放固件已补入两个色温码；八个动作均可从屏幕选择并发送。

## 可复用与需要替换的内容

归档 MQTT 固件中已复用的设计：

- Wi-Fi 连接和非阻塞重连思路。
- command 校验、event 和 presence 的职责拆分。
- NVS 存储、屏幕状态和本地按键交互框架。
- `state_is_assumed: true` 和 `hardware_confirmed: false` 的单向设备语义。

需要替换：

- 38 kHz IR carrier、GPIO42/GPIO46 IR 收发。
- 红外 RMT 接收和发射实现。
- `/ir/` topic、`IR_CODE_JSON` 和红外命令命名。
- 未经实物确认的 `power_on`、`power_off`、固定色温档等命令槽。

当前设备原生 Zenoh key：

```text
humanex/cx9-ba/<device-id>/command
humanex/cx9-ba/<device-id>/info
humanex/cx9-ba/<device-id>/event
humanex/cx9-ba/<device-id>/live
```

## 当前硬件决策

一台 CoreS3 或一台 StickS3 都能完成采集、回放和 Zenoh 接入，不需要两台主控。
但主控本身没有 433.92 MHz 射频前端，需要外接：

- `M5Stack Unit RF433R`，SKU `U113`，用于接收。
- `M5Stack Unit RF433T`，SKU `U114`，用于发射。
- `M5Stack Unit Hub`，SKU `U006`，用于让 RX/TX 长期同时连接；也可用 Grove Y
  线替代，开发初期手动拔插时可以暂不购买。

RF433T 使用 Grove 黄线作为 `RF_TX`，RF433R 使用白线作为 `RF_RX`，因此可在
同一个直连 Hub 上同时工作：

| 主控 | RF_TX | RF_RX |
| --- | --- | --- |
| CoreS3 PORT.B | GPIO9 | GPIO8 |
| StickS3 PORT.CUSTOM | GPIO9 | GPIO10 |

StickS3 使用 Grove 外设时需要确认已通过 `M5.Power.setExtOutput(true)` 打开
外部 5V 输出。开发期推荐 CoreS3，最终常驻可使用更小的 StickS3。

## 下一步执行顺序

1. 准备 RF433R 和 RF433T；如需同时连接，再增加 Unit Hub 或 Grove Y 线。
2. 在 433.92 MHz 接收原装遥控器，确认每种操作均产生稳定信号。
3. 为每个动作采集至少 10 次：旋钮顺/逆时针单步、旋钮短按/长按、S1/S2/S3
   短按/长按，以及连续快速旋转。
4. 保存原始高低电平持续时间，不急于假定协议或命令含义。
5. 比较同一动作的重复样本，判断固定码、设备 ID、计数器、校验和和重复帧。
6. 用 RF433T 原样回放一个命令，20 次中成功至少 18 次。
7. 完成全部命令码表，并以实物行为命名命令。
8. 配置 StickS3 Wi-Fi 和 Zenoh router endpoint，完成设备协议腿联调。
9. 最后根据绝对/相对命令能力决定使用 `rf.remote` 还是标准 `light.*` trait。

详细采集方法和验收标准见 `RF433-capture-development-plan.md`。

## 建模边界

当前仍按单向设备处理：

- RF 发射成功只代表 `sent`，不代表挂灯真实 `applied`。
- 原装旋钮或灯体触摸控制可能使 bridge 的 shadow state 失真。
- 在没有传感器或双向协议证明前，reported 必须标记为 assumed。
- 只有协议证明能可靠到达绝对亮度、开关和色温，才考虑标准 `light.*`。

## 文件索引

- `RF433-capture-development-plan.md`：当前有效的硬件、采集、分析和开发计划。
- `RF433-protocol-reproduction.md`：开关具体参数、完整码表、解码与 RMT 复现方法。
- `CX9-B-A-monitorlight-research.md`：历史 IR 调研，已标记为被取代。
- `StickS3-only-IR-MQTT-decision.md`：历史 IR 决策，已标记为被取代。
- `CoreS3-StickS3-IR-development-plan.md`：历史 IR 计划，已标记为被取代。
- `firmware/rf433-bridge/`：当前 RF 固件开发入口。
- `firmware/archive/sticks3-ir-mqtt-remote/`：可构建的历史 IR 固件及可复用参考。
- `rf-captures/`：真实 RF 原始样本和归一化样本。
