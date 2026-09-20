# BCU-EMS Modbus 通信需求

> 本文由通信点表自动转换生成，供 Modbus 上位机/EMS 开发、联调和验收使用。
> 源文件：`二级架构BCU与EMS之间ModbusTcp通信点表-AIO二代-V1.3-20260429.xlsx`；源表最新版本：`V1.3`（2026-04-29）。

## 1. 范围与角色

- EMS 是 Modbus 主站/客户端，上位机负责发起轮询和控制写入。
- BCU/BMS 是 Modbus 从站/服务端；BCU 设备地址范围为 `0x01-0x41`。
- 本需求覆盖 Rack Signal、Rack Measure、Rack Control、Rack Diag、Rack Detail 及告警阈值参数。
- 点名以表格中的英文 `Name` 为软件唯一标识；同一张表内不得重复。

## 2. 通信与协议要求

### 2.1 Modbus TCP（主链路）

- 物理链路：以太网；协议：Modbus TCP；TCP 连接由 EMS 发起，BMS 监听。
- 默认 BMS 地址：`192.168.1.199`；默认端口：`502`。地址和端口必须可配置。
- MBAP 头：Transaction Identifier 2 字节、Protocol Identifier 2 字节（Modbus TCP 为 `0x0000`）、Length 2 字节、Unit Identifier 1 字节；随后为 PDU。
- PDU 的寄存器数据按高字节在前、低字节在后传输；寄存器地址和数量均按协议字段编码。
- 读取周期允许范围：`200 ms-1000 ms`；默认建议 `500 ms`，周期必须可配置。
- 单帧最多读取 `120` 个寄存器；超过 120 个寄存器必须自动拆分为多帧，并按地址连续性合并结果。

### 2.2 Modbus RTU（RS485 兼容链路）

- 物理链路：RS485；协议：Modbus RTU；EMS 为主站，BCU 为从站。
- 默认从站地址：`1, 2, 3, 4, 5, ...`；默认波特率：`57600`。串口号、校验位、停止位和从站地址必须可配置。
- RTU 帧使用 CRC16；CRC 低字节先传、高字节后传。
- 异常响应功能码为 `0x80 + 原功能码`，异常码至少区分：`0x01` 非法功能、`0x02` 非法数据地址、`0x03` 非法数据长度、`0x04` 读写失败。

### 2.3 支持的功能码

| Function | Meaning | Requirement |
| --- | --- | --- |
| 0x03 | Read Holding Registers | 读取保持寄存器；用于可读点和 R/W 参数 |
| 0x04 | Read Input Registers | 读取输入寄存器；用于只读测量/状态点 |
| 0x06 | Write Single Register | 写单个寄存器；仅用于表中允许单点写入的 R/W 点 |
| 0x10 | Write Multiple Registers | 写多个寄存器；支持连续参数批量下发 |

## 3. 地址分区与功能码

| Block | Register range | Supported functions | Direction |
| --- | --- | --- | --- |
| Rack Signal | 0x0001-0x0200 | 0x03, 0x04 | BCU -> EMS |
| Rack Measure | 0x0201-0x0400 | 0x03, 0x04 | BCU -> EMS |
| Rack Control | 0x0401-0x0800; 0x0900-0x0901 | 0x03, 0x04, 0x06, 0x10 | EMS -> BCU / readback |
| Rack Diag | 0x0801-0x0B00 (except 0x0900-0x0901, assigned to Control) | 0x03, 0x04 | BCU -> EMS |
| Rack Detail | 0x1000-0x6000 | 0x03, 0x04 | BCU -> EMS |
| Alarm parameters | 0x6001-0x7000 | 0x03, 0x04, 0x06, 0x10 | BAU/EMS <-> BCU |

### 3.1 点表规模

| Table | Rows in source sheet | Use |
| --- | --- | --- |
| Rack Signal | 121 | 状态、告警和 bit-field |
| Rack Measure | 64 | 汇总测量 |
| Rack Control | 11 | EMS 控制写入 |
| Rack Diag | 28 | 诊断、版本和液冷信息 |
| Rack Detail | 26 | 单体、温度和连接器明细 |
| Alarm parameters | 108 | 告警阈值读写 |

## 4. 数据模型与实现规则

1. `名称` 是上位机页面显示字段，必须保留中文；`Name` 是点位程序键；`Addr` 是十六进制 Modbus 寄存器地址。地址范围表达式（如 `0x1401-0x1600`）表示连续寄存器区间。
2. `Number` 表示点数量或预留长度；`use_number` 表示实际使用数量。数组点必须按地址顺序映射为 `name_1 ... name_N` 或表中 `{}` 约定的索引名称。
3. `Type` 决定原始寄存器解释：`u16` 无符号 16 位、`int16` 有符号 16 位；不得先转成浮点再判断符号。
4. `Unit` 为工程量单位。缩放值写在单位中（如 `0.1V`、`0.1A`、`0.1℃`），上位机应同时保存原始值和工程值，避免阈值写入时重复缩放。
5. `R` 点禁止写入；`R/W` 或 `W/R` 点允许读回和写入。写入后必须按协议响应校验，并建议立即读回确认。
6. `reserve*` 点为保留空间，默认不展示、不下发、不参与告警判断；但地址必须保留，不能压缩映射。
7. 对 bit-field 点，按 `Definition` 中的 bit 位解析；未定义 bit 必须保留原始值，不能静默丢弃。
8. 地址分区存在一个功能归属例外：`0x0900`/`0x0901` 在数值上落入 Rack Diag 区间，但点表将其定义为 Rack Control 的 BCU Unix 时间高/低 16 位同步寄存器；配置路由必须以点表的 `Block/Name` 归属优先，不能仅按地址范围判断读写权限。

## 5. 点表附录

### 5.1 Rack Signal：状态/告警读取

- 方向：BCU -> EMS；属性以 `R` 为主；支持 `0x03/0x04`。
- 具有 bit 定义的连续行属于上一条点的位域说明；实现时应生成位级子信号，但保留原始 16 位寄存器。

