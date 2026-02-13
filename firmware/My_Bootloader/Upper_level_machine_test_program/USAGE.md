# STM32 Bootloader 通信测试程序使用说明

## 程序概述

这是一个用于与STM32双区Bootloader系统进行通信的上位机测试程序，支持固件升级、程序跳转等功能。

## 功能特性

- **固件升级**: 将新的固件下载到A区
- **程序跳转**: 直接跳转到A区应用程序运行
- **区域复制**: 将B区内容复制到A区并跳转
- **实时监控**: 持续监视串口通信状态
- **断线重连**: 自动检测并重连断开的串口连接
- **在线升级**: 在A区程序运行时可发送指令跳转回Bootloader进行升级

## 环境要求

- Python 3.x
- pyserial 库 (`pip install pyserial`)

## 使用方法

### 1. 准备工作

1. 确保STM32开发板已通过USB连接到电脑
2. 确保设备处于Bootloader模式
3. 确认串口号（默认使用COM23，请根据实际情况修改）
4. 准备好要升级的固件文件（.bin格式）

### 2. 运行程序

```bash
python test_bootloader_communication.py
```

### 3. 主菜单选项

程序启动后会显示以下菜单：

```
请选择开始模式:
1. 升级A区固件
2. 直接跳转到A区
3. 用B区覆盖A区并跳转
```

## 详细功能说明

### 选项1: 升级A区固件

1. 程序会自动读取预设路径下的固件文件
2. 计算固件的CRC32校验值
3. 发送启动命令给Bootloader
4. 分块传输固件数据（每块512字节）
5. 每个数据块传输后等待ACK确认
6. 传输完成后等待Bootloader处理完成

### 选项2: 直接跳转到A区

1. 发送跳转命令到Bootloader
2. Bootloader将控制权交给A区的应用程序
3. 设备开始运行A区的应用程序

### 选项3: 用B区覆盖A区并跳转

1. 发送复制命令到Bootloader
2. Bootloader将B区的内容复制到A区
3. 复制完成后跳转到A区运行
4. 设备开始运行从B区复制过来的程序

## 通信协议

### 数据包格式

```
[magic_head][magic_end][cmd][bootloader_state][data(可选)]
```

- `magic_head`: 0xA5 (魔数头)
- `magic_end`: 0x5A (魔数尾)
- `cmd`: 命令码
- `bootloader_state`: 状态码
- `data`: 可选的数据字段

### 命令码定义

| 命令码 | 名称 | 说明 |
|--------|------|------|
| 0x01 | Cmd_Invalid | 无效命令或错误 |
| 0x02 | Cmd_ACK | 确认响应 |
| 0x03 | Cmd_NACK | 否定确认 |
| 0x04 | Cmd_Update_State | 更新状态 |
| 0x05 | Cmd_Jump_To_A | 跳转到A区 |
| 0x06 | Cmd_CopyB_To_A_Jump_To_A | 用B区覆盖A区并跳转 |
| 0x07 | Cmd_Updata_A | 更新A区固件 |

### 状态码定义

| 状态码 | 名称 | 说明 |
|--------|------|------|
| 0x00 | Run_Invalid | 无效状态 |
| 0x01 | Run_Waiting_Cmd | 等待命令 |
| 0x02 | Run_Receiving_Bin | 接收二进制数据 |
| 0x03 | Run_Flash_Erasure | Flash擦除 |
| 0x04 | Run_Flash_Write | Flash写入 |
| 0x05 | Run_Prepare_To_Jump_To_A | 准备跳转到A区 |
| 0x06 | Run_Prepare_To_Jump_To_B | 准备跳转到B区 |

## 故障排除

### 常见问题

1. **无法连接串口**
   - 检查STM32是否正确连接到电脑
   - 确认设备是否处于Bootloader模式
   - 检查USB驱动是否正确安装
   - 确认串口号是否正确（默认COM23）

2. **固件升级失败**
   - 检查固件文件路径是否正确
   - 确认固件文件是否完整有效
   - 检查Bootloader是否正常工作

3. **通信超时**
   - 检查串口参数设置是否正确
   - 确认Bootloader是否正在等待命令

### 断线重连与在线升级

程序具有自动断线重连功能，当检测到串口断开时会自动尝试重连，并询问当前单片机运行区域：

1. **A区运行时**:
   - 程序会询问是否要升级A区固件
   - 如果选择升级，会发送启动指令使单片机跳转回Bootloader模式
   - 然后可以重新选择升级、跳转或其他操作

2. **B区运行时**:
   - 直接提供三个开始模式供选择

这种设计允许在A区应用程序运行状态下远程触发Bootloader模式，实现真正的在线升级功能。

## 注意事项

1. 确保在进行固件升级前设备处于正确的Bootloader模式
2. 固件文件路径在代码中硬编码，如需更改请修改源代码
3. 传输大文件时可能需要较长时间，请耐心等待
4. 串口连接断开后程序会尝试重连，不影响整体操作
5. 在A区应用程序运行时，可通过发送特定指令使其跳转回Bootloader模式进行升级

## 文件路径配置

默认固件文件路径：
```
D:\code_D\stm32_code_MX\My_RemoteDownload\My_APP\MDK-ARM\My_APP\My_APP.bin
```

如需更改，请修改代码中的`bin_file_path`变量。