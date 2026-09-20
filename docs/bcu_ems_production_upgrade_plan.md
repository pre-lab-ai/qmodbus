# BCU/EMS 生产上位机升级开发计划

## 1. 目标与执行规则

目标是将现有 `qmodbus-master` 从通用 Modbus 调试工具升级为面向 BCU/EMS 的生产上位机，覆盖：稳定连接、点表驱动、中文显示、轮询采集、工程量解码、告警、控制写入、历史数据、审计和可发布安装包。

执行规则：

1. 所有步骤按顺序执行，不跳过门禁。
2. 每一步必须同时通过代码检查、自动化测试/构建验证和必要的运行验证，才进入下一步。
3. 每一步只解决一个可验收的架构问题，保留可回滚的独立提交边界。
4. 生产功能必须优先使用点表配置驱动，禁止继续把地址、中文名、单位和缩放因子硬编码到 UI 槽函数。
5. 真实 BCU 联调前使用模拟从站或回环测试，避免将未经验证的写控制发送到现场设备。

## 2. 工具链和环境基线

### 2.1 推荐环境

- Qt 6.11.2 MSVC 2022 64-bit，组件至少包括 Qt Core、Gui、Widgets、SerialPort、Tools。
- Visual Studio 2022 C++ Desktop workload，包含 MSVC、Windows SDK、`nmake`、`rc.exe`。
- 固定 qmake、编译器、Windows SDK 和 libmodbus 版本，写入构建说明。
- Qt Creator 可选，不是编译必需；但建议用于 UI `.ui` 调整和调试。

当前机器已验证：Qt 6.11.2 `msvc2022_64` qmake/uic/moc/rcc、Qt SerialPort、Visual Studio 2022 MSVC/nmake 和 Windows SDK 可用。工程使用 Qt SerialPort 枚举串口，Modbus TCP/RTU/ASCII 由内置 libmodbus 负责。

## 3. 阶段总览与门禁

| 步骤 | 状态 | 目标 | 进入下一步的门禁 |
| --- | --- | --- | --- |
| 0. 构建基线 | 已完成 | 工程可在固定工具链下生成可执行文件 | qmake + nmake 成功，程序可启动 |
| 1. 生命周期与 TCP 连接修复 | 已完成 | 消除重复 TCP 重连、补齐上下文释放 | 编译成功，启动 smoke test 通过 |
| 2. 协议会话抽离 | 已完成 | 用 RAII 会话对象替代 UI 直接持有裸 `modbus_t*` | 已通过编译和启动 smoke test；协议模拟测试待补强 |
| 3. 点表领域模型 | 已完成 | 加载中文名称、英文键、地址、类型、单位、权限 | 点表完整性校验通过，解码测试通过 |
| 4. 轮询调度与后台线程 | 已完成 | 实现分组、周期、拆帧、超时、重试和取消 | 120 寄存器边界及高负载测试通过 |
| 4.5. Qt 6 全面迁移 | 已完成 | 移除 qextserialport，切换 Qt SerialPort，修复 Qt 6 API 和部署配置 | Qt 6 主程序、点表测试、轮询测试和部署包 smoke test 通过 |
| 5. 采集存储与质量码 | 已完成（基础版） | 保存原始值、工程值、时间戳、质量和审计 | 数据层、手动读取/后台轮询接入、SQLite/CSV、写入审计和保留策略通过；控制安全闭环留至步骤 7 |
| 6. BCU/EMS 页面重构 | 已完成 | 按业务分区提供总览、测量、控制、诊断、告警页面 | 点表驱动业务页、中文名称、业务块页签、采集状态和 Qt6 启动验证通过；控制安全闭环进入步骤 7 |
| 7. 控制与告警安全闭环 | 已完成（含 BCU 联调） | 写入校验、权限、确认、读回、告警确认/恢复 | 用户已确认完成与 BCU 的联调；现场签字记录仍归档到验收单 |
| 8. 发布与现场验收 | 软件发布准备完成；现场验收待执行 | 安装包、配置迁移、诊断、升级和现场验收 | 发布包可复现；干净机、24h TCP/RTU、升级回滚和逐点抽测仍需现场门禁 |