| 名称 | Name | Description | Definition | Data origin | Number | Addr | Unit | Attribute | Type | use_number |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 功能安全告警详细故障 | functional_safety_warn | Functional Safety Warn | 0-Normal, 1-Fault | BCU | 1 | 0x0010 |  | R | u16 |  |
|  |  |  | Bit12:functional_safety_warn.sample_chip_fault |  |  |  |  |  |  |  |
| 严重告警 | extern_critical_alarm | Extern Critical Alarm | 0-Normal, 1-Fault | BCU | 1 | 0x0012 |  | R | u16 |  |
|  |  |  | Bit0: extern_critical_alarm.Rack Vol High |  |  |  |  |  |  |  |
|  |  |  | Bit1: extern_critical_alarm.Rack Vol Low |  |  |  |  |  |  |  |
|  |  |  | Bit2:extern_critical_alarm.Cell Vol High |  |  |  |  |  |  |  |
|  |  |  | Bit3: extern_critical_alarm.Chg dsg cell Vol Low |  |  |  |  |  |  |  |
|  |  |  | Bit4: extern_critical_alarm.DischargeCurrent High |  |  |  |  |  |  |  |
|  |  |  | Bit5: extern_critical_alarm.Charge Current High |  |  |  |  |  |  |  |
|  |  |  | Bit6: extern_critical_alarm.system temp High |  |  |  |  |  |  |  |
|  |  |  | Bit7: extern_critical_alarm.system chg temp Low |  |  |  |  |  |  |  |
|  |  |  | Bit10: extern_critical_alarm.Ins Low |  |  |  |  |  |  |  |
|  |  |  | Bit12: extern_critical_alarm.Battery Rank Busbar Temp High |  |  |  |  |  |  |  |
|  |  |  | Bit13: extern_critical_alarm.Cell Vol Diff High |  |  |  |  |  |  |  |
|  |  |  | Bit14: extern_critical_alarm.Cell Temp Diff High |  |  |  |  |  |  |  |
| 告警 | extern_alarm | Extern Alarm | 0-Normal, 1-alarm | BCU | 1 | 0x0013 |  | R | u16 |  |
|  |  |  | Bit0: extern_alarm.Rack Vol High |  |  |  |  |  |  |  |
|  |  |  | Bit1: extern_alarm.Rack Vol Low |  |  |  |  |  |  |  |
|  |  |  | Bit2: extern_alarm.Cell Vol High |  |  |  |  |  |  |  |
|  |  |  | Bit3: extern_alarm.Chg dsg cell Vol Low |  |  |  |  |  |  |  |
|  |  |  | Bit4: extern_alarm.Discharge Current High |  |  |  |  |  |  |  |
|  |  |  | Bit5: extern_alarm.Charge Current High |  |  |  |  |  |  |  |
|  |  |  | Bit6: extern_alarm.system temp High |  |  |  |  |  |  |  |
|  |  |  | Bit7: extern_alarm.system chg temp Low |  |  |  |  |  |  |  |
|  |  |  | Bit10: extern_alarm.Ins Low |  |  |  |  |  |  |  |
|  |  |  | Bit12: extern_alarm.Battery Rank Busbar Temp High |  |  |  |  |  |  |  |
|  |  |  | Bit13: extern_alarm.Cell Vol Diff |  |  |  |  |  |  |  |
|  |  |  | Bit14: extern_alarm.Cell Temp Diff |  |  |  |  |  |  |  |
| 预警 | extern_warn | Extern Warning | 0-Normal, 1-preAlarm | BCU | 1 | 0x0014 |  | R | u16 |  |
|  |  |  | Bit0: extern_warn.Rack Vol High |  |  |  |  |  |  |  |
|  |  |  | Bit1: extern_warn.Rack Vol Low |  |  |  |  |  |  |  |
|  |  |  | Bit2: extern_warn.Cell Vol High |  |  |  |  |  |  |  |
|  |  |  | Bit3: extern_warn.Chg dsg cell Vol Low |  |  |  |  |  |  |  |
|  |  |  | Bit4: extern_warn.Discharge Current High |  |  |  |  |  |  |  |
|  |  |  | Bit5: extern_warn.Charge Current High |  |  |  |  |  |  |  |
|  |  |  | Bit6: extern_warn.system temp High |  |  |  |  |  |  |  |
|  |  |  | Bit7: extern_warn.system chg temp Low |  |  |  |  |  |  |  |
|  |  |  | Bit10: extern_warn.Ins Low |  |  |  |  |  |  |  |
|  |  |  | Bit12: extern_warn.Battery Rank Busbar Temp High |  |  |  |  |  |  |  |
|  |  |  | Bit13: extern_warn.Cell Vol Diff |  |  |  |  |  |  |  |
|  |  |  | Bit14: extern_warn.Cell Temp Diff |  |  |  |  |  |  |  |
| 严重告警II | extern_critical_alarm | Extern Critical Alarm | 0-Normal, 1-Fault | BCU | 1 | 0x0015 |  | R | u16 |  |
|  |  |  | Bit1:extern_critical_alarm2.pack_vol_over_high |  |  |  |  |  |  |  |
|  |  |  | Bit2:extern_critical_alarm2.pack_vol_over_low |  |  |  |  |  |  |  |
|  |  |  | Bit4:extern_critical_alarm2.pack_vol_diff_over_high |  |  |  |  |  |  |  |
|  |  |  | Bit5:extern_critical_alarm2.chg_power_over_high |  |  |  |  |  |  |  |
|  |  |  | Bit6:extern_critical_alarm2.dsg_power_over_high |  |  |  |  |  |  |  |
|  |  |  | Bit7:extern_critical_alarm2.Soh_over_low |  |  |  |  |  |  |  |
|  |  |  | Bit9:extern_critical_alarm2.System idle cell low |  |  |  |  |  |  |  |
|  |  |  | Bit10:extern_critical_alarm2.System dsg and idle temp low |  |  |  |  |  |  |  |
|  |  |  | Bit9-bit15:extern_critical_alarm2.reserve |  |  |  |  |  |  |  |
| 告警II | extern_alarm | Extern Alarm | 0-Normal, 1-Alarm | BCU | 1 | 0x0016 |  | R | u16 |  |
|  |  |  | Bit1:extern_alarm2.pack_vol_over_high |  |  |  |  |  |  |  |
|  |  |  | Bit2:extern_alarm2.pack_vol_over_low |  |  |  |  |  |  |  |
|  |  |  | Bit4:extern_alarm2.pack_vol_diff_over_high |  |  |  |  |  |  |  |
|  |  |  | Bit5:extern_alarm2.chg_power_over_high |  |  |  |  |  |  |  |
|  |  |  | Bit6:extern_alarm2.dsg_power_over_high |  |  |  |  |  |  |  |
|  |  |  | Bit9:extern_alarm2.System idle cell low |  |  |  |  |  |  |  |
|  |  |  | Bit10:extern_alarm2.System dsg and idle temp low |  |  |  |  |  |  |  |
|  |  |  | Bit9-bit15:extern_alarm2.reserve |  |  |  |  |  |  |  |
| 预警II | extern_warn | Extern Warning | 0-Normal, 1-PreAlarm | BCU | 1 | 0x0017 |  | R | u16 |  |
|  |  |  | Bit1:extern_warn2.pack_vol_over_high |  |  |  |  |  |  |  |
|  |  |  | Bit2:extern_warn2.pack_vol_over_low |  |  |  |  |  |  |  |
|  |  |  | Bit4:extern_warn2.pack_vol_diff_over_high |  |  |  |  |  |  |  |
|  |  |  | Bit5:extern_warn2.chg_power_over_high |  |  |  |  |  |  |  |
|  |  |  | Bit6:extern_warn2.dsg_power_over_high |  |  |  |  |  |  |  |
|  |  |  | Bit9:extern_warn2.System idle cell low |  |  |  |  |  |  |  |
|  |  |  | Bit10:extern_warn2.System dsg and idle temp low |  |  |  |  |  |  |  |
|  |  |  | Bit9-bit15:extern_warn2.reserve |  |  |  |  |  |  |  |
| 预上电阶段 | pre_power_stage | Pre-Powering Stage | 0:idle 1/2:start 3:success 4:failure | BCU | 1 | 0x0018 |  | R | u16 |  |
| 系统状态 | BCU_state | BCU State | 0:Idle 1:forbid charging 2:forbid Discharging 3: Standby 4: Stop | BCU | 1 | 0x0019 |  | R | u16 |  |
| 心跳帧 | reserve4 | heart beat | inc every second | BCU | 1 | 0x001A |  | R |  |  |
| 预充失败原因 | pre_chg_fail_reason | Pre-Charging Fail Reason | （reserve） | BCU | 1 | 0x001C |  | R | u16 |  |
| 充放电状态 | current_state | Current State | 0:Idle 1:Discharge 2:Charge | BCU | 1 | 0x001D |  | R | u16 |  |
| 充满放空标志 | full_or_empty_flag | Full or Empty Flag | bit0:1 full;bit1:1 empty | BCU | 1 | 0x001E |  | R | u16 |  |
| 主控对外接触器总状态 | BCU_conactor_state | Conactor State | Bit0:BCU_conactor_state.pos_relay | BCU | 1 | 0x001F |  | R | u16 |  |
|  |  |  | Bit1:BCU_conactor_state.pre_relay |  |  |  |  |  |  |  |
|  |  |  | Bit2:BCU_conactor_state.neg_relay |  |  |  |  |  |  |  |
|  |  |  | Bit3:BCU_conactor_state.isolation_switch |  |  |  |  |  |  |  |
|  |  |  | Bit4:BCU_conactor_state.fuse_switch |  |  |  |  |  |  |  |
| 平台预留位置 | reserve5 |  |  | BCU | 27 | 0x0028-0x003F |  |  |  |  |
| 热管理状态 | Heat management status | Obtain status based on thermal management strategy | 0x0: close state;0x1: Internal circulation state; 0x2: Cooling state; 0x3: Heating status; | BCU | 1 | 0x0040 |  | R | u16 |  |
| BMS故障等级 | BMS fault level | BMS fault level | 0x0: Normal; 0x1:1 level alarm; 0x2: Level 2 alarm; 0x3: Level 3 alarm | BCU | 1 | 0x0041 |  | R | u16 |  |
| 主控对外故障报警I | BCU_external_fault_alarm_I | External Alarm Of Master_I | 0-Normal, 1-Fault | BCU | 1 | 0x0042 |  | R | u16 |  |
|  |  |  | Bit0: Open circuit fault of fuse |  |  |  |  |  |  |  |
|  |  |  | Bit1: Daisy chain/ Inner CAN communication failure |  |  |  |  |  |  |  |
|  |  |  | Bit2: Current acquisition fault |  |  |  |  |  |  |  |
|  |  |  | Bit3: Circuit breaker unable to disconnect fault |  |  |  |  |  |  |  |
|  |  |  | Bit4: Cell temperature acquisition circuit malfunction |  |  |  |  |  |  |  |
|  |  |  | Bit5: Copper bar temperature acquisition circuit malfunction |  |  |  |  |  |  |  |
|  |  |  | Bit6: High voltage sampling fault |  |  |  |  |  |  |  |
|  |  |  | Bit7: Power supply voltage too high fault |  |  |  |  |  |  |  |
|  |  |  | Bit8: Low power supply voltage fault |  |  |  |  |  |  |  |
|  |  |  | Bit9: Cell voltage measurement fault |  |  |  |  |  |  |  |
|  |  |  | Bit10: Battery voltage sampling line disconnection fault |  |  |  |  |  |  |  |
|  |  |  | Bit11: Cell temperature measurement fault |  |  |  |  |  |  |  |
|  |  |  | Bit12: Minor current drift fault |  |  |  |  |  |  |  |
|  |  |  | Bit13: Current zero drift moderate fault |  |  |  |  |  |  |  |
|  |  |  | Bit14: Severe current zero drift fault |  |  |  |  |  |  |  |
| 主控对外故障报警II | BCU_external_fault_alarm_II | External Alarm Of Master_II | 0-Normal, 1-Fault | BCU | 1 | 0x0043 |  | R | u16 |  |
|  |  |  | Bit0: Thermal runaway fault |  |  |  |  |  |  |  |
|  |  |  | Bit1: Equalization circuit fault |  |  |  |  |  |  |  |
|  |  |  | Bit2: Too high equilibrium temperature fault |  |  |  |  |  |  |  |
|  |  |  | Bit10:BCU_external_fault_alarm.BCU_self_check |  |  |  |  |  |  |  |
|  |  |  | Bit11-bit15:reserve |  |  |  |  |  |  |  |
| 主控对外故障报警III | BCU_external_fault_alarm_III | External Alarm Of Master_III | 0-Normal, 1-Fault | BCU | 1 | 0x0044 |  | R | u16 |  |
|  |  |  | Bit0: Communication failure with fire protection system |  |  |  |  |  |  |  |
|  |  |  | Bit1: Communication failure with liquid cooling system |  |  |  |  |  |  |  |
|  |  |  | Bit2: Communication failure with PCS system |  |  |  |  |  |  |  |
|  |  |  | Bit3: Communication failure with EMS system |  |  |  |  |  |  |  |
|  |  |  | Bit6: First level malfunction of fire protection system |  |  |  |  |  |  |  |
|  |  |  | Bit7: Fire protection system secondary fault |  |  |  |  |  |  |  |
|  |  |  | Bit8: Level 3 fault of fire protection system |  |  |  |  |  |  |  |
|  |  |  | Bit9: Emergency stop fault |  |  |  |  |  |  |  |
|  |  |  | Bit10: Water immersion fault |  |  |  |  |  |  |  |
|  |  |  | Bit11: Surge fault |  |  |  |  |  |  |  |
|  |  |  | Bit12: Door open fault |  |  |  |  |  |  |  |
|  |  |  | Bit13: liquid cooling system stop fault |  |  |  |  |  |  |  |
|  |  |  | Bit14: liquid cooling system alarm fault |  |  |  |  |  |  |  |
|  |  |  | Bit15:reserve |  |  |  |  |  |  |  |
| 项目部门预留位置 | reserve6 | project protocol reseved | 无 | BCU | 32 | 0x0045-0x005F |  | R | U16 |  |

