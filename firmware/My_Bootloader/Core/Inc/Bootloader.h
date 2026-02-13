/**
 * @file Bootloader.h
 * @author here
 * @brief 
 * @version 0.1
 * @date 2026-02-11
 * 
 * @copyright Copyright (c) 2026
 * 
 */
#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include "main.h"
#include <tim.h>
#include "usbd_cdc_if.h"
/* define */
/* f407每个扇区的大小 */
#define SECTOR_SIZE_0  0x4000  // 16KB
#define SECTOR_SIZE_1  0x4000  // 16KB
#define SECTOR_SIZE_2  0x4000  // 16KB
#define SECTOR_SIZE_3  0x4000  // 16KB
#define SECTOR_SIZE_4  0x10000 // 64KB
#define SECTOR_SIZE_5  0x20000 // 128KB
#define SECTOR_SIZE_6  0x20000 // 128KB
#define SECTOR_SIZE_7  0x20000 // 128KB
#define FLASH_TOTAL_SIZE       0x80000     // 512KB
#define STACK_ADDR              0x20000000 // SRAM起始地址(&操作之后得到的值)
#define FLASH_BASE_ADDR 0x08000000 // flash起始地址

// AB分区存储分配
#define BOOTLOADER_START_ADDR   0x08000000  // 扇区0-1 (32KB):  Bootloader区域 (0x0800 0000 ~ 0x0800 7FFF)
#define BOOTLOADER_END_ADDR     0x08007FFF
#define BOOTLOADER_SIZE         0x8000      // 32KB

#define APP_B_START_ADDR        0x08008000  // 扇区2-5 (224KB): B备份区域 (0x0800 8000 ~ 0x0803 FFFF)
#define APP_B_END_ADDR          0x0803FFFF
#define APP_B_SIZE              0x38000     // 224KB

#define APP_A_START_ADDR        0x08040000  // 扇区6-7 (256KB): A运行+更新区域 (0x0804 0000 ~ 0x0807 FFFF)
#define APP_A_END_ADDR          0x0807FFFF
#define APP_A_SIZE              0x40000     // 256KB

#define APP_END_ADDR            (FLASH_BASE_ADDR + FLASH_TOTAL_SIZE - 1)//用户程序结束地址

// 魔数 
#define MAGIC_HEADER    ((uint8_t)0xA5)   // 包头魔数
#define MAGIC_FOOTER    ((uint8_t)0x5A)   // 包尾魔数,，用于验证状态信息有效性
#define PARTITION_STATUS_MAGIC_0  0x424C544C  // "BLTL" 魔数，用于验证状态信息有效性
#define  MAGIC_ACK  0x06           // ACK (ASCII)
#define  MAGIC_NAK  0x15           // NAK (ASCII)


//错误类型
typedef struct 
{
   uint8_t ERROR_Flash_Erasure;
   uint8_t ERROR_Flash_Download;  
   uint8_t ERROR_Copy_B_To_A;
   uint8_t ERROR_Copy_A_To_B;
   uint8_t ERROR_CRC;
   uint8_t ERROR_Jump;
}AllERRORs_t;

//Bootloader开始模式的状态机
typedef struct 
{
   uint8_t Cmd_Which_State;
   uint8_t Cmd_Invalid;  
   uint8_t Cmd_ACK;
   uint8_t Cmd_NACK;
   uint8_t Cmd_Update_State;
   uint8_t Cmd_Jump_To_A;  
   uint8_t Cmd_CopyB_To_A_Jump_To_A;
   uint8_t Cmd_Updata_A;
   AllERRORs_t* AllERRORs;
}AllCmds_t;

//Bootloader开始模式的状态机
typedef struct 
{
   uint8_t Start_Invalid;  
   uint8_t Start_Dirrectly_Jump_To_A;
   uint8_t Start_CopyB_To_A_Jump_To_A;
   uint8_t Start_Updata_A;
}AllBootloaderStartModes_t;

//Bootloader运行状态的状态机
typedef struct 
{
   uint8_t Run_Invalid;  //无效,不改变状态
   uint8_t Run_Waiting_Cmd; //等待命令
   uint8_t Run_Receiving_Bin;//接收bin
   uint8_t Run_Flash_Erasure;  //擦除flash
   uint8_t Run_Flash_Write;  //写入flash
   uint8_t Run_Copy_B_To_A;  //复制B到A
   uint8_t Run_Prepare_To_Jump_To_A;  //准备跳转到A
   uint8_t Run_Prepare_To_Jump_To_B;  //准备跳转到B
   
}AllBootloaderRunStates_t;

//储存实际状态的结构体
typedef struct 
{
   uint8_t StartMode,RunState;
}BootloaderState_t;



