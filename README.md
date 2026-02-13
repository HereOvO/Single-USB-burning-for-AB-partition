# STM32F407 双分区Bootloader与应用程序

基于STM32F407的双分区Bootloader系统，包含Bootloader和配套应用程序，支持通过USB CDC接口进行安全可靠的固件升级。

## 项目概述

本项目是一个完整的STM32F407固件升级解决方案，包含：
- **Bootloader**: 引导加载程序，负责固件升级和程序跳转
- **Application**: 主应用程序，与Bootloader配合实现远程升级

## 特性

### Bootloader特性
- **双分区设计**：A区为运行/更新区，B区为备份区
- **USB CDC全双工通信**：支持实时双向通信
- **智能Flash管理**：仅擦除非FF扇区，提高效率
- **CRC32校验**：确保固件完整性
- **状态机控制**：清晰的状态转换逻辑
- **看门狗集成**：防止程序卡死
- **超时处理机制**：接收bin文件时3秒超时处理，未接命令时15秒自动跳转
- **错误处理机制**：完善的错误反馈

### Application特性
- **自动备份**：启动时自动备份到B区(必须)
- **远程升级响应**：可响应升级请求跳转到Bootloader(必须)
- **看门狗管理**：持续喂狗保证系统稳定(必须)
- **状态监控**：监控系统运行状态

## 硬件要求

- STM32F407VET6
- 512KB Flash
- 192KB SRAM
- USB接口

## Flash分区布局

| 分区 | 起始地址 | 结束地址 | 大小 | 用途 |
|------|----------|----------|------|------|
| Bootloader | 0x08000000 | 0x08007FFF | 32KB | Bootloader程序 |
| B区(备份) | 0x08008000 | 0x0803FFFF | 224KB | B备份区域 |
| A区(运行+更新) | 0x08040000 | 0x0807FFFF | 256KB | A运行+更新区域 |

## 通信协议

### 4.1 数据包格式

```
[魔数头(1字节)][魔数尾(1字节)][命令(1字节)][状态(1字节)][数据(...字节)]
```

- **魔数头**: `0xA5`
- **魔数尾**: `0x5A`
- **命令**: 指令码
- **状态**: 当前运行状态
- **数据**: 可选数据字段

### 4.2 命令码定义

| 命令码 | 十六进制 | 描述 |
|--------|----------|------|
| Cmd_Invalid | 0x01 | 无效命令/错误响应 |
| Cmd_ACK | 0x02 | 确认响应 |
| Cmd_NACK | 0x03 | 否认响应 |
| Cmd_Update_State | 0x04 | 更新状态 |
| Cmd_Jump_To_A | 0x05 | 跳转到A区 |
| Cmd_CopyB_To_A_Jump_To_A | 0x06 | 用B区覆盖A区并跳转 |
| Cmd_Updata_A | 0x07 | 升级A区固件 |

### 4.3 错误码定义

| 错误码 | 十六进制 | 描述 |
|--------|----------|------|
| ERROR_Flash_Erasure | 0x01 | Flash擦除错误 |
| ERROR_Flash_Download | 0x02 | Flash下载错误 |
| ERROR_Copy_B_To_A | 0x03 | B区复制到A区错误 |
| ERROR_Copy_A_To_B | 0x04 | A区复制到B区错误 |
| ERROR_Jump | 0x05 | 跳转错误 |
| ERROR_CRC | 0x06 | CRC校验错误 |

### 4.4 错误响应格式

当发生错误时，Bootloader发送错误响应包：
- Flash下载错误：`[0xA5][0x5A][0x01][0x00][NULL]` (Cmd=ERROR_Flash_Download, State=Run_Invalid)
- Flash擦除错误：`[0xA5][0x5A][0x01][0x00][NULL]` (Cmd=ERROR_Flash_Erasure, State=Run_Invalid)
- 跳转错误：`[0xA5][0x5A][0x01][0x00][NULL]` (Cmd=ERROR_Jump, State=Run_Invalid)
- CRC校验错误：`[0xA5][0x5A][0x01][0x00][CRC值(4字节LE)]` (Cmd=ERROR_CRC, State=Run_Invalid, Data=CRC值)

## 工作流程

### 固件升级流程

1. **启动命令**：
   - 上位机发送：`[0xA5][0x5A][0x07][0x00][固件大小(4字节LE)][CRC32值(4字节LE)]`
   - 其中 `[0x07]` 表示 `Cmd_Updata_A` 命令
   - 固件大小为32位小端序整数
   - CRC32值为固件数据的CRC32校验值（32位小端序整数）