### 5.2 Rack Measure：汇总测量

| 名称 | Name | Description | Definition | Data origin | Number | Addr | Unit | Attribute | Type | use_number |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 单体最高电压 | max_cell_vol | Max Cell Vol |  | BCU | 1 | 0x0210 | mV | R | u16 |  |
| 单体最高电压编号 | max_cell_vol_num | Max Cell Vol Num | #number | BCU | 1 | 0x0211 |  | R | u16 |  |
| 最高单体所在模组编号 | max_vol_BMU_num | Max Vol BMU Num | #number | BCU | 1 | 0x0212 |  | R | u16 |  |
| 最高单体在模组中的单体编号 | BMU_max_vol_num | BMU Max Vol Num | #number | BCU | 1 | 0x0213 |  | R | u16 |  |
| 单体最低电压 | min_cell_vol | Min Cell Vol |  | BCU | 1 | 0x0214 | mV | R | u16 |  |
| 单体最低电压编号 | min_cell_vol_num | Min Cell Vol Num | #number | BCU | 1 | 0x0215 |  | R | u16 |  |
| 最低单体所在模组编号 | min_vol_BMU_num | Min Vol BMU Num | #number | BCU | 1 | 0x0216 |  | R | u16 |  |
| 最低单体在模组中的单体编号 | BMU_min_vol_num | BMU Min Vol Num | #number | BCU | 1 | 0x0217 |  | R | u16 |  |
| 最高最低单体压差 | max_cell_vol_diff | Max Cell Vol Diff |  | BCU | 1 | 0x0218 | mV | R | u16 |  |
| 单体平均电压 | ave_cell_vol | Ave Cell Vol |  | BCU | 1 | 0x0219 | mV | R | u16 |  |
| 电池最高温度 | max_cell_temp | Max Cell Temp |  | BCU | 1 | 0x021A | 0.1℃ | R | int16 |  |
| 电池最高温度编号 | max_cell_temp_num | MaxCellTemp Num | #number | BCU | 1 | 0x021B |  | R | u16 |  |
| 最高温度所在从控编号 | max_temp_BMU_num | MaxTempBMU Num | #number | BCU | 1 | 0x021C |  | R | u16 |  |
| 最高温度在模组中的温度编号 | BMU_max_temp_num | BMU MaxTempNum | #number | BCU | 1 | 0x021D |  | R | u16 |  |
| 电池最低温度 | min_cell_temp | Min Cell Temp |  | BCU | 1 | 0x021E | 0.1℃ | R | int16 |  |
| 电池最低温度编号 | min_cell_temp_num | MinCellTemp Num | #number | BCU | 1 | 0x021F |  | R | u16 |  |
| 最低温度所在模组编号 | min_temp_BMU_num | MinTempBMU Num | #number | BCU | 1 | 0x0220 |  | R | u16 |  |
| 最低温度在模组中的温度编号 | BMU_min_temp_num | BMU MinTempNum | #number | BCU | 1 | 0x0221 |  | R | u16 |  |
| 平均温度 | rack_ave_temp | Rack Ave Temp |  | BCU | 1 | 0x0222 | 0.1℃ | R | int16 |  |
| 最大可接受允许充电电流限值 | max_allowed_chg_cur_limit | Max Allowed Charging Current  Limit |  | BCU | 1 | 0x0225 | A（原始值×0.1） | R | u16 |  |
| 最大可接受允许放电电流限值 | max_allowed_dchg_cur_limit | Max Allowed Discharging Current  Limit |  | BCU | 1 | 0x0226 | A（原始值×0.1） | R | u16 |  |
| 显示SOC | display_SOC | Display SOC |  | BCU | 1 | 0x0227 | %（原始值×0.1） | R | u16 |  |
| SOH | SOH | SOH |  | BCU | 1 | 0x0228 | %（原始值×0.1） | R | u16 |  |
| 电池最大温差 | max_bat_temp_diff | Max Bat Temp Diff |  | BCU | 1 | 0x0229 | 0.1℃ | R | int16 |  |
| 簇内部SOC | rack_inner_SOC | InnerSoc |  | BCU | 1 | 0x022E | ‰ | R | u16 |  |
| 预充总压 | pre_chg_vol | Precharging Vol |  | BCU | 1 | 0x022F | 0.1V | R | u16 |  |
| 累计总压 | BMU_total_vol | Total accumulated pressure |  | BCU | 1 | 0x0230 | 0.1V | R | u16 |  |
| 电池总电压 | total_vol | Total Vol |  | BCU | 1 | 0x0231 | V（原始值×0.1） | R | u16 |  |
| 总电流（用于SOC积分或对外显示） | total_cur | Total Cur |  | BCU | 1 | 0x0234 | A（原始值×0.1） | R | int16 |  |
| 绝缘阻值 | Insulation resistance | Insulation resistance |  | BCU | 1 | 0x0237 | kΩ | R | u16 |  |
| 正端绝缘阻值 | positive insulation resistance | Positive Insulation |  | BCU | 1 | 0x0238 | kΩ | R | u16 |  |
| 负端绝缘阻值 | negative insulation resistance | Negative Insulation |  | BCU | 1 | 0x0239 | kΩ | R | u16 |  |
| 供电采样电压 | supply_sample_vol | Supply Voltage |  | BCU | 1 | 0x023A | mV | R | u16 |  |
| 铜排温度最高温度 | HV_Box_max_temp | HV Box Max Temp |  | BCU | 1 | 0x023B | 0.1℃ | R | int16 |  |
| 高压箱熔丝温度1 | HV_box_fuse_temp1 | HV_box_fuse_temp1 | High voltage box fuse | BCU | 1 | 0x023C | 0.1℃ | R | int16 |  |
| 高压箱OUTPUT温度2 | HV_box_OUTPUT_temp2 | HV_box_OUTPUT_temp2 | High voltage box OUTPUT | BCU | 1 | 0x023D | 0.1℃ | R | int16 |  |
| 高压箱铜排温度3 | HV_box_temp3 | HV_box_temp3 | Bronze plate temperature | BCU | 1 | 0x023E | 0.1℃ | R | int16 |  |
| 高压箱铜排温度4 | HV_box_temp4 | HV_box_temp4 | Bronze plate temperature | BCU | 1 | 0x023F | 0.1℃ | R | int16 |  |
| 当月累计充电电量 | month_accum_chg_energy | Month Charge Energy |  | BCU | 1 | 0x0246 | kwh | R | u16 |  |
| 当月累计放电电量 | month_accum_dchg_energy | Month Discharge Energy |  | BCU | 1 | 0x0247 | kwh | R | u16 |  |
| 当年累计充电电量高16位 | year_chg_energy_h16b | Year Charge Energy High 16 Bit |  | BCU | 1 | 0x0248 | kwh | R | u16 |  |
| 当年累计充电电量低16位 | year_chg_energy_l16b | Year Charge Energy Low 16 Bit |  | BCU | 1 | 0x0249 | kwh | R | u16 |  |
| 当年累计放电电量高16位 | year_dchg_energy_h16b | Year Charge Energy High 16 Bit |  | BCU | 1 | 0x024A | kwh | R | u16 |  |
| 当年累计放电电量低16位 | year_dchg_energy_l16b | Year Discharge Energy  Low 16 Bit |  | BCU | 1 | 0x024B | kwh | R | u16 |  |
| 总累计充电容量高16位 | total_chg_capcity_h16bit | Total Charge Capcity High 16 Bit |  | BCU | 1 | 0x024C | AH | R | u16 |  |
| 总累计充电容量低16 | total_chg_capcity_l16bit | Total Charge Capcity  Low 16 Bit |  | BCU | 1 | 0x024D | AH | R | u16 |  |
| 总累计放电容量高16位 | total_dchg_capcity_h16bit | Total Discharge Capcity High 16 Bit |  | BCU | 1 | 0x024E | AH | R | u16 |  |
| 总累计放电容量低16位 | total_dchg_capcity_l16bit | Total Discharge Capcity High 16 Bit |  | BCU | 1 | 0x024F | AH | R | u16 |  |
| 平台预留位置 | reserve6 |  |  | BCU | 28 | 0x0254-0x026F |  |  |  |  |
| 最大可接受充电功率 | max_allowed_chg_power | Max Allowed Charging Power |  | BCU | 1 | 0x0270 | kW（原始值×0.1） | R | u16 |  |
| 最大可接受放电功率 | max_allowed_dchg_power | Max Allowed Discharging Power |  | BCU | 1 | 0x0271 | kW（原始值×0.1） | R | u16 |  |
| 电池簇SOE | rack_soe | rack_soe |  | BCU | 1 | 0x0272 | %（原始值×0.1） | R | u16 |  |
| 从控01均衡执行状态 | Balance execution status from control 01 | Balance execution status from control 01 | 0x0: Balanced off;<br>0x1: Balance enabled;<br>Note: If any balance is enabled from the control center, it is in the enabled state | BCU | 1 | 0x0273 |  | R | u16 |  |
| 从控02均衡执行状态 | Balance execution status from control 02 | Balance execution status from control 02 | 0x0: Balanced off;<br>0x1: Balance enabled;<br>Note: If any balance is enabled from the control center, it is in the enabled state | BCU | 1 | 0x0274 |  | R | u16 |  |
| 从控03均衡执行状态 | Balance execution status from control 03 | Balance execution status from control 03 | 0x0: Balanced off;<br>0x1: Balance enabled;<br>Note: If any balance is enabled from the control center, it is in the enabled state | BCU | 1 | 0x0275 |  | R | u16 |  |
| 从控04均衡执行状态 | Balance execution status from control 04 | Balance execution status from control 04 | 0x0: Balanced off;<br>0x1: Balance enabled;<br>Note: If any balance is enabled from the control center, it is in the enabled state | BCU | 1 | 0x0276 |  | R | u16 |  |
| 从控05均衡执行状态 | Balance execution status from control 05 | Balance execution status from control 05 | 0x0: Balanced off;<br>0x1: Balance enabled;<br>Note: If any balance is enabled from the control center, it is in the enabled state | BCU | 1 | 0x0277 |  | R | u16 |  |
| 热失控电池柜编号 | Thermal runaway battery cabinet number | Thermal runaway battery cabinet number | #number | BCU | 1 | 0x027B |  | R | u16 |  |
| 热失控电池柜中电池插箱编号 | Number of battery insertion box in thermal runaway battery cabinet | Number of battery insertion box in thermal runaway battery cabinet | #number | BCU | 1 | 0x027C |  | R | u16 |  |
| BMS绝缘检测功能状态 | BMS insulation detection function status | BMS insulation detection function status | 0: Close（default）<br>1: Enable<br>Other: Invalid | BCU | 1 | 0x027D |  | R | u16 |  |
| BMS绝缘检测指令响应状态 | BMS insulation detection command response status | BMS insulation detection command response status | 0: Successfully opened;<br>1: Unable to open;<br>2: Successfully closed;<br>3: Unable to close.<br>0xffff: Invalid value | BCU | 1 | 0x027E |  | R | u16 |  |
| BMS人工关闭绝缘功能指令 | BMS manual shutdown insulation function command | BMS manual shutdown insulation function command | 0: Not manually forced to close（default）<br>1: Manual forced shutdown | BCU | 1 | 0x027F |  | R | u16 |  |
| BMS绝缘检测执行状态 | BMS insulation detection execution status | BMS insulation detection execution status | 0: Not detected;（default）<br>1: Periodic testing;<br>2: Single test;<br>3: Detection completed. | BCU | 1 | 0x0280 |  | R | u16 |  |
| 项目部门预留位置 | reserve7 | project protocol reseved | 无 | BCU | - | 0x027D-0x028F |  | R | U16 |  |