## 4. 详细步骤

### 步骤 0：构建基线

**实施内容**

- 固定 qmake + MSVC + Windows SDK 的构建命令。
- 在 `qmodbus.pro` 补充 Windows 头文件边界和串口枚举依赖：`WIN32_LEAN_AND_MEAN`、`user32`、`advapi32`。
- 使用独立 `build_baseline` 目录，不污染源代码目录。

**验收**

- qmake 生成 Makefile 成功。
- nmake 完成并生成 `release/qmodbus.exe`。
- 可启动进程并保持事件循环运行。

**结果**

- 已通过。生成物位于 `build_baseline3/release/qmodbus.exe`。

### 步骤 1：连接生命周期与 TCP 重连修复

**实施内容**

- `TcpIpSettingsWidget::tcpConnect()` 只在上下文为空时创建连接。
- 禁止 `MainWindow::sendModbusRequest()` 每次请求重新连接 TCP。
- TCP 禁用时主动 `modbus_close()` + `modbus_free()`。
- 串口设置组件析构时释放活动上下文。
- 连接激活信号以真实连接结果为准，不在仅勾选控件时报告成功。

**验收**

- 编译成功。
- 无设备时启动程序不崩溃。
- offscreen 启动 smoke test 至少保持运行 3 秒。
- 静态确认发送函数不再调用 TCP 重连函数。

**结果**

- 已通过。实现见 `tcpipsettingswidget.*`、`serialsettingswidget.cpp`、`mainwindow.cpp`；`build_step1/release/qmodbus.exe` 已生成，启动 smoke test 通过。

### 步骤 2：协议会话抽离

**实施内容**

新增 `transport` 模块：

- `ModbusSession`：封装上下文创建、连接、关闭、重连、响应超时和错误状态。
- `ModbusRequest`/`ModbusResponse`：封装功能码、站号、地址、数量、原始数据和异常信息。
- `ModbusSessionPtr`：使用 RAII 或明确的唯一所有权，不让 UI 和批处理共享裸指针。
- `IModbus` 降级为传输适配接口，移除 `modbus_t* modbus()` 暴露。
- 总线监视回调改为 Qt signal 或事件队列，不再通过 `globalMainWin` 回调 UI。

**验收**

- TCP、RTU、ASCII 创建/连接/关闭均有测试。
- 超时、断线、非法功能、非法地址和设备异常可转换为统一错误对象。
- 连接关闭后无悬空会话；AddressSanitizer 或运行时检查无泄漏。
- MainWindow 不再包含 `modbus_read_*`/`modbus_write_*` 直接调用。

**结果**

- 已通过。新增 `src/modbussession.h/.cpp`；设置组件、MainWindow 和 BatchProcessor 已切换到会话接口；`build_step2/release/qmodbus.exe` 已生成，启动 smoke test 通过。

### 步骤 3：点表领域模型

**实施内容**

建立 `point_model` 和版本化配置：

- 字段：`名称`、`Name`、`Block`、`Addr`、`Number`、`Unit`、`Attribute`、`Type`、缩放、枚举、bit 定义、读写功能码。
- 中文 `名称` 只用于页面显示；英文 `Name` 是稳定程序键。
- 导入现有通信点表的 6 个附录表，并保留 Rack Control `0x0900/0x0901` 例外映射。
- 启动时校验名称唯一性、英文键唯一性、地址格式、区间重叠、类型和读写属性。

**验收**

- 点表可在无设备状态下加载并显示中文名称。
- `u16`、`int16`、0.1 缩放、bit-field、数组和保留区均有单元测试。
- 检出地址冲突时阻止进入采集状态并显示明确错误。
- `0x0900/0x0901` 被解析为 Control 时间同步点。

