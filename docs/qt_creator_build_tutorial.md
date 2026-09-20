# Qt Creator 编译与发布教程

## 1. 适用范围

本文适用于 `qmodbus-master` 工程，目标是使用 Qt Creator 完成 Qt 6 工程配置、编译、运行和 Windows 可执行文件部署。

当前工程基线：

| 项目 | 配置 |
| --- | --- |
| Qt | Qt 6.11.2 MSVC 2022 64-bit |
| 编译器 | Microsoft Visual C++ 2022 x64 |
| 构建工具 | qmake + NMake |
| 工程文件 | `qmodbus-master/qmodbus.pro` |
| 构建配置 | Release |
| 推荐构建目录 | `build_qt6` |
| 输出文件 | `build_qt6/release/qmodbus.exe`（推荐）或 `qmodbus-master/release/qmodbus.exe` |

## 2. 环境要求

安装以下组件：

- Qt 6.11.2 MSVC 2022 64-bit
- Qt Creator
- Visual Studio 2022 Desktop development with C++
- MSVC 2022 编译工具
- Windows 10/11 SDK

Qt 安装目录应包含：

```text
D:\Software\Qt\6.11.2\msvc2022_64
```

Visual Studio 编译环境脚本：

```text
C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
```

## 3. 打开工程

启动 Qt Creator，选择：

```text
文件 -> 打开文件或项目
```

打开工程文件：

```text
E:\Work\01-FOXCONN\01-Project\01-AIO_V2\BMS\05-HostPC\03-ModbusPC\qmodbus-master\qmodbus.pro
```

不要打开 `Makefile`，也不要把已有构建目录当作工程目录打开。

## 4. 在新版 Qt Creator 中打开首选项

部分新版 Qt Creator 不再在“工具”菜单中显示 `Options` 或 `Kits`。根据界面布局，应点击窗口左下角的齿轮图标：

```text
左下角 -> 首选项
```

也可以从菜单进入：

```text
编辑 -> 首选项
```

打开首选项后，在左侧选择“构建和运行”或直接搜索 `Kits`。其中包含三个相关页面：

- `Qt Versions`：配置 qmake 和 Qt 版本。
- `Compilers`：配置 MSVC 编译器。
- `Kits`：将 Qt 版本、编译器和桌面设备组合为可用 Kit。

## 5. 配置 Qt Version

进入首选项中的：

```text
构建和运行 -> Qt Versions
```

点击“添加”，选择：

```text
D:\Software\Qt\6.11.2\msvc2022_64\bin\qmake.exe
```

确认 Qt 版本显示为：

```text
Qt 6.11.2 MSVC 2022 64-bit
```

如果 Qt Creator 已经自动识别该版本，不需要重复添加。重点是确认 qmake 路径指向 `msvc2022_64`，不能指向 `mingw` 目录。

## 6. 配置 MSVC 编译器

进入：

```text
构建和运行 -> Compilers
```

确认存在以下编译器：

```text
Microsoft Visual C++ Compiler 2022 (amd64)
```

如果没有该编译器：

1. 打开 Visual Studio Installer。
2. 修改 Visual Studio 2022 安装。
3. 勾选“使用 C++ 的桌面开发”。
4. 确认安装 MSVC v143 和 Windows 10/11 SDK。
5. 重启 Qt Creator。

## 7. 配置 Kit

进入首选项中的：

```text
构建和运行 -> Kits
```

优先使用自动识别的 Desktop Kit。如果没有自动生成，点击“添加”，关键配置如下：

| 配置项 | 值 |
| --- | --- |
| Device type | Desktop |
| Compiler | Microsoft Visual C++ 2022 x64 |
| Qt version | Qt 6.11.2 MSVC 2022 64-bit |
| Build tool | NMake |
| CMake | 不使用 |

本工程是 qmake 工程，不需要配置 CMake 构建流程。

如果 Kit 使用了 MinGW 编译器，应切换到 MSVC 2022 64-bit。Qt 库、编译器和目标架构必须保持一致。

## 8. 配置构建目录

打开左侧项目配置页：

```text
Projects -> Build
```

选择 Qt 6 MSVC 2022 64-bit Kit，并设置：

```text
Build configuration: Release
Build directory: E:\Work\01-FOXCONN\01-Project\01-AIO_V2\BMS\05-HostPC\03-ModbusPC\build_qt6
```

确认构建步骤包含：

1. qmake
2. Build

Release 构建完成后，目标文件应位于：

```text
E:\Work\01-FOXCONN\01-Project\01-AIO_V2\BMS\05-HostPC\03-ModbusPC\build_qt6\release\qmodbus.exe
```

## 9. 编译工程