### 5.3 Rack Control：EMS 写入控制

- 地址范围为 `0x0401-0x0800`，另含时间同步寄存器 `0x0900-0x0901`；后两点虽位于 Rack Diag 数值区间内，仍按 Rack Control 实现。
- 写控制必须支持单寄存器 `0x06` 和连续多寄存器 `0x10`；具体点是否允许写入以 `Attribute` 为准。
- 脉冲型命令（如 reset、clear fault）写入有效值后不得重复周期下发；应提供人工触发、超时和结果确认。

| 名称 | Name | Description | Definition | Comment | Data origin | Number | Addr | Unit | Attribute | Type |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 启动绝缘采样 | start_insulation_sampleing | Start Insulation Sampleing | 1： Enable<br>2： Disable | BMS采集绝缘的开关控制。断网时若需要BMS采集绝缘，则使能此功能，若不需要，则不使能（上位机控制） | BCU/EMS | 1 | 0x0401 | --- | R/W | u16 |
| 下高压控制 | Upper and lower high pressure control | Upper and lower high pressure control | write value definition:<br>0x01: High voltage unde the battery cluster | 1、EMS正常情况下不控制下高压，有需求时再控制。正常BMS有故障会自行下高压 | BCU/EMS | 1 | 0x0402 | --- | R/W | u16 |
| 主控复位 | BCU_reset | Reset | 0x1：Reset ; other:No Reset | 写入0x1，可控制进行主控软件复位；写入其他值无效<br>EMS无特殊情况，不进行主控复位操作，有需要时手动操作 | BCU/EMS | 1 | 0x0403 | --- | R/W | u16 |
| 平台预留位置 | reserve1 |  | 无 |  | BCU | 28 | 0x0405-0x0464 |  | R/W |  |
| 一键清除异常事件 | clear_all_abnormal_event | clear_all_abnormal_event | 0x1：clear all fault | 写入0x1，可控制进行主控软件故障复归；写入其他值无效<br>有需求时手动操作 | BCU/EMS | 1 | 0x0465 | --- | R/W | u16 |
| 下一次充放电状态 | Next charging and discharging state during idle state | Next charging and discharging state during idle state | 0x1: The next stage is charging mode<br>0x2: The next stage is discharge mode<br>0xff: Invalid value | 该信号要与下一阶段充电时长一起下发给BMS | BCU/EMS | 1 | 0x0466 |  | R/W | u16 |
| 下一阶段充电时长 | Next stage charging duration | Next stage charging duration | 0xffff: Invalid value | EMS需要定时更新，静置时先更新一次，之后每10min更新一次。若有重新设置，需立即更新 | BCU/EMS | 1 | 0x0469 | 0.1h | R/W | u16 |
| EMS绝缘检测控制指令 | EMS insulation detection control command | EMS insulation detection control command | 0: Enable insulation detection;<br>1: Turn off insulation testing.<br>0xffff: Invalid value | 事件信号<br>EMS绝缘检测控制开关。并网后是否需要BMS采集绝缘，由此开关进行控制 | BCU/EMS | 1 | 0x046A |  | R/W | u16 |
| 项目部门预留位置 | reserve2 | project protocol reseved | 无 | 特殊项目可以使用该空间 | BCU | - | 0x046B-0x0484 |  | R/W | U16 |
| BCU时间高位 | BCU_sync_time_h16b | BCU Sync Time High 16bit | EMS每间隔10min需要进行一次校准 | 主控时间高16位（EMS用unix时间戳对主控时间进行校准） | BCU/EMS | 1 | 0x0900 |  | R/W | u16 |
| BCU时间低位 | BCU_sync_time_l16b | BCU Sync Time Low 16bit | EMS每间隔10min需要进行一次校准 | 主控时间低16位（EMS用unix时间戳对主控时间进行校准） | BCU/EMS | 1 | 0x0901 |  | R/W | u16 |

### 5.4 Rack Diag：诊断与液冷信息