**结果**

- 已通过。新增 `src/pointmodel.h/.cpp`，提供版本化点定义、类型/权限解析、工程值解码、bit-field 展开、数组解码和完整性校验。
- 新增 `tools/generate_point_table.py`，从本需求 Markdown 的六个附录生成 `data/point_table.json`；当前配置包含 248 个点定义，保留中文 `display_name`、英文 `key`、`source_key`、地址原文、单位、属性、缩放和 bit 定义。
- 重复源键自动生成唯一运行键并保留 `source_key` 追溯；保留区允许与实际点地址重叠；`0x0900/0x0901` 按 Rack Control 校验。
- MainWindow 启动时加载 `:/config/point_table.json`，状态栏报告点表加载数量或明确校验错误。
- 新增 `tests/pointmodel_test.pro` 和 `pointmodel_test.cpp`，覆盖 `u16`、`int16`/0.1 缩放、bit-field、数组、保留区、重复键、地址重叠和 Control 时间同步例外。
- Qt 5.15.2 MSVC 64-bit 下点表测试返回 0；`build_step3/release/qmodbus.exe` 编译成功；`-platform offscreen` 启动 smoke test 通过。

### 步骤 4：轮询调度与后台线程

**实施内容**

- 新增 `PollScheduler`，按业务块建立轮询组和周期。
- 连续寄存器自动合并；每帧最多 120 个寄存器，跨保留区或权限区时拆分。
- 请求在 worker 线程串行执行；支持超时、有限重试、取消和退避。
- 采集结果返回结构化消息，UI 线程只消费结果。
- 保留手动读取入口，但也走同一请求队列。

**验收**

- 200 ms-1000 ms 周期稳定运行，不阻塞 UI。
- 120、121、125 寄存器边界测试通过。
- 断线后不产生并发重连风暴；恢复后自动继续轮询。
- 相同会话不允许多个请求并发写入。

**结果**

- 已通过。新增 `src/pollscheduler.h/.cpp`，提供 `PollPlan`、`PollWorker` 和 `PollScheduler`。
- 点表中的非保留寄存器按业务块和功能码合并为连续范围，超过 120 个寄存器自动拆帧；Rack Control/Alarm parameters 优先使用 `0x03`，测量/信号/诊断/明细优先使用 `0x04`。
- worker 运行在独立 `QThread`，同一时刻只执行一个请求；每帧维护独立到期时间，避免帧数放大轮询周期；失败支持最多 5 次配置重试，并按帧执行指数退避（上限 30 秒）。
- 新增 `ModbusSessionTransport`，将现有 `ModbusSession` 适配到轮询传输接口；调度结果通过 Qt queued signal 返回，便于 UI 线程消费。
- 新增 `tests/pollscheduler_test.pro` 和 `pollscheduler_test.cpp`，覆盖 120/121/125 寄存器边界、连续范围合并、后台 worker、失败重试、结果回传、停止销毁和生成点表全量帧上限。
- Qt 5.15.2 MSVC 64-bit 下轮询测试返回 0；`build_step4/release/qmodbus.exe` 编译成功；`-platform offscreen` 启动 smoke test 通过。

### 步骤 5：采集存储、质量码和审计

**实施内容**

- 统一结果字段：设备、站号、点名、中文名称、地址、原始值、工程值、单位、质量码、时间戳、请求耗时。
- 使用 SQLite 保存历史和通信事件；CSV 作为导出格式，不作为主存储。
- 写入审计：用户、时间、点名、旧值、新值、结果、异常信息。
- 设计数据保留、归档和磁盘空间策略，避免高频写盘。

**验收**

- 断线、超时、异常响应、恢复和写入失败均有质量码和事件记录。
- 重启后历史数据可继续追加且数据库完整性检查通过。
- 导出 CSV 有固定表头、版本和时区信息。

**当前实现进度**

