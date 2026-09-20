from __future__ import annotations

from pathlib import Path
from datetime import datetime
import re

import openpyxl


ROOT = Path(__file__).resolve().parent
SOURCE = next(p for p in ROOT.glob("*.xlsx") if not p.name.startswith("~$"))
OUTPUT = ROOT / "bcu_ems_modbus_requirements.md"


def clean(value) -> str:
    if value is None:
        return ""
    text = str(value).replace("\r\n", "\n").replace("\r", "\n").strip()
    return text


def md(value) -> str:
    text = clean(value)
    if "\ufffd" in text:
        return "[source text encoding error]"
    return text.replace("|", "\\|").replace("\n", "<br>")


def nonempty_rows(ws):
    for row in ws.iter_rows(values_only=True):
        values = [clean(v) for v in row]
        if any(values):
            yield values


def rows_from_table(ws, header_row: int):
    rows = list(ws.iter_rows(values_only=True))
    headers = [clean(v) for v in rows[header_row - 1]]
    result = []
    for raw in rows[header_row:]:
        values = [clean(v) for v in raw]
        if any(values):
            result.append(dict(zip(headers, values)))
    return headers, result


def table(headers, rows, fields=None):
    fields = fields or headers
    out = ["| " + " | ".join(md(h) for h in fields) + " |", "| " + " | ".join("---" for _ in fields) + " |"]
    for row in rows:
        out.append("| " + " | ".join(md(row.get(f, "")) for f in fields) + " |")
    return "\n".join(out)


def first_value(ws, row_no, col_no):
    return clean(ws.cell(row=row_no, column=col_no).value)


wb = openpyxl.load_workbook(SOURCE, read_only=True, data_only=True)
version = wb["Version"]
version_rows = list(nonempty_rows(version))
version_history = version_rows[2:]
latest = version_history[-1] if version_history else ["", "", "", "", ""]


def display_date(value) -> str:
    if isinstance(value, datetime):
        return value.strftime("%Y-%m-%d")
    text = clean(value)
    match = re.match(r"^(\d{4}-\d{2}-\d{2})(?:\s+00:00:00)?$", text)
    return match.group(1) if match else text

define = wb["Define"]
signal_headers, signals = rows_from_table(wb["Rack Signal"], 2)
detail_headers, details = rows_from_table(wb["Rack Detail"], 2)
measure_headers, measures = rows_from_table(wb["Rack Measure"], 2)
control_headers, controls = rows_from_table(wb["Rack Control"], 2)
diag_headers, diags = rows_from_table(wb["Rack Diag"], 2)
alarm_headers, alarms = rows_from_table(wb["Rack_Alarm Parameter Set & Read"], 4)