| 名称 | Name | Description | Definition | Data origin | Number | Addr | Unit | Attribute | Type | use_number |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 主控软件项目编号 | BCU_software_project_num | BCU Software Project Num | #Num | BCU | 1 | 0x0801 | --- | R | u16 |  |
| 主控软件主版本号 | BCU_software_main_version_num | BCU Software Main Version Num | #Num（from 1 to 1000） | BCU | 1 | 0x0802 | --- | R | u16 |  |
| 主控软件子版本号 | BCU_software_sub_version_num | BCU Software Sub Version Num | #Num（from 1 to 1000） | BCU | 1 | 0x0803 | --- | R | u16 |  |
| 主控软件修正版本号 | BCU_software_main_revise_num | BCU Software Main Revise Num | #Num（from 1 to 1000） | BCU | 1 | 0x0804 | --- | R | u16 |  |
| 所有从控的通信故障信息低16位 | BMU_comm_fault_state_l16b | BMU Comm fault state 1 | Each bit Represents a Slave，from Slave 1 to 16（0：Normal；1：Fault） | BCU | 1 | 0x080D | Hex | R | u16 |  |
| 所有从控的概要故障信息1-16 | BMU_sum_fault_state1 | BMU fault state 1 | Each bit Represents a Slave，from Slave 1 to 16（0：Normal；1：Fault） | BCU | 1 | 0x0810 | Hex | R | u16 |  |
| 项目部门预留位置(动环数据) | reserve4 | project protocol reseved | 无 | BCU | 32 | 0x0A00-0x0BFF |  | R/W | U16 |  |
| 机组外环境温度 | External ambient temp | External ambient temp |  | BCU/EMS | 1 | 0x0A00 |  | R | int16 |  |
| 液冷机组工作模式 | Lcu running mode | Lcu running mode | 0x01: water pump circulation<br>0x02: Refrigeration mode<br>0x03:  Heating mode<br>0x04: System automatic control mode | BCU/EMS | 1 | 0x0A01 |  | R | u16 |  |
| 液冷机组出水设定温度 | Lcu water temp setting | Lcu water temp setting |  | BCU/EMS | 1 | 0x0A02 | 0.1℃ | R | int16 |  |
| 液冷机组使能开关 | Lcu enable switch | Lcu enable switch | 0x00：Off；<br>0x01：On； | BCU/EMS | 1 | 0x0A03 |  | R | u16 |  |
| 水泵运行状态 | Running state of water pump | Running state of water pump | 1 means in service, <br>0 means out of service | BCU/EMS | 1 | 0x0A04 |  | R | u16 |  |
| 压缩机运行状态 | Running status of #1 compressor | Running status of #1 compressor | 1 means in service, <br>0 means out of service | BCU/EMS | 1 | 0x0A05 |  | R | u16 |  |
| 电加热运行状态 | Running status of electric heater | Running status of electric heater | 1 means in service, <br>0 means out of service | BCU/EMS | 1 | 0x0A06 |  | R | u16 |  |
| 冷凝风机运行状态 | Running status of condensate fan | Running status of condensate fan | 1 means in service, <br>0 means out of service | BCU/EMS | 1 | 0x0A07 |  | R | u16 |  |
| 机组出水温度 | Outlet water temp | Outlet water temp |  | BCU/EMS | 1 | 0x0A08 | 0.1℃ | R | int16 |  |
| 机组回水温度 | Return water temp | Return water temp |  | BCU/EMS | 1 | 0x0A09 | 0.1℃ | R | int16 |  |
| 机组出水压力 | Water outlet pressure | Water outlet pressure |  | BCU/EMS | 1 | 0x0A0A | 0.01Bar | R | u16 |  |
| 机组回水压力 | Return water pressure | Return water pressure |  | BCU/EMS | 1 | 0x0A0B | 0.01Bar | R | u16 |  |
| 补液水泵状态 | Status of make-up water pump | Status of make-up water pump | 1: on 0 :off | BCU/EMS | 1 | 0x0A0C |  | R | u16 |  |
| 故障告警代码 | Fault alarm code | Fault alarm code | See fault code sheet | BCU/EMS | 1 | 0x0A0D |  | R | u16 |  |
| 故障等级 | Fault Level | Fault Level | 1:Stop <br>0:Alarm | BCU/EMS | 1 | 0x0A0E |  | R | u16 |  |
| 消防系统预警等级 | Fire system warning level | Fire system warning level | Fire warning level<br>0x00: Normal state<br>0x01: First level alarm<br>0x02: Level 2 alarm<br>0x03: Level 3 alarm | BCU/EMS | 1 | 0x0A40 |  | R | u16 |  |
| 消防系统设备状态标志 | Fire system equipment status signs | Fire system equipment status signs | Bit0: Sound and light alarm start flag, 0: Normal 1: Start<br>Bit1: Fire extinguishing agent capacity flag, 0: Normal 1: Insufficient<br>Bit2: Start stop button device start signal, 0: not pressed 1: pressed<br>Bit3: Start stop button device stop signal, 0: not pressed 1: pressed<br>Bit4: Fire extinguisher status, 0: Fire extinguisher not activated 1: Fire extinguisher activated | BCU/EMS | 1 | 0x0A41 |  | R | u16 |  |
| 柜内消防系统烟雾浓度数据 | Inside cabinet fire system smoke concentration data | Inside cabinet fire system  smoke concentration data | Smoke concentration, measurement range: 0-2.00dB/m<br>Increase the actual value by 100 times, calculation method: actual smoke concentration value=smoke concentration data/100<br>For example, when the smoke concentration data is 15, it is 0.15 dB/m | BCU/EMS | 1 | 0x0A56 |  | R | u16 |  |
| 柜内消防系统温度数据 | Inside cabinet fire system temp data | Inside cabinet fire system  temp data | Temperature data, measurement temperature range: -40 ℃ to+125 ℃<br>Data offset of 40, calculation method: actual temperature value=temperature data-40<br>For example, when the temperature data is 0: -40 ℃<br>When the temperature data is 40: 0 ℃<br>When the temperature data is 165: 125 ℃ | BCU/EMS | 1 | 0x0A57 |  | R | u16 |  |
| 柜内消防系统一氧化碳数据 | Inside cabinet fire system carbon monoxide data | Inside cabinet fire system carbon monoxide data | Carbon monoxide concentration, measurement range: 0-2550ppm<br>Calculation method: Actual carbon monoxide concentration value=Carbon monoxide concentration data * 10<br>For example, when the carbon monoxide concentration data is 20, it is 200ppm | BCU/EMS | 1 | 0x0A58 |  | R | u16 |  |
| 柜内消防系统状态标志 | Inside cabinet fire system status flag | Inside cabinet fire system  status flag | Bit0: Fire detector fault flag, 0: Normal 1: Fault<br>Bit1: Fire Confirmation BMS Thermal Runaway Alarm Flag, 0: Normal, 1: Alarm<br>bit2: reserved;<br>Bit3: Fire detector alarm flag, 0: Normal 1: Alarm | BCU/EMS | 1 | 0x0A59 |  | R | u16 |  |

### 5.5 Rack Detail：单体/温度/连接器明细

| 名称 | Name | Description | ASWName | Comment | Data origin | Number | Addr | Unit | Attribute | Type | use_number |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 单体电压 | single_vol_n{512} | Single Vol | BSMR_uCellVltg_A{} |  | BCU | max len = 512 | 0x1401-0x1600 | mV | R | u16 | 260 |
| 预留 | reserve4 |  |  | 52*5=260 | BCU | 512 | 0x1601-0x1800 | --- | R | u16 |  |
| 电池温度 | bat_temp_n{512} | Temp | BSMR_tCellTemp_A{} | 28*5=140 | BCU | max len = 512 | 0x1801-0x1A00 | 0.1℃ | R | int16 |  |
| 预留 | reserve5 |  |  |  | BCU | 768 | 0x1A01-0x1D00 | --- | R | u16 |  |
| PACK1内高压连接器负NTC | HV Connector neg temp_1 |  |  |  | BCU | 1 | 0x2203 | 0.1℃ | R | int16 | 连续的20个地址，可以连读后解析 |
| PACK1内MSD右NTC | MSD R temp_1 |  |  | 跨接铝牌上侧 | BCU | 1 | 0x2204 | 0.1℃ | R | int16 |  |
| PACK1内MSD左NTC | MSD L temp_1 |  |  | 跨接铝牌下侧 | BCU | 1 | 0x2205 | 0.1℃ | R | int16 |  |
| PACK1内高压连接器正NTC | HV Connector pos temp_1 |  |  |  | BCU | 1 | 0x2206 | 0.1℃ | R | int16 |  |
| PACK2内高压连接器负NTC | HV Connector neg temp_2 |  |  |  | BCU | 1 | 0x2207 | 0.1℃ | R | int16 |  |
| PACK2内MSD右NTC | MSD R temp_2 |  |  | 跨接铝牌上侧 | BCU | 1 | 0x2208 | 0.1℃ | R | int16 |  |
| PACK2内MSD左NTC | MSD L temp_2 |  |  | 跨接铝牌下侧 | BCU | 1 | 0x2209 | 0.1℃ | R | int16 |  |
| PACK2内高压连接器正NTC | HV Connector pos temp_2 |  |  |  | BCU | 1 | 0x220A | 0.1℃ | R | int16 |  |
| PACK3内高压连接器负NTC | HV Connector neg temp_3 |  |  |  | BCU | 1 | 0x220B | 0.1℃ | R | int16 |  |
| PACK3内MSD右NTC | MSD R temp_3 |  |  | 跨接铝牌上侧 | BCU | 1 | 0x220C | 0.1℃ | R | int16 |  |
| PACK3内MSD左NTC | MSD L temp_3 |  |  | 跨接铝牌下侧 | BCU | 1 | 0x220D | 0.1℃ | R | int16 |  |
| PACK3内高压连接器正NTC | HV Connector pos temp_3 |  |  |  | BCU | 1 | 0x220E | 0.1℃ | R | int16 |  |
| PACK4内高压连接器负NTC | HV Connector neg temp_4 |  |  |  | BCU | 1 | 0x220F | 0.1℃ | R | int16 |  |
| PACK4内MSD右NTC | MSD R temp_4 |  |  | 跨接铝牌上侧 | BCU | 1 | 0x2210 | 0.1℃ | R | int16 |  |
| PACK4内MSD左NTC | MSD L temp_4 |  |  | 跨接铝牌下侧 | BCU | 1 | 0x2211 | 0.1℃ | R | int16 |  |
| PACK4内高压连接器正NTC | HV Connector pos temp_4 |  |  |  | BCU | 1 | 0x2212 | 0.1℃ | R | int16 |  |
| PACK5内高压连接器负NTC | HV Connector neg temp_5 |  |  |  | BCU | 1 | 0x2213 | 0.1℃ | R | int16 |  |
| PACK5内MSD右NTC | MSD R temp_5 |  |  | 跨接铝牌上侧 | BCU | 1 | 0x2214 | 0.1℃ | R | int16 |  |
| PACK5内MSD左NTC | MSD L temp_5 |  |  | 跨接铝牌下侧 | BCU | 1 | 0x2215 | 0.1℃ | R | int16 |  |
| PACK5内高压连接器正NTC | HV Connector pos temp_5 |  |  |  | BCU | 1 | 0x2216 | 0.1℃ | R | int16 |  |
| 平台预留位置 | reserve14 |  |  |  | BCU | 256 | 0x3601-0x3700 |  |  |  |  |
| 项目部门预留位置 | reserve15 | project protocol reseved |  | 该部分不允许项目部自定义 | BCU | 256 | 0x3701-0x3800 | X | X | X |  |