点击 Qt Creator 左下角的锤子按钮，或按：

```text
Ctrl+B
```

Qt Creator 会依次执行 qmake 和 NMake。构建输出窗口没有错误并显示构建完成，即表示编译成功。

命令行等价操作如下：

```bat
cd /d E:\Work\01-FOXCONN\01-Project\01-AIO_V2\BMS\05-HostPC\03-ModbusPC\build_qt6
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
qmake ..\qmodbus-master\qmodbus.pro
nmake /f Makefile.Release
```

## 10. 运行程序

编译成功后，点击 Qt Creator 左下角的绿色运行按钮，或按：

```text
Ctrl+R
```

运行目标应为：

```text
build_qt6\release\qmodbus.exe
```

工程目录下的 `qmodbus-master/release/qmodbus.exe` 也会在使用源目录中的 `Makefile.Release` 编译时更新。两个程序来自同一份源码，但调试时应确认 Qt Creator 的运行目标指向当前构建目录，避免误启动旧的 Release 文件。

## 10.1 连接 BCU 并发送 Modbus 请求

1. 根据设备接口选择 `TCP/IP`、`RTU` 或 `ASCII` 连接页。
2. TCP 连接页在未激活时即可填写 Network Address 和端口，填写 BCU 的实际 IP 和端口（常用端口为 `502`）；RTU/ASCII 选择正确的 COM 口、波特率、数据位、校验位和停止位。
3. 点击对应页的 `Active` 复选框。只有连接成功且 `Active` 保持勾选时，主窗口才会绑定该 Modbus 会话。
4. 回到 `Modbus Request` 页设置 `Slave ID`、功能码、起始地址和数量，点击 `Send`。

如果状态栏显示 `No active Modbus connection. Enable Active on the RTU, TCP or ASCII connection tab.`，表示当前没有可用的已打开会话。请先确认第 3 步，并检查连接页状态栏给出的具体错误（例如 IP、端口、COM 口或串口参数错误）。程序会自动恢复已经打开但尚未绑定到主窗口的唯一会话；如果 TCP、RTU、ASCII 同时存在多个活动会话，应只保留实际使用的一个。

请求地址使用 Modbus 协议的 0 基寄存器地址。点表中的十六进制地址（例如 `0x1401`）应换算为十进制 `5121` 后填写；不要把文档中的 `40001`/`30001` 显示编号直接作为起始地址。

### 无设备启动测试

在 Qt Creator 的：

```text
Projects -> Run -> Arguments
```

中加入：

```text
-platform offscreen
```

该参数用于没有显示设备或自动化 smoke test 场景。正常桌面使用时不需要添加。

## 11. 发布可执行文件

单独复制 `qmodbus.exe` 通常无法在未安装 Qt 的计算机上运行，需要使用 `windeployqt` 收集依赖。

在 Qt 6 MSVC 命令行中执行：

```bat
D:\Software\Qt\6.11.2\msvc2022_64\bin\windeployqt.exe ^
  --release ^
  --compiler-runtime ^
  E:\Work\01-FOXCONN\01-Project\01-AIO_V2\BMS\05-HostPC\03-ModbusPC\build_qt6\release\qmodbus.exe
```

部署目录至少应包含：

```text
qmodbus.exe
Qt6Core.dll
Qt6Gui.dll
Qt6Widgets.dll
Qt6SerialPort.dll
Qt6Sql.dll
platforms\qwindows.dll
sqldrivers\qsqlite.dll
```

其中：

- `Qt6SerialPort.dll` 用于串口枚举和串口配置。
- `Qt6Sql.dll` 用于 SQLite 采集历史和审计数据。
- `sqldrivers\qsqlite.dll` 是 SQLite 驱动，缺少它会导致数据库无法打开。
- `platforms\qwindows.dll` 是 Windows 图形平台插件，缺少它会导致程序启动失败。

本工程已提供完整部署目录：

```text
E:\Work\01-FOXCONN\01-Project\01-AIO_V2\BMS\05-HostPC\03-ModbusPC\build_qt6\deploy_qt6
```

部署后建议直接运行该目录中的 `qmodbus.exe`，或将整个目录复制到一台未安装 Qt 的测试电脑上验证启动。不要使用尚未重新部署的旧目录。

## 12. 常见问题

### 12.1 找不到 Kits 页面

这是新版 Qt Creator 的界面位置变化，不代表 Qt 6.11.2 没有安装。请点击左下角齿轮图标“首选项”，再进入“构建和运行”。不要只在“工具”菜单中查找 `Kits`。

### 12.2 Qt Version 已添加但没有可用 Kit

依次检查：