- 新增 `src/qualitycode.h` 和 `src/acquisitionstore.h/.cpp`，统一 `GOOD`、`TIMEOUT`、`DISCONNECTED`、`PROTOCOL_ERROR`、`INVALID_DATA` 等质量码。
- SQLite 建立采集样本、通信事件和审计事件表；样本原始值/工程值使用 JSON 保存，支持数组和 bit-field，并使用事务写入。
- 主窗口初始化应用数据目录下的 `acquisition.sqlite`；手动保持寄存器/输入寄存器读取已记录中文名称、地址、原始值、工程值、质量码、时间戳和耗时。
- `tests/acquisitionstore_test.cpp` 已覆盖成功、超时、恢复、审计、CSV 固定表头、重启续写和 `PRAGMA integrity_check`。
- 后台 `PollScheduler` 结果已接入统一存储；启动时按 `acquisitionRetentionDays` 执行可选保留清理；手动写入成功/失败均记录审计事件（含中文名、旧值/新值、功能码、地址和数量）。控制安全闭环仍在步骤 7 实现。

### 步骤 6：BCU/EMS 页面重构

**实施内容**

- 总览：连接状态、BCU 版本、关键告警、Rack 数量和采集健康度。
- Rack Signal/Measure：中文名称、英文键、工程值、单位、质量、更新时间。
- Rack Detail：单体电压/温度、连接器温度等数组点分页和异常高亮。
- Rack Control：仅展示可写点，显示值域、权限、二次确认和读回结果。
- Rack Diag：软件版本、BMU 通信状态、液冷信息。
- Alarm：告警等级、首次发生、最近发生、恢复时间、确认人和原始 bit。

**当前实现**

- 新增 `BusinessViewWidget`，从点表动态生成总览、Rack Control、Rack Signal、Rack Measure、Rack Diag，以及 Rack Detail 的单体电压、单体温度和辅助明细页签。
- `Alarm parameters` 在点表中表示告警阈值/恢复阈值配置，不是告警发生状态；已从运行总览页签和筛选项移除，后续应在独立参数配置页面展示。
- 总览使用单一可扩展主表占满业务页面，主窗口启动时最大化显示；表格列按内容自适应并保留人工拖拽调整，移除右侧选择摘要和底部 `Property/Value` 详情表。
- Overview 只显示系统状态、充放电状态、热管理状态、BMS 故障等级、单体最高/最低电压及编号、单体最高/最低温度及编号、SOC/SOH/SOE、电压电流、绝缘阻值、铜排温度和充放电能力等关键点；页面显示中文名称、英文键、地址、工程值、单位、质量码和 UTC 更新时间。
- Rack Signal 保留同地址多信号行，增加中文 `Definition` 列；寄存器 Value 统一按四位大写十六进制显示，具有 bit 定义的点默认折叠，点击父行可展开非预留 bit 子行，再次点击即可收回。
- Rack Control 已恢复启动绝缘采样、下高压控制、主控复位和一键清除异常事件等 R/W 点；Rack Diag 已恢复主控软件项目编号、主/子/修正版本号。
- Rack Detail 的单体电压和单体温度按点表配置数量展开为独立页，名称分别为 `单体电压001...`、`电池温度001...`，地址按起始地址连续递增；电压数量使用 Excel `use_number`，温度数量使用点表注释中的配置数量。
- 无设备时启动采集会显示错误且不进入运行态；采集结果通过领域结构刷新，不直接访问 libmodbus。

**步骤 6 验收结果**

- `businessviewwidget_test` 已验证总览关键点、Rack Control/Rack Diag 缺失点、单体电压/温度分页、Rack Signal 位域展开和 bit 数值排序。
- Qt6 主程序及业务视图均以 `-platform offscreen` 启动验证通过；步骤 6 页面不直接访问协议上下文。
- Rack Control 的具体写入确认、读回和审计由步骤 7 的控制安全闭环负责。

**验收**