### 5.6 Rack Alarm Parameter Set & Read：告警阈值

- 该区域为保持寄存器参数；Rack 地址从 `2` 到 `N+1`，每个 Rack 独立。
- 支持 `0x03/0x04` 读取和 `0x06/0x10` 写入；写入必须具备权限控制、范围校验、写后读回和审计记录。

| 名称 | Name | Description | Definition | Comment | Data origin | Number | Addr | Unit | Attribute | Type | use_number |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Rack单体过压一级报警门限 | lv1_hi_clu_sinvol_alm | Rack Cell Over Voltage Warning |  |  | BAU, BCU[1] | 1 | 0x6001 | mv | W/R | u16 |  |
| Rack单体过压一级恢复门限 | lv1_hi_clu_sinvol_alm_reco | Rack Cell Over Voltage Warning Recover |  |  | BAU, BCU[1] | 1 | 0x6002 | mv | W/R | u16 |  |
| Rack单体欠压一级报警门限 | lv1_low_clu_sinvol_alm | Rack Cell Under Voltage Warning |  |  | BAU, BCU[1] | 1 | 0x6003 | mv | W/R | u16 |  |
| Rack单体欠压一级恢复门限 | lv1_low_clu_sinvol_alm_reco | Rack Cell Under Voltage Warning Recover |  |  | BAU, BCU[1] | 1 | 0x6004 | mv | W/R | u16 |  |
| Rack总电压过压一级报警门限 | lv1_hi_total_vol_alm | Rack System Over Voltage Warning |  |  | BAU, BCU[1] | 1 | 0x6005 | 0.1V | W/R | u16 |  |
| Rack总电压过压一级恢复门限 | lv1_hi_total_vol_alm_reco | Rack System Over Voltage Warning Recover |  |  | BAU, BCU[1] | 1 | 0x6006 | 0.1V | W/R | u16 |  |
| Rack总电压欠压一级报警门限 | lv1_low_total_vol_alm | Rack System Under Voltage Warning |  |  | BAU, BCU[1] | 1 | 0x6007 | 0.1V | W/R | u16 |  |
| Rack总电压欠压一级恢复门限 | lv1_low_total_vol_alm_reco | Rack System Under Voltage Warning Recover |  |  | BAU, BCU[1] | 1 | 0x6008 | 0.1V | W/R | u16 |  |
| Rack充电过流一级报警门限 | lv1_chg_cur_hi_alm | Rack Charge Over Current Warning |  |  | BAU, BCU[1] | 1 | 0x6009 | 0.1A | W/R | u16 |  |
| Rack充电过流一级恢复门限 | lv1_hi_chg_cur_alm_reco | Rack Charge Over Current Warning Recover |  |  | BAU, BCU[1] | 1 | 0x600A | 0.1A | W/R | u16 |  |
| Rack放电过流一级报警门限 | lv1_dchg_cur_hi_alm | Rack Discharge Over Current Warning |  |  | BAU, BCU[1] | 1 | 0x600B | 0.1A | W/R | u16 |  |
| Rack放电过流一级恢复门限 | lv1_hi_dchg_cur_alm_reco | Rack Discharge Over Current Warning Recover |  |  | BAU, BCU[1] | 1 | 0x600C | 0.1A | W/R | u16 |  |
| Rack充电温度过高一级报警门限 | lv1_clu_chg_temp_hi_alm | Rack Charge Over Temp Warning |  |  | BAU, BCU[1] | 1 | 0x600D | 0.1℃ | W/R | int16 |  |
| Rack充电温度过高一级恢复门限 | lv1_hi_clu_chg_temp_alm_reco | Rack Charge Over Temp Warning Recover |  |  | BAU, BCU[1] | 1 | 0x600E | 0.1℃ | W/R | int16 |  |
| Rack充电温度过低一级报警门限 | lv1_clu_chg_temp_low_alm | Rack Charge Under Temp Warning |  |  | BAU, BCU[1] | 1 | 0x600F | 0.1℃ | W/R | int16 |  |
| Rack充电温度过低一级恢复门限 | lv1_low_clu_chg_temp_alm_reco | Rack Charge Under Temp Warning Recover |  |  | BAU, BCU[1] | 1 | 0x6010 | 0.1℃ | W/R | int16 |  |
| RackSOC过低一级报警门限 | lv1_SOC_low_warn | Rack SOC Lower Warning |  |  | BAU, BCU[1] | 1 | 0x6011 | - | W/R | u16 |  |
| RackSOC过低一级恢复门限 | lv1_low_SOC_alm_reco | Rack SOC Lower Warning Recover |  |  | BAU, BCU[1] | 1 | 0x6012 | - | W/R | u16 |  |
| Rack极柱温度过高一级报警门限 | lv1_polarity_temp_hi_alm | Rack Pole Over Temp Warning |  |  | BAU, BCU[1] | 1 | 0x6013 | 0.1℃ | W/R | int16 |  |
| Rack极柱温度过高一级恢复门限 | lv1_hi_polarity_temp_alm_reco | Rack Pole Over Temp Warning Recover |  |  | BAU, BCU[1] | 1 | 0x6014 | 0.1℃ | W/R | int16 |  |
| Rack绝缘失效一级报警门限 | lv1_ir_low_warn | Rack Insulation Failure Warning |  |  | BAU, BCU[1] | 1 | 0x6015 | Ω/V | W/R | u16 |  |
| Rack绝缘失效一级恢复门限 | lv1_low_ir_alm_reco | Rack Insulation Failure Warning Recover |  |  | BAU, BCU[1] | 1 | 0x6016 | Ω/V | W/R | u16 |  |
| Rack单体压差过高一级报警门限 | lv1_hi_clu_sinvol_diff_alm | Rack Cell Vol Diff Over Big Warning |  |  | BAU, BCU[1] | 1 | 0x6017 | mv | W/R | u16 |  |
| Rack单体压差过高一级恢复门限 | lv1_hi_clu_sinvol_diff_alm_reco | Rack Cell Vol Diff Over Big Warning Recover |  |  | BAU, BCU[1] | 1 | 0x6018 | mv | W/R | u16 |  |
| Rack总电压压差过高一级报警门限 | lv1_hi_vol_diff_alm | Rack Total Vol Diff Over Big Warning |  |  | BAU, BCU[1] | 1 | 0x6019 | 0.1V | W/R | u16 |  |
| Rack总电压压差过高一级恢复门限 | lv1_hi_tot_vol_diff_alm_reco | Rack Total Vol Diff Over Big Warning Recover |  |  | BAU, BCU[1] | 1 | 0x601A | 0.1V | W/R | u16 |  |
| Rack放电过温一级报警门限 | lv1_hi_clu_temp_alm | Rack Discharge Over Temp Warning |  |  | BAU, BCU[1] | 1 | 0x601B | 0.1℃ | W/R | int16 |  |
| Rack放电过温一级恢复门限 | lv1_hi_clu_temp_alm_reco | Rack Discharge Over Temp Warning Recover |  |  | BAU, BCU[1] | 1 | 0x601C | 0.1℃ | W/R | int16 |  |
| Rack放电欠温一级报警门限 | lv1_low_clu_temp_alm | Rack Discharge Under Temp Warning |  |  | BAU, BCU[1] | 1 | 0x601D | 0.1℃ | W/R | int16 |  |
| Rack放电欠温一级恢复门限 | lv1_low_clu_temp_alm_reco | Rack Discharge Under Temp Warning Recover |  |  | BAU, BCU[1] | 1 | 0x601E | 0.1℃ | W/R | int16 |  |
| Rack温差过高一级报警门限 | lv1_hi_bat_temp_diff_alm | Rack Bat Temp Diff Over Big Warning |  |  | BAU, BCU[1] | 1 | 0x601F | 0.1℃ | W/R | u16 |  |
| Rack温差过高一级恢复门限 | lv1_hi_bat_temp_diff_alm_reco | Rack Bat Temp Diff Over Big Warning Recover |  |  | BAU, BCU[1] | 1 | 0x6020 | 0.1℃ | W/R | u16 |  |
| Rack 高压箱连接器温度过高一级报警门限 | lv1_hi_hvbox_temp_alm | Rack HVB Temp High warning |  |  | BAU, BCU[1] | 1 | 0x6021 | 0.1℃ | W/R | u16 |  |
| Rack 高压箱连接器温度过高一级恢复门限 | lv1_hi_hvbox_temp_alm_reco | Rack HVB Temp High warning Recover |  |  | BAU, BCU[1] | 1 | 0x6022 | 0.1℃ | W/R | u16 |  |
| Rack单体过压二级报警门限 | lv2_hi_clu_sinvol_alm | Rack Cell Over Voltage Alarm |  |  | BAU, BCU[1] | 1 | 0x6023 | mv | W/R | u16 |  |
| Rack单体过压二级恢复门限 | lv2_hi_clu_sinvol_alm_reco | Rack Cell Over Voltage Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x6024 | mv | W/R | u16 |  |
| Rack单体欠压二级报警门限 | lv2_low_clu_sinvol_alm | Rack Cell Under Voltage Alarm |  |  | BAU, BCU[1] | 1 | 0x6025 | mv | W/R | u16 |  |
| Rack单体欠压二级恢复门限 | lv2_low_clu_sinvol_alm_reco | Rack Cell Under Voltage Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x6026 | mv | W/R | u16 |  |
| Rack总电压过压二级报警门限 | lv2_hi_total_vol_alm | Rack System Over Voltage Alarm |  |  | BAU, BCU[1] | 1 | 0x6027 | 0.1V | W/R | u16 |  |
| Rack总电压过压二级恢复门限 | lv2_hi_total_vol_alm_reco | Rack System Over Voltage Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x6028 | 0.1V | W/R | u16 |  |
| Rack总电压欠压二级报警门限 | lv2_low_total_vol_alm | Rack System Under Voltage Alarm |  |  | BAU, BCU[1] | 1 | 0x6029 | 0.1V | W/R | u16 |  |
| Rack总电压欠压二级恢复门限 | lv2_low_total_vol_alm_reco | Rack System Under Voltage Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x602A | 0.1V | W/R | u16 |  |
| Rack充电过流二级报警门限 | lv2_chg_cur_hi_alm | Rack Charge Over Current Alarm |  |  | BAU, BCU[1] | 1 | 0x602B | 0.1A | W/R | u16 |  |
| Rack充电过流二级恢复门限 | lv2_hi_chg_cur_alm_reco | Rack Charge Over Current Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x602C | 0.1A | W/R | u16 |  |
| Rack放电过流二级报警门限 | lv2_dchg_cur_hi_alm | Rack Discharge Over Current Alarm |  |  | BAU, BCU[1] | 1 | 0x602D | 0.1A | W/R | u16 |  |
| Rack放电过流二级恢复门限 | lv2_hi_dchg_cur_alm_reco | Rack Discharge Over Current Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x602E | 0.1A | W/R | u16 |  |
| Rack充电温度过高二级报警门限 | lv2_clu_chg_temp_hi_alm | Rack Charge Over Temp Alarm |  |  | BAU, BCU[1] | 1 | 0x602F | 0.1℃ | W/R | int16 |  |
| Rack充电温度过高二级恢复门限 | lv2_hi_clu_chg_temp_alm_reco | Rack Charge Over Temp Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x6030 | 0.1℃ | W/R | int16 |  |
| Rack充电温度过低二级报警门限 | lv2_clu_chg_temp_low_alm | Rack Charge Under Temp Alarm |  |  | BAU, BCU[1] | 1 | 0x6031 | 0.1℃ | W/R | int16 |  |
| Rack充电温度过低二级恢复门限 | lv2_low_clu_chg_temp_alm_reco | Rack Charge Under Temp Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x6032 | 0.1℃ | W/R | int16 |  |
| RackSOC过低二级报警门限 | lv2_SOC_low_alm | Rack SOC Lower Alarm |  |  | BAU, BCU[1] | 1 | 0x6033 | 0.01 | W/R | u16 |  |
| RackSOC过低二级恢复门限 | lv2_low_SOC_alm_reco | Rack SOC Lower Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x6034 | 0.01 | W/R | u16 |  |
| Rack极柱温度过高二级报警门限 | lv2_polarity_temp_hi_alm | Rack Pole Over Temp Alarm |  |  | BAU, BCU[1] | 1 | 0x6035 | 0.1℃ | W/R | int16 |  |
| Rack极柱温度过高二级恢复门限 | lv2_hi_polarity_temp_alm_reco | Rack Pole Over Temp Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x6036 | 0.1℃ | W/R | int16 |  |
| Rack绝缘失效二级报警门限 | lv2_ir_low_alm | Rack Insulation Failure Alarm |  |  | BAU, BCU[1] | 1 | 0x6037 | Ω/V | W/R | u16 |  |
| Rack绝缘失效二级恢复门限 | lv2_low_ir_alm_reco | Rack Insulation Failure Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x6038 | Ω/V | W/R | u16 |  |
| Rack单体压差过高二级报警门限 | lv2_hi_clu_sinvol_diff_alm | Rack Cell Vol Diff Over Big Alarm |  |  | BAU, BCU[1] | 1 | 0x6039 | mv | W/R | u16 |  |
| Rack单体压差过高二级恢复门限 | lv2_hi_clu_sinvol_diff_alm_reco | Rack Cell Vol Diff Over Big Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x603A | mv | W/R | u16 |  |
| Rack总电压压差过高二级报警门限 | lv2_hi_vol_diff_alm | Rack Total Vol Diff Over Big Alarm |  |  | BAU, BCU[1] | 1 | 0x603B | 0.1V | W/R | u16 |  |
| Rack总电压压差过高二级恢复门限 | lv2_hi_tot_vol_diff_alm_reco | Rack Total Vol Diff Over Big Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x603C | 0.1V | W/R | u16 |  |
| Rack放电过温二级报警门限 | lv2_hi_clu_temp_alm | Rack Discharge Over Temp Alarm |  |  | BAU, BCU[1] | 1 | 0x603D | 0.1℃ | W/R | int16 |  |
| Rack放电过温二级恢复门限 | lv2_hi_clu_temp_alm_reco | Rack Discharge Over Temp Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x603E | 0.1℃ | W/R | int16 |  |
| Rack放电欠温二级报警门限 | lv2_low_clu_temp_alm | Rack Discharge Under Temp Alarm |  |  | BAU, BCU[1] | 1 | 0x603F | 0.1℃ | W/R | int16 |  |
| Rack放电欠温二级恢复门限 | lv2_low_clu_temp_alm_reco | Rack Discharge Under Temp Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x6040 | 0.1℃ | W/R | int16 |  |
| Rack温差过高二级报警门限 | lv2_hi_bat_temp_diff_alm | Rack Bat Temp Diff Over Big Alarm |  |  | BAU, BCU[1] | 1 | 0x6041 | 0.1℃ | W/R | u16 |  |
| Rack温差过高二级恢复门限 | lv2_hi_bat_temp_diff_alm_reco | Rack Bat Temp Diff Over Big Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x6042 | 0.1℃ | W/R | u16 |  |
| Rack 高压箱连接器温度过高二级报警门限 | lv2_hi_hvbox_temp_alm | Rack HVB Temp High Alarm |  |  | BAU, BCU[1] | 1 | 0x6043 | 0.1℃ | W/R | u16 |  |
| Rack 高压箱连接器温度过高二级恢复门限 | lv2_hi_hvbox_temp_alm_reco | Rack HVB Temp High Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x6044 | 0.1℃ | W/R | u16 |  |
| Rack单体过压三级报警门限 | lv3_hi_clu_sinvol_alm | Rack Cell Over Voltage Critical Alarm |  |  | BAU, BCU[1] | 1 | 0x6045 | mv | W/R | u16 |  |
| Rack单体过压三级恢复门限 | lv3_hi_clu_sinvol_alm_reco | Rack Cell Over Voltage Critical Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x6046 | mv | W/R | u16 |  |
| Rack单体欠压三级报警门限 | lv3_low_clu_sinvol_alm | Rack Cell Under Voltage Critical Alarm |  |  | BAU, BCU[1] | 1 | 0x6047 | mv | W/R | u16 |  |
| Rack单体欠压三级恢复门限 | lv3_low_clu_sinvol_alm_reco | Rack Cell Under Voltage Critical Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x6048 | mv | W/R | u16 |  |
| Rack总电压过压三级报警门限 | lv3_hi_total_vol_alm | Rack System Over Voltage Critical Alarm |  |  | BAU, BCU[1] | 1 | 0x6049 | 0.1V | W/R | u16 |  |
| Rack总电压过压三级恢复门限 | lv3_hi_total_vol_alm_reco | Rack System Over Voltage Critical Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x604A | 0.1V | W/R | u16 |  |
| Rack总电压欠压三级报警门限 | lv3_low_total_vol_alm | Rack System Under Voltage Critical Alarm |  |  | BAU, BCU[1] | 1 | 0x604B | 0.1V | W/R | u16 |  |
| Rack总电压欠压三级恢复门限 | lv3_low_total_vol_alm_reco | Rack System Under Voltage Critical Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x604C | 0.1V | W/R | u16 |  |
| Rack充电过流三级报警门限 | lv3_chg_cur_hi_alm | Rack Charge Over Current Critical Alarm |  |  | BAU, BCU[1] | 1 | 0x604D | 0.1A | W/R | u16 |  |
| Rack充电过流三级恢复门限 | lv3_hi_chg_cur_alm_reco | Rack Charge Over Current Critical Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x604E | 0.1A | W/R | u16 |  |
| Rack放电过流三级报警门限 | lv3_dchg_cur_hi_alm | Rack Discharge Over Current Critical Alarm |  |  | BAU, BCU[1] | 1 | 0x604F | 0.1A | W/R | u16 |  |
| Rack放电过流三级恢复门限 | lv3_hi_dchg_cur_alm_reco | Rack Discharge Over Current Critical Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x6050 | 0.1A | W/R | u16 |  |
| Rack充电温度过高三级报警门限 | lv3_clu_chg_temp_hi_alm | Rack Charge Over Temp Critical Alarm |  |  | BAU, BCU[1] | 1 | 0x6051 | 0.1℃ | W/R | int16 |  |
| Rack充电温度过高三级恢复门限 | lv3_hi_clu_chg_temp_alm_reco | Rack Charge Over Temp Critical Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x6052 | 0.1℃ | W/R | int16 |  |
| Rack充电温度过低三级报警门限 | lv3_clu_chg_temp_low_alm | Rack Charge Under Temp Critical Alarm |  |  | BAU, BCU[1] | 1 | 0x6053 | 0.1℃ | W/R | int16 |  |
| Rack充电温度过低三级恢复门限 | lv3_low_clu_chg_temp_alm_reco | Rack Charge Under Temp Critical Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x6054 | 0.1℃ | W/R | int16 |  |
| RackSOC过低三级报警门限 | lv3_SOC_low_alm | Rack SOC Lower Critical Alarm |  |  | BAU, BCU[1] | 1 | 0x6055 | % | W/R | u16 |  |
| RackSOC过低三级恢复门限 | lv3_low_SOC_alm_reco | Rack SOC Lower Critical Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x6056 | % | W/R | u16 |  |
| Rack极柱温度过高三级报警门限 | lv3_polarity_temp_hi_alm | Rack Pole Over Temp Critical Alarm |  |  | BAU, BCU[1] | 1 | 0x6057 | 0.1℃ | W/R | int16 |  |
| Rack极柱温度过高三级恢复门限 | lv3_hi_polarity_temp_alm_reco | Rack Pole Over Temp Critical Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x6058 | 0.1℃ | W/R | int16 |  |
| Rack绝缘失效三级报警门限 | lv3_ir_low_alm | Rack Insulation Failure Critical Alarm |  |  | BAU, BCU[1] | 1 | 0x6059 | Ω/V | W/R | u16 |  |
| Rack绝缘失效三级恢复门限 | lv3_low_ir_alm_reco | Rack Insulation Failure Critical Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x605A | Ω/V | W/R | u16 |  |
| Rack单体压差过高三级报警门限 | lv3_hi_clu_sinvol_diff_alm | Rack Cell Vol Diff Over Big Critical Alarm |  |  | BAU, BCU[1] | 1 | 0x605B | mv | W/R | u16 |  |
| Rack单体压差过高三级恢复门限 | lv3_hi_clu_sinvol_diff_alm_reco | Rack Cell Vol Diff Over Big Critical Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x605C | mv | W/R | int16 |  |
| Rack总电压压差过高三级报警门限 | lv3_hi_vol_diff_alm | Rack Total Vol Diff Over Big Critical Alarm |  |  | BAU, BCU[1] | 1 | 0x605D | 0.1V | W/R | int16 |  |
| Rack总电压压差过高三级恢复门限 | lv3_hi_tot_vol_diff_alm_reco | Rack Total Vol Diff Over Big Critical Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x605E | 0.1V | W/R | int16 |  |
| Rack放电过温三级报警门限 | lv3_hi_clu_temp_alm | Rack Discharge Over Temp Critical Alarm |  |  | BAU, BCU[1] | 1 | 0x605F | 0.1℃ | W/R | int16 |  |
| Rack放电过温三级恢复门限 | lv3_hi_clu_temp_alm_reco | Rack Discharge Over Temp Critical Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x6060 | 0.1℃ | W/R | u16 |  |
| Rack放电欠温三级报警门限 | lv3_low_clu_temp_alm | Rack Discharge Under Temp Critical Alarm |  |  | BAU, BCU[1] | 1 | 0x6061 | 0.1℃ | W/R | u16 |  |
| Rack放电欠温三级恢复门限 | lv3_low_clu_temp_alm_reco | Rack Discharge Under Temp Critical Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x6062 | 0.1℃ | W/R | u16 |  |
| Rack温差过高三级报警门限 | lv3_hi_bat_temp_diff_alm | Rack Bat Temp Diff Over Big Critical Alarm |  |  | BAU, BCU[1] | 1 | 0x6063 | 0.1℃ | W/R | u16 |  |
| Rack温差过高三级恢复门限 | lv3_hi_bat_temp_diff_alm_reco | Rack Bat Temp Diff Over Big Critical Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x6064 | 0.1℃ | W/R | u16 |  |
| Rack 高压箱连接器温度过高三级报警门限 | lv3_hi_hvbox_temp_alm | Rack HVB Temp High Critical Alarm |  |  | BAU, BCU[1] | 1 | 0x6065 | 0.1℃ | W/R | u16 |  |
| Rack 高压箱连接器温度过高三级恢复门限 | lv3_hi_hvbox_temp_alm_reco | Rack HVB Temp High Critical Alarm Recover |  |  | BAU, BCU[1] | 1 | 0x6066 | 0.1℃ | W/R | u16 |  |
| Rack 模组压差过高告警三级阈值 | lv3_module_vol_differ_hi_alm_throld |  |  |  | BAU, BCU[1] |  | 0x6067 | 0.1V | W/R | u16 |  |
| Rack 模组压差过高告警二级阈值 | lv2_module_vol_differ_hi_alm_throld |  |  |  | BAU, BCU[1] |  | 0x6068 | 0.1V | W/R | u16 |  |
| Rack 模组压差过高告警一级阈值 | lv1_module_vol_differ_hi_alm_throld |  |  |  | BAU, BCU[1] |  | 0x6069 | 0.1V | W/R | u16 |  |
| Rack 模组压差过高告警三级恢复阈值 | lv3_module_vol_differ_hi_alm_reco |  |  |  | BAU, BCU[1] |  | 0x606A | 0.1V | W/R | u16 |  |
| Rack 模组压差过高告警二级恢复阈值 | lv2_module_vol_differ_hi_alm_reco |  |  |  | BAU, BCU[1] |  | 0x606B | 0.1V | W/R | u16 |  |
| Rack 模组压差过高告警一级恢复阈值 | lv1_module_vol_differ_hi_alm_reco |  |  |  | BAU, BCU[1] |  | 0x606C | 0.1V | W/R | u16 |  |