2. **Flash擦除**：
   - Bootloader接收到启动命令后，开始擦除A区Flash
   - 擦除完成后发送ACK：`[0xA5][0x5A][0x02][0x00][NULL]`
   - 状态从 `Run_Flash_Erasure` 转换到 `Run_Receiving_Bin`

3. **数据传输**：
   - 上位机连续发送固件数据块
   - 每次数据块大小为512字节（建议大小,如果需要修改,请调整USB接收部分）
   - 每次接收到数据时，Bootloader会重置超时计数器(`TimerCounter_ms=0`)
   - 数据被缓存到`data_buffer`中，当累积达到512字节时触发Flash写入

4. **数据写入**：
   - 当缓冲区满512字节时，状态从 `Run_Receiving_Bin` 转换到 `Run_Flash_Write`
   - Bootloader将数据写入Flash
   - 写入成功后发送ACK：`[0xA5][0x5A][0x02][0x00][NULL]`，状态回到 `Run_Receiving_Bin`
   - 写入失败发送错误响应：`[0xA5][0x5A][0x01][0x00][NULL]` (ERROR_Flash_Download)，进入`BootloaderErrorInterFunc()`错误函数

5. **传输完成与CRC校验**：
   - 3秒无数据传输后，进入超时处理：
     - 如有剩余数据（`data_buffer_offset != 0`），则写入Flash
     - 写入完成后，计算A区固件的CRC32值
     - 将计算得到的CRC32值与启动命令中接收到的预期CRC32值进行比较
     - 如果CRC32值匹配，发送状态更新：`[0xA5][0x5A][0x04][0x05][NULL]`，状态转换到 `Run_Prepare_To_Jump_To_A`，跳转到A区应用程序
     - 如果CRC32值不匹配，发送错误响应：`[0xA5][0x5A][0x01][0x00][预期CRC32值(4字节LE)]` (ERROR_CRC)，进入错误处理函数

### 应用程序备份流程

1. **启动时**：应用程序自动将自身备份到B区
2. **持续运行**：应用程序正常执行业务逻辑
3. **升级响应**：接收到升级指令时跳转到Bootloader

## 状态机

### 5.1 开始模式状态机 (AllBootloaderStartModes_t)

| 状态 | 值 | 描述 |
|------|----|------|
| Start_Invalid | 0x00 | 无效状态，等待命令 |
| Start_Dirrectly_Jump_To_A | 0x01 | 直接跳转到A区 |
| Start_CopyB_To_A_Jump_To_A | 0x02 | 用B区覆盖A区并跳转 |
| Start_Updata_A | 0x03 | 升级A区固件 |

### 5.2 运行状态状态机 (AllBootloaderRunStates_t)

| 状态 | 值 | 描述 |
|------|----|------|
| Run_Invalid | 0x00 | 无效状态 |
| Run_Waiting_Cmd | 0x01 | 等待命令 |
| Run_Receiving_Bin | 0x02 | 接收bin文件 |
| Run_Flash_Erasure | 0x03 | Flash擦除 |
| Run_Flash_Write | 0x04 | Flash写入 |
| Run_Prepare_To_Jump_To_A | 0x05 | 准备跳转到A区 |
| Run_Prepare_To_Jump_To_B | 0x06 | 准备跳转到B区 |

### 5.3 状态转换图

```
[系统启动] -> [Start_Invalid, Run_Waiting_Cmd]
     ↓
[接收Cmd_Updata_A] -> [Start_Updata_A, Run_Flash_Erasure]
     ↓
[Flash擦除完成] -> [Start_Updata_A, Run_Receiving_Bin]
     ↓
[接收数据] -> [Start_Updata_A, Run_Receiving_Bin]
     ↓
[缓冲区满512字节] -> [Start_Updata_A, Run_Flash_Write]
     ↓
[Flash写入完成] -> [Start_Updata_A, Run_Receiving_Bin]
     ↓
[3秒超时且有剩余数据] -> [写入剩余数据] -> [Start_Updata_A, Run_Prepare_To_Jump_To_A]
     ↓
[3秒超时无剩余数据] -> [Start_Updata_A, Run_Prepare_To_Jump_To_A]
     ↓
[跳转到A区应用程序]
```

### 5.4 超时处理机制

在固件升级过程中，Bootloader使用3秒超时机制来处理数据传输结束：