lines = []
lines += [
    "# BCU-EMS Modbus 通信需求",
    "",
    "> 本文由通信点表自动转换生成，供 Modbus 上位机/EMS 开发、联调和验收使用。",
    "> 源文件：`%s`；源表最新版本：`%s`（%s）。" % (SOURCE.name, md(latest[1] if len(latest) > 1 else ""), md(display_date(latest[0] if latest else ""))),
    "",
    "## 1. 范围与角色",
    "",
    "- EMS 是 Modbus 主站/客户端，上位机负责发起轮询和控制写入。",
    "- BCU/BMS 是 Modbus 从站/服务端；BCU 设备地址范围为 `0x01-0x41`。",
    "- 本需求覆盖 Rack Signal、Rack Measure、Rack Control、Rack Diag、Rack Detail 及告警阈值参数。",
    "- 点名以表格中的英文 `Name` 为软件唯一标识；同一张表内不得重复。",
    "",
    "## 2. 通信与协议要求",
    "",
    "### 2.1 Modbus TCP（主链路）",
    "",
    "- 物理链路：以太网；协议：Modbus TCP；TCP 连接由 EMS 发起，BMS 监听。",
    "- 默认 BMS 地址：`192.168.1.199`；默认端口：`502`。地址和端口必须可配置。",
    "- MBAP 头：Transaction Identifier 2 字节、Protocol Identifier 2 字节（Modbus TCP 为 `0x0000`）、Length 2 字节、Unit Identifier 1 字节；随后为 PDU。",
    "- PDU 的寄存器数据按高字节在前、低字节在后传输；寄存器地址和数量均按协议字段编码。",
    "- 读取周期允许范围：`200 ms-1000 ms`；默认建议 `500 ms`，周期必须可配置。",
    "- 单帧最多读取 `120` 个寄存器；超过 120 个寄存器必须自动拆分为多帧，并按地址连续性合并结果。",
    "",
    "### 2.2 Modbus RTU（RS485 兼容链路）",
    "",
    "- 物理链路：RS485；协议：Modbus RTU；EMS 为主站，BCU 为从站。",
    "- 默认从站地址：`1, 2, 3, 4, 5, ...`；默认波特率：`57600`。串口号、校验位、停止位和从站地址必须可配置。",
    "- RTU 帧使用 CRC16；CRC 低字节先传、高字节后传。",
    "- 异常响应功能码为 `0x80 + 原功能码`，异常码至少区分：`0x01` 非法功能、`0x02` 非法数据地址、`0x03` 非法数据长度、`0x04` 读写失败。",
    "",
    "### 2.3 支持的功能码",
    "",
    table(["Function", "Meaning", "Requirement"], [
        {"Function": "0x03", "Meaning": "Read Holding Registers", "Requirement": "读取保持寄存器；用于可读点和 R/W 参数"},
        {"Function": "0x04", "Meaning": "Read Input Registers", "Requirement": "读取输入寄存器；用于只读测量/状态点"},
        {"Function": "0x06", "Meaning": "Write Single Register", "Requirement": "写单个寄存器；仅用于表中允许单点写入的 R/W 点"},
        {"Function": "0x10", "Meaning": "Write Multiple Registers", "Requirement": "写多个寄存器；支持连续参数批量下发"},
    ], ["Function", "Meaning", "Requirement"]),
    "",
    "## 3. 地址分区与功能码",
    "",
    table(["Block", "Register range", "Supported functions", "Direction"], [
        {"Block": "Rack Signal", "Register range": "0x0001-0x0200", "Supported functions": "0x03, 0x04", "Direction": "BCU -> EMS"},
        {"Block": "Rack Measure", "Register range": "0x0201-0x0400", "Supported functions": "0x03, 0x04", "Direction": "BCU -> EMS"},
        {"Block": "Rack Control", "Register range": "0x0401-0x0800; 0x0900-0x0901", "Supported functions": "0x03, 0x04, 0x06, 0x10", "Direction": "EMS -> BCU / readback"},
        {"Block": "Rack Diag", "Register range": "0x0801-0x0B00 (except 0x0900-0x0901, assigned to Control)", "Supported functions": "0x03, 0x04", "Direction": "BCU -> EMS"},
        {"Block": "Rack Detail", "Register range": "0x1000-0x6000", "Supported functions": "0x03, 0x04", "Direction": "BCU -> EMS"},
        {"Block": "Alarm parameters", "Register range": "0x6001-0x7000", "Supported functions": "0x03, 0x04, 0x06, 0x10", "Direction": "BAU/EMS <-> BCU"},
    ], ["Block", "Register range", "Supported functions", "Direction"]),
    "",
    "### 3.1 点表规模",
    "",
    table(["Table", "Rows in source sheet", "Use"], [
        {"Table": "Rack Signal", "Rows in source sheet": str(len(signals)), "Use": "状态、告警和 bit-field"},
        {"Table": "Rack Measure", "Rows in source sheet": str(len(measures)), "Use": "汇总测量"},
        {"Table": "Rack Control", "Rows in source sheet": str(len(controls)), "Use": "EMS 控制写入"},
        {"Table": "Rack Diag", "Rows in source sheet": str(len(diags)), "Use": "诊断、版本和液冷信息"},
        {"Table": "Rack Detail", "Rows in source sheet": str(len(details)), "Use": "单体、温度和连接器明细"},
        {"Table": "Alarm parameters", "Rows in source sheet": str(len(alarms)), "Use": "告警阈值读写"},
    ], ["Table", "Rows in source sheet", "Use"]),
    "",
    "## 4. 数据模型与实现规则",
    "",
    "1. `名称` 是上位机页面显示字段，必须保留中文；`Name` 是点位程序键；`Addr` 是十六进制 Modbus 寄存器地址。地址范围表达式（如 `0x1401-0x1600`）表示连续寄存器区间。",
    "2. `Number` 表示点数量或预留长度；`use_number` 表示实际使用数量。数组点必须按地址顺序映射为 `name_1 ... name_N` 或表中 `{}` 约定的索引名称。",
    "3. `Type` 决定原始寄存器解释：`u16` 无符号 16 位、`int16` 有符号 16 位；不得先转成浮点再判断符号。",
    "4. `Unit` 为工程量单位。缩放值写在单位中（如 `0.1V`、`0.1A`、`0.1℃`），上位机应同时保存原始值和工程值，避免阈值写入时重复缩放。",
    "5. `R` 点禁止写入；`R/W` 或 `W/R` 点允许读回和写入。写入后必须按协议响应校验，并建议立即读回确认。",
    "6. `reserve*` 点为保留空间，默认不展示、不下发、不参与告警判断；但地址必须保留，不能压缩映射。",
    "7. 对 bit-field 点，按 `Definition` 中的 bit 位解析；未定义 bit 必须保留原始值，不能静默丢弃。",
    "8. 地址分区存在一个功能归属例外：`0x0900`/`0x0901` 在数值上落入 Rack Diag 区间，但点表将其定义为 Rack Control 的 BCU Unix 时间高/低 16 位同步寄存器；配置路由必须以点表的 `Block/Name` 归属优先，不能仅按地址范围判断读写权限。",
    "",
    "## 5. 点表附录",
    "",
    "### 5.1 Rack Signal：状态/告警读取",
    "",
    "- 方向：BCU -> EMS；属性以 `R` 为主；支持 `0x03/0x04`。",
    "- 具有 bit 定义的连续行属于上一条点的位域说明；实现时应生成位级子信号，但保留原始 16 位寄存器。",
    "",
    table(signal_headers, signals, [signal_headers[0], "Name", "Description", "Definition", "Data origin", "Number", "Addr", "Unit", "Attribute", "Type", "use_number"]),
    "",
    "### 5.2 Rack Measure：汇总测量",
    "",
    table(measure_headers, measures, [measure_headers[0], "Name", "Description", "Definition", "Data origin", "Number", "Addr", "Unit", "Attribute", "Type", "use_number"]),
    "",
    "### 5.3 Rack Control：EMS 写入控制",
    "",
    "- 地址范围为 `0x0401-0x0800`，另含时间同步寄存器 `0x0900-0x0901`；后两点虽位于 Rack Diag 数值区间内，仍按 Rack Control 实现。",
    "- 写控制必须支持单寄存器 `0x06` 和连续多寄存器 `0x10`；具体点是否允许写入以 `Attribute` 为准。",
    "- 脉冲型命令（如 reset、clear fault）写入有效值后不得重复周期下发；应提供人工触发、超时和结果确认。",
    "",
    table(control_headers, controls, [control_headers[0], "Name", "Description", "Definition", "Comment", "Data origin", "Number", "Addr", "Unit", "Attribute", "Type"]),
    "",
    "### 5.4 Rack Diag：诊断与液冷信息",
    "",
    table(diag_headers, diags, [diag_headers[0], "Name", "Description", "Definition", "Data origin", "Number", "Addr", "Unit", "Attribute", "Type", "use_number"]),
    "",
    "### 5.5 Rack Detail：单体/温度/连接器明细",
    "",
    table(detail_headers, details, [detail_headers[0], "Name", "Description", "ASWName", "Comment", "Data origin", "Number", "Addr", "Unit", "Attribute", "Type", "use_number"]),
    "",
    "### 5.6 Rack Alarm Parameter Set & Read：告警阈值",
    "",
    "- 该区域为保持寄存器参数；Rack 地址从 `2` 到 `N+1`，每个 Rack 独立。",
    "- 支持 `0x03/0x04` 读取和 `0x06/0x10` 写入；写入必须具备权限控制、范围校验、写后读回和审计记录。",
    "",
    table(alarm_headers, alarms, [alarm_headers[0], "Name", "Description", "Definition", "Comment", "Data origin", "Number", "Addr", "Unit", "Attribute", "Type", "use_number"]),
    "",
    "## 6. 上位机功能需求",
    "",
    "- **连接管理**：支持 TCP Client 和 RS485 RTU 两种模式；显示连接状态、设备地址、重连次数和最近错误。",
    "- **轮询调度**：按地址块配置轮询周期；自动拆分超过 120 寄存器的请求；记录每次请求的事务号、功能码、起始地址、数量、耗时和响应状态。",
    "- **数据展示**：同时显示原始寄存器、工程值、单位、质量码、时间戳和数据来源；支持 bit-field 展开。",
    "- **控制写入**：对 R/W 点提供值域/枚举校验、二次确认、权限控制、写入结果和读回结果；禁止对 R 点显示写入口。",
    "- **告警**：通信超时、CRC/协议异常、非法功能、非法地址、非法数据、设备异常响应都要产生可检索事件；业务告警沿用 Rack Signal/Diag 位定义。",
    "- **数据记录**：至少支持 CSV 或 SQLite 记录；记录应包含设备地址、点名、寄存器地址、原始值、工程值、质量码和采集时间。",
    "- **配置导入**：点表应从版本化配置加载，不把地址和缩放因子硬编码在界面逻辑中。",
    "",
    "## 7. 验收测试要求",
    "",
    "1. TCP 连接 BMS `192.168.1.199:502` 成功，Unit ID 在 `0x01-0x41` 范围内可配置。",
    "2. 使用 `0x03` 和 `0x04` 分别读取各地址块，验证地址、数量、字节序、类型和单位换算。",
    "3. 对超过 120 寄存器的 Rack Detail 数组验证自动分帧、无重复、无丢失且地址连续。",
    "4. 对 Rack Control 和告警参数使用 `0x06`、`0x10` 写入，验证 R 点拒绝写入、R/W 点响应正确、写后读回一致。",
    "5. 注入 `0x01/0x02/0x03/0x04` 异常响应，验证错误分类、重试策略和事件记录。",
    "6. 验证 RTU CRC16 低字节在前，以及超时、断线、重连和串口参数切换。",
    "7. 对 bit-field 信号逐位构造测试值，验证位级告警与原始寄存器一致。",
    "",
    "## 8. 数据质量与待确认项",
    "",
    "- 源 Excel 的部分中文单元格读取结果包含替换字符，属于源文件编码/字体兼容问题；本需求文件以英文 `Name`、地址、单位、属性、类型和可读英文描述为准，中文业务描述需在正式基线发布前由协议负责人复核。",
    "- `Rack Signal` 中部分位域说明分布在主点下一行，需在配置导入器中实现父项继承。",
    "- `Rack Control` 中 `BCU_sync_time_h16b` 等时间同步点存在跨寄存器语义，低 16 位/高 16 位组合方式和写入顺序应由 BCU 固件接口负责人确认。",
    "- 表内温度单位存在编码异常（如 `0.1��`）；需求实现应按英文/数值语义复核为摄氏度后再固化，禁止直接依赖乱码文本。",
    "- 地址分区表与个别点的实际地址需在联调阶段做范围一致性校验；发现越界时以协议负责人签发的修订版点表为准。",
    "",
    "---",
    "",
    "*Generated on %s from the attached workbook.*" % datetime.now().strftime("%Y-%m-%d"),
]

OUTPUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
print(OUTPUT)
print({"signals": len(signals), "measures": len(measures), "controls": len(controls), "diags": len(diags), "details": len(details), "alarms": len(alarms)})