- UI 不直接 include libmodbus 头文件。
- 页面只绑定 ViewModel/领域结果，不读取协议上下文。
- 中文名称与需求点表一致，地址例外点显示正确。
- 模拟数据、断线状态、空数据和异常状态均有明确页面表现。

当前基础版验收已通过 Excel/需求 Markdown/JSON 点位一致性检查、Qt6 offscreen 启动和 `businessviewwidget_test`；控制交互、告警发生/恢复/确认将在步骤 7 完成。

### 步骤 7：控制与告警安全闭环

**实施内容**

- R 点禁止写，R/W 点按功能码允许 0x06/0x10。
- 写入前做类型、范围、枚举、单位和权限校验。
- reset/clear 类命令采用一次性命令模型，禁止轮询重复发送。
- 写入后读回验证；失败时保留请求帧、响应帧和错误码。
- 告警按发生、保持、恢复、确认建模，保留原始寄存器和 bit。

**验收**

- 模拟从站覆盖成功、拒绝、超时、异常响应、写后读回不一致。
- 未授权用户无法执行控制写入。
- 告警发生/恢复/确认历史顺序正确。

**当前实现**

- 新增 `src/controlvalidator.h/.cpp`：按点表 R/W 属性和允许功能码校验写入；支持命令枚举（如 0x01/0x02/0xff）和操作角色（operator/engineer/administrator），拒绝 viewer 等未授权角色。
- 手动写入增加二次确认对话框；写入成功后立即使用保持寄存器读回并逐寄存器比对，失败则生成 `write_failure` 通信事件和失败审计。
- 新增 `src/alarmstate.h/.cpp`：bit 告警按 occurred、held、recovered、acknowledged 状态管理；发生/恢复事件写入通信事件表，保留原始点、bit、中文名称和时间。
- 新增 `tests/controlalarm_test.cpp`，覆盖权限、功能码、枚举、告警发生/保持/恢复/确认规则。
- 新增 `src/controltransaction.h/.cpp` 及 `tests/controltransaction_test.cpp`，通过模拟从站覆盖写成功、设备拒绝、超时和写后读回不一致四类场景。
- Rack Control 页面新增 Definition、Access、Action 列；写入按钮连接到 MainWindow 的控制事务，执行角色校验、十进制/十六进制枚举校验、二次确认、写后读回和审计记录。后台轮询运行时禁止发起控制写入。
- 控制页使用十进制枚举定义（如 `1：Enable；2：Disable`）时，`ControlValidator` 也会执行有效值约束；审计用户从 `operatorRole` 配置读取。
- 控制事务按功能码调用真实的 0x06/0x10 写入接口；手动写入文本先做严格的十进制/十六进制解析，非法值不发送。
- Rack Signal 活动 bit 行提供右键“确认告警”入口，不新增独立 Alarm 页面；确认动作调用 `AlarmStateModel`，并将确认人、来源点、bit、原始寄存器值和时间写入通信事件。
- 通信事件表新增 `point_key`、`bit_index`、`raw_value` 字段，启动时自动迁移旧数据库，支持告警发生、恢复和确认的结构化追溯。
- `ControlTransaction` 已统一封装 0x06/0x10 写入和读回验证；模拟从站测试覆盖单寄存器、多寄存器、设备拒绝、超时、异常响应长度和读回不一致。
- 存储测试覆盖告警结构化事件和旧版 SQLite 表自动迁移，迁移后可继续写入确认事件。

当前基础版已通过 Qt6 编译、控制/告警领域测试、模拟从站场景测试、告警确认入口测试和 offscreen 启动验证；真实设备联调仍需后续完成。

### 步骤 8：发布和现场验收

**实施内容**

- 固定 Qt/MSVC/libmodbus 版本，生成可重复构建包。
- 安装器包含 Qt DLL、VC runtime、配置模板、数据库迁移和卸载逻辑。
- 配置和数据目录与程序目录分离；支持备份、恢复和版本迁移。
- 建立现场诊断包：版本、配置摘要、通信统计和脱敏日志。