1. **数据接收阶段**：每当接收到数据时，`TimerCounter_ms`被重置为0
2. **超时检测**：当`TimerCounter_ms > 3000`时触发超时处理
3. **剩余数据处理**：
   - 检查`data_buffer_offset`是否非零
   - 如果有剩余数据（`data_buffer_offset != 0`），则执行最后一次Flash写入
   - 写入成功后发送ACK，失败则发送错误响应
4. **CRC校验**：计算A区固件的CRC32值并与预期值比较
   - 如果CRC校验成功，状态设置为`Run_Prepare_To_Jump_To_A`，跳转到A区应用程序
   - 如果CRC校验失败，发送错误响应并进入错误处理函数
5. **跳转执行**：发送状态更新包后跳转到A区应用程序

另外，如果Bootloader启动后15秒内未接收到上位机命令，会自动跳转到A区应用程序。

## 特殊功能

### 6.1 超时机制
- 数据接收超时时间为3秒
- 每次接收到数据时重置超时计数器
- 超时后检查是否存在未写入的剩余数据
- 如有剩余数据则将其写入Flash，然后进行CRC校验
- CRC校验通过后跳转到应用程序
- 未接收到上位机命令时，15秒后自动跳转到A区

### 6.2 数据对齐处理
- 支持奇数长度数据的正确写入Flash
- 自动处理字节对齐问题

### 6.3 错误处理
- Flash擦除错误检测
- Flash写入错误检测
- 跳转地址验证
- BootloaderErrorInterFunc()错误处理函数

### 6.4 Flash擦除机制
- 写入A区前，检查A区每个扇区（扇区6和7）的前256字节，非全FF则擦除该扇区
- 写入B区前，检查B区每个扇区（扇区2, 3, 4, 5）的前256字节，非全FF则擦除该扇区
- 确保写入前目标区域都是空的（全FF状态）
- 避免重复擦除，提高效率
- 其他区域按需检查并擦除对应扇区

### 6.5 CRC校验机制
- 固件升级时，上位机计算固件的CRC32值并随启动命令发送
- 下位机接收完所有数据后计算A区的CRC32值并与预期值比较
- B区覆盖A区时，复制完成后计算A区和B区的CRC32值进行比较
- CRC校验失败时发送错误响应并进入错误处理函数

## 使用方法

### 7.1 固件升级
1. 连接USB线到目标板
2. 发送启动命令 `[0xA5][0x5A][0x07][0x00][固件大小(4字节LE)][CRC32值(4字节LE)]`
3. 等待ACK响应
4. 连续发送固件数据
5. 等待自动跳转或手动跳转
6. 系统将在传输完成后进行CRC校验，校验通过后自动跳转到应用程序

### 7.2 直接跳转
1. 发送跳转命令 `[0xA5][0x5A][0x04][0x00][NULL]`
2. 等待跳转到A区应用程序

### 7.3 用B区覆盖A区并跳转
1. 发送命令 `[0xA5][0x5A][0x06][0x00][NULL]`
2. Bootloader将B区内容复制到A区
3. 复制完成后进行CRC校验
4. 校验通过后跳转到A区应用程序

### 7.4 应用程序备份
应用程序启动时会自动将自身备份到B区，确保有可用的备份固件。

## 配置说明

- UART1和PB12用于调试，在实际工程中可以注释掉
- 任务状态判断交由上位机处理
- USB CDC通信支持全双工操作

## 注意事项

1. 确保固件大小不超过A区容量（256KB）
2. 固件必须是有效的二进制格式(bin)
3. 传输过程中不要断开USB连接
4. 升级失败时可使用B区恢复功能
5. 保持足够的供电以避免升级过程中的意外断电
6. CRC校验确保数据完整性，校验失败时系统将进入错误处理状态
7. 启动命令中包含固件大小和CRC32值，确保计算准确
8. Bootloader启动后15秒内未接收到命令会自动跳转到A区
9. 接收bin文件时3秒无数据传输会触发超时处理

## 故障排除

### 9.1 无法进入Bootloader
- 检查硬件连接
- 确认Bootloader是否正确烧录

### 9.2 升级失败
- 检查固件大小是否超出限制
- 验证固件格式是否正确
- 确认USB连接稳定
- 检查CRC32值计算是否正确
- 确保传输过程中数据未损坏

### 9.3 跳转失败
- 检查应用程序向量表是否正确
- 验证Flash写入是否完整

---

## 许可证

该项目遵循MIT许可证。