## 6. 上位机功能需求

- **连接管理**：支持 TCP Client 和 RS485 RTU 两种模式；显示连接状态、设备地址、重连次数和最近错误。
- **轮询调度**：按地址块配置轮询周期；自动拆分超过 120 寄存器的请求；记录每次请求的事务号、功能码、起始地址、数量、耗时和响应状态。
- **数据展示**：同时显示原始寄存器、工程值、单位、质量码、时间戳和数据来源；支持 bit-field 展开。
- **控制写入**：对 R/W 点提供值域/枚举校验、二次确认、权限控制、写入结果和读回结果；禁止对 R 点显示写入口。
- **告警**：通信超时、CRC/协议异常、非法功能、非法地址、非法数据、设备异常响应都要产生可检索事件；业务告警沿用 Rack Signal/Diag 位定义。
- **数据记录**：至少支持 CSV 或 SQLite 记录；记录应包含设备地址、点名、寄存器地址、原始值、工程值、质量码和采集时间。
- **配置导入**：点表应从版本化配置加载，不把地址和缩放因子硬编码在界面逻辑中。

## 7. 验收测试要求

1. TCP 连接 BMS `192.168.1.199:502` 成功，Unit ID 在 `0x01-0x41` 范围内可配置。
2. 使用 `0x03` 和 `0x04` 分别读取各地址块，验证地址、数量、字节序、类型和单位换算。
3. 对超过 120 寄存器的 Rack Detail 数组验证自动分帧、无重复、无丢失且地址连续。
4. 对 Rack Control 和告警参数使用 `0x06`、`0x10` 写入，验证 R 点拒绝写入、R/W 点响应正确、写后读回一致。
5. 注入 `0x01/0x02/0x03/0x04` 异常响应，验证错误分类、重试策略和事件记录。
6. 验证 RTU CRC16 低字节在前，以及超时、断线、重连和串口参数切换。
7. 对 bit-field 信号逐位构造测试值，验证位级告警与原始寄存器一致。

## 8. 数据质量与待确认项

- 源 Excel 的部分中文单元格读取结果包含替换字符，属于源文件编码/字体兼容问题；本需求文件以英文 `Name`、地址、单位、属性、类型和可读英文描述为准，中文业务描述需在正式基线发布前由协议负责人复核。
- `Rack Signal` 中部分位域说明分布在主点下一行，需在配置导入器中实现父项继承。
- `Rack Control` 中 `BCU_sync_time_h16b` 等时间同步点存在跨寄存器语义，低 16 位/高 16 位组合方式和写入顺序应由 BCU 固件接口负责人确认。
- 表内温度单位存在编码异常（如 `0.1��`）；需求实现应按英文/数值语义复核为摄氏度后再固化，禁止直接依赖乱码文本。
- 地址分区表与个别点的实际地址需在联调阶段做范围一致性校验；发现越界时以协议负责人签发的修订版点表为准。

---

*Generated on 2026-09-11 from the attached workbook.*