1. `Qt Versions` 中是否添加了 `...\msvc2022_64\bin\qmake.exe`。
2. `Compilers` 中是否存在 MSVC 2022 amd64。
3. `Kits` 中 Qt 版本和编译器是否属于同一架构。
4. 是否误选了 MinGW Qt 或 32-bit 编译器。

完成后关闭并重新打开 `qmodbus.pro`，在项目配置页重新选择 Kit。

### 12.3 找不到 `cl.exe`

原因通常是 Kit 使用了 MinGW，或者 Visual Studio C++ 工具链未安装。

处理方法：

1. 在 Visual Studio Installer 中安装 Desktop development with C++。
2. 确认 MSVC 2022 和 Windows SDK 已安装。
3. 在 Qt Creator 中选择 MSVC 2022 x64 Kit。

### 12.4 找不到 Qt DLL

如果启动时报错缺少 `Qt6Core.dll`、`Qt6Widgets.dll` 等文件，说明没有完成部署。

重新执行 `windeployqt --release --compiler-runtime`，并确认 DLL 与 `qmodbus.exe` 位于同一发布目录。

### 12.5 TCP Active 已勾选但仍提示未连接

`Active` 表示用户请求启用连接；只有 TCP 三次握手成功后，Modbus 会话才会打开。连接失败时新版程序会自动取消 `Active`，并在状态栏显示底层错误。

可在 PowerShell 中检查 BCU 的网络和端口：

```powershell
Test-Connection 192.168.1.199 -Count 2
Test-NetConnection 192.168.1.199 -Port 502
```

如果 Ping 成功但 `TcpTestSucceeded` 为 `False`，应检查 BCU 是否启用 Modbus TCP Server、端口是否为 `502`、设备防火墙以及 PC 与 BCU 是否位于同一网段。

### 12.6 Modbus Poll 可以读到，但 QModbus 超时或 connection reset

确认 Modbus Poll 的所有窗口已停止或关闭后，再测试 QModbus。部分 BCU/网关只允许一个 Modbus TCP 客户端；当多个 Modbus Poll 窗口持续轮询时，新的客户端可能被设备静默丢弃或主动复位连接。联调时应一次只保留一个主站，并先停止 `Poll`，再用 QModbus 单次 `Send` 验证。

### 12.7 找不到 SQLite 驱动

如果程序提示数据库无法打开，检查：

```text
sqldrivers\qsqlite.dll
```

该文件必须位于程序目录下的 `sqldrivers` 子目录。

### 12.8 qmake 配置异常

关闭 Qt Creator 后，清理构建目录中的以下文件，再重新配置：

```text
.qmake.stash
Makefile
Makefile.Release
release\
```

不要删除源代码目录中的工程文件和 `data\point_table.json`。

### 12.9 编译器和 Qt 架构不一致

以下组合不能混用：

- Qt MSVC 64-bit + MinGW 编译器
- Qt 32-bit + MSVC 64-bit 编译器
- Qt 6 + Qt 5 生成的旧构建目录

遇到链接错误时，优先确认 Qt Version、Compiler、Kit 和构建目录是否匹配。

## 13. 编译完成检查清单

- [ ] 打开的工程是 `qmodbus.pro`
- [ ] Qt Version 为 Qt 6.11.2 MSVC 2022 64-bit
- [ ] Compiler 为 MSVC 2022 x64
- [ ] Build configuration 为 Release
- [ ] 构建目录为 `build_qt6`
- [ ] `qmodbus.exe` 已生成
- [ ] Qt Creator 运行目标指向本次构建生成的 `release\qmodbus.exe`
- [ ] `-platform offscreen` 启动测试通过
- [ ] `windeployqt` 已复制 Qt DLL
- [ ] `sqldrivers\qsqlite.dll` 已存在
- [ ] 在无 Qt 开发环境的测试电脑上启动成功

### 13.1 BCU/EMS 页面显示约定

在 `BCU/EMS overview` 页面中，表格列按以下顺序显示：

```text
Name | Address | Key | Value | Unit | Quality | Updated (UTC)
```

其中：

- `Name` 为点表中的中文名称，用于上位机页面显示。
- `Address` 使用四位大写十六进制格式，例如 `0x0001`、`0x0900`。
- 页面按照地址数值从小到大排列，不按显示字符串排序。
- `Block` 已从表格列中移除，业务块通过上方页签和筛选框选择。

## 14. 当前工程验证基线

本工程当前已使用 Qt 6.11.2 MSVC 2022 64-bit 验证：

- 主程序 Release 编译成功。
- 点表、轮询、采集存储、业务视图、控制事务和告警状态测试通过。
- 主程序在 `offscreen` 模式下可持续运行。