**验收**

- 干净 Windows 环境安装并启动成功。
- TCP/RTU 长时间运行 24 小时无崩溃、无连接泄漏、无数据库损坏。
- 升级保留点表配置和历史数据，回滚可恢复上一个版本。
- 现场 BCU/EMS 点表逐项抽测，控制写入需人工签字确认。

**软件发布准备结果（2026-09-16）**

- 固定构建基线：Qt 6.11.2 MSVC 2022 x64、内置 libmodbus 3.1.1、qmake + NMake Release；构建命令和部署目录记录在 `qt_creator_build_tutorial.md`。
- 主程序将 `QSettings` 组织/应用标识固定为 `Foxconn/ModbusPC`，SQLite 和可变数据迁移到 `%LOCALAPPDATA%\\Foxconn\\ModbusPC`；首次升级会复制程序目录中的旧 `acquisition.sqlite`，不删除旧文件。
- SQLite schema 版本固定为 2，启动时继续执行旧表字段迁移；控制/告警历史保留，数据库完整性由现有测试覆盖。
- `tools/create_release_manifest.ps1` 生成 `release_manifest.json`、SHA-256 文件和 Windows x64 ZIP；清单包含 Qt/MSVC/libmodbus、点表 hash、可执行文件 hash、schema 版本和 TRACE=false。
- `tools/backup_restore_modbuspc.ps1` 提供数据目录备份与恢复；恢复前保留带时间戳的旧目录，便于回滚。
- `qmodbus.nsi` 已改为固定版本和实际部署目录，卸载仅移除程序目录，不删除用户数据；NSIS 是否安装和最终安装器构建仍需在发布机验证。
- Qt Release 编译成功，`windeployqt --release --compiler-runtime` 已收集 Qt DLL、SQLite 驱动和 VC runtime；`build_qt6\\release_package\\ModbusPC-0.1.1-win64.zip` 已生成。

**现场验收保留项**

- 干净 Windows 安装/卸载与升级回滚；
- TCP、RTU 各链路连续运行 24 小时，无崩溃、连接泄漏或数据库损坏；
- 真实 BCU/EMS 点表逐项抽测、异常/断线/恢复场景和控制写入人工签字；
- 发布机使用 NSIS 生成并试装 `ModbusPC-0.1.1-setup.exe`。

## 5. 当前进度

- 步骤 0：已完成。
- 步骤 1：已完成并通过编译及启动 smoke test。
- 步骤 2：已完成编译级验收。
- 步骤 3：已完成并通过点表加载、完整性校验、解码单元测试、主程序编译及启动 smoke test。
- 步骤 4：已完成并通过轮询计划、120 寄存器拆帧、后台 worker、重试/退避、取消和主程序构建验证。
- 步骤 4.5：已完成 Qt 6.11.2 MSVC 64-bit 全面迁移；主程序、点表测试、轮询测试均返回 0，windeployqt 独立目录启动 smoke test 通过。
- 步骤 5：已完成（基础版）；采集存储、质量码、手动/后台轮询、保留清理、CSV、SQLite 完整性和写入审计均已验证。
- 步骤 6：已完成；关键点总览、控制/诊断缺失点恢复、单体电压/温度独立页、Rack Signal 十六进制值与可折叠位域解析、bit 数值排序和采集状态已验证。
- 步骤 7：已完成（含 BCU 联调）；控制页写入按钮、权限/枚举校验、二次确认、0x06/0x10 写后读回、审计、Rack Signal 告警确认入口和告警发生/保持/恢复/确认事件持久化已验证，用户已确认与 BCU 联调完成。
- 步骤 8：软件发布准备完成；发布构建、Qt/VC runtime 部署、用户数据目录隔离、schema 迁移、发布清单、备份/恢复脚本和便携 ZIP 已完成。现场干净机安装、24 小时运行、升级回滚和逐点抽测待执行。