//与上位机通信的结构体
typedef struct {
    uint8_t magic_head;//魔数
    uint8_t magic_end;//结束魔数
    uint8_t  cmd;//指令
    uint8_t  BootloaderState;//运行状态
    uint8_t* data;//以\0结尾的其它信息
} BootloaderPacket_t;


//新固件的信息
typedef struct  {
    float Ver;//版本号
    uint32_t Size; //版本大小(字节)
}NewFirmwareInformation_t;

//标准状态定义
extern AllCmds_t AllCMDs;
extern AllBootloaderStartModes_t AllBootloaderStartModes;//Bootloader开始模式的状态机
extern AllBootloaderRunStates_t AllBootloaderRunStates;//Bootloader运行状态的状态机

//实际状态结构体
extern BootloaderState_t BootloaderState;

extern uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];
extern uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];
extern uint16_t rx_read_pos;
extern uint16_t tx_send_pos;

extern uint32_t total_size_to_receive;//准备要接收的bin大小
extern volatile uint16_t received_data_size; // 单次接收到的数据的字节数
extern volatile uint32_t total_received_data_size; // 接收到的数据的总字节数
extern volatile uint16_t written_data_size; // 单次写入到flash的数据的字节数
extern volatile uint32_t total_written_data_size; // 写入到flash的数据的总字节数
extern uint32_t flash_offset; //flash偏移量
extern volatile uint8_t data_buffer[612];
extern uint8_t last_byte_flag; //上一次接收的数据长度是否为奇数，1表示是，0表示否
extern uint8_t last_byte; //上一次剩下的一个字节
extern uint8_t is_data_buffer_full;
extern uint16_t data_buffer_offset;

extern uint8_t is_start_transmission ;//是否开始传输的标志位
extern uint8_t is_transmission_complete ;//传输结束标志位
extern uint8_t is_jump_to_application;//是否跳转到应用程序的标志位
extern uint8_t first_reception; //首次接收标志位
extern volatile uint16_t TimerCounter_ms; //定时器计数（ms）
extern uint32_t expected_crc_value;  // 存储期望的CRC值
extern uint32_t calculated_crc_value;  // 存储计算的CRC值
extern BootloaderPacket_t BootloaderPacket_TX;//发送上位机通信的数据包
extern BootloaderPacket_t BootloaderPacket_RX;//接收到上位机通信的数据包
extern NewFirmwareInformation_t NewFirmwareInformation;

void Bootloader_Init(void);//初始化函数
uint32_t Flash_GetSectorStartAddressByAddress(uint32_t Address);
static uint8_t Flash_GetHalSectorNumber(uint32_t SectorStartAddress);
HAL_StatusTypeDef Flash_EraseSectorByAddress(uint32_t SectorStartAddress);
void JumpToApplication(void);//跳转函数
void JumpToSpecificApplication(uint32_t app_addr);//跳转到指定应用程序
void ResetAll(void);//重置所有相关变量和状态
HAL_StatusTypeDef Download_Flash(uint32_t Size);
HAL_StatusTypeDef FlashErase(uint32_t flash_start_addr, uint32_t Size);

// AB分区管理函数
void LoadPartitionStatus(void);           // 加载分区状态
void SavePartitionStatus(void);           // 保存分区状态
void SwitchToUpdatePartition(void);       // 切换到更新分区
void SwitchToPreviousPartition(void);     // 切换到之前的分区
uint8_t GetCurrentBootPartition(void);    // 获取当前启动分区
uint8_t GetNextUpdatePartition(void);     // 获取下次更新分区
void UpdatePartitionStatus(uint8_t status, uint32_t size, uint32_t crc); // 更新分区状态
uint32_t CalculateCRC32(const uint8_t *data, uint32_t length); // 计算CRC32

// 看门狗相关函数
void FeedIwdg(void);                    // 喂独立看门狗
void CheckAndUpdateWatchdog(void);      // 检查并更新看门狗状态

// 新增函数声明
HAL_StatusTypeDef CopyBtoA(void);                 // 复制B区程序到A区
uint8_t CopyAtoB(void);                 // 复制A区程序到B区（备份）
void UpdateFirmwareVersion(uint32_t version); // 更新固件版本号

HAL_StatusTypeDef Send_BootloaderPacket(uint8_t cmd, uint8_t BootloaderState, uint8_t* data);
void JumpToBootloader(void);//跳转到Bootloader
void JumpToSpecificBootloader(uint32_t Bootloader_addr);

void print_uint32_with_label(const char* label, uint32_t value);
void BootloaderErrorInterFunc(void);
#endif // BOOTLOADER_H


