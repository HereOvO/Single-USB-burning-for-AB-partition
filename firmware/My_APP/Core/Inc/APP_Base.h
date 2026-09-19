#ifndef __APP_BASE_H
#define __APP_BASE_H

#include "main.h"
#include "cmsis_os.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"
#include <string.h>
#include "stm32f4xx_hal.h"
#include "core_cm4.h"  // 包含SCB定义

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
#define BOOTLOADER_START_ADDR   0x08000000  // 扇区0-1 (32KB): Bootloader区域
#define BOOTLOADER_END_ADDR     0x08007FFF
#define BOOTLOADER_SIZE         0x8000      // 32KB

#define APP_B_START_ADDR        0x08008000  // 扇区2-5 (224KB): B备份区域
#define APP_B_END_ADDR          0x0803FFFF
#define APP_B_SIZE              0x38000     // 224KB

#define APP_A_START_ADDR        0x08040000  // 扇区6-7 (256KB): A运行+更新区域
#define APP_A_END_ADDR          0x0807FFFF
#define APP_A_SIZE              0x40000     // 256KB

#define APP_END_ADDR            (FLASH_BASE_ADDR + FLASH_TOTAL_SIZE - 1)//用户程序结束地址

// 魔数
#define MAGIC_HEADER    ((uint8_t)0xA5)   // 包头魔数
#define MAGIC_FOOTER    ((uint8_t)0x5A)   // 包尾魔数，用于验证状态信息有效性
#define PARTITION_STATUS_MAGIC_0  0x424C544C  // "BLTL" 魔数
#define  MAGIC_ACK  0x06           // ACK
#define  MAGIC_NAK  0x15           // NAK

// 错误类型
typedef struct
{
   uint8_t ERROR_Flash_Erasure;
   uint8_t ERROR_Flash_Download;
   uint8_t ERROR_Copy_B_To_A;
   uint8_t ERROR_Copy_A_To_B;
   uint8_t ERROR_CRC;
   uint8_t ERROR_Jump;
}AllERRORs_t;

// Bootloader命令
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

// Bootloader开始模式
typedef struct
{
   uint8_t Start_Invalid;
   uint8_t Start_Dirrectly_Jump_To_A;
   uint8_t Start_CopyB_To_A_Jump_To_A;
   uint8_t Start_Updata_A;
}AllBootloaderStartModes_t;

// Bootloader运行状态
typedef struct
{
   uint8_t Run_Invalid;  // 无效，不改变状态
   uint8_t Run_Waiting_Cmd; // 等待命令
   uint8_t Run_Receiving_Bin; // 接收bin
   uint8_t Run_Flash_Erasure; // 擦除flash
   uint8_t Run_Flash_Write; // 写入flash
   uint8_t Run_Copy_B_To_A; // 复制B到A
   uint8_t Run_Prepare_To_Jump_To_A; // 准备跳转到A
   uint8_t Run_Prepare_To_Jump_To_B; // 准备跳转到B

}AllBootloaderRunStates_t;

// 实际状态
typedef struct
{
   uint8_t StartMode,RunState;
}BootloaderState_t;

// 与上位机通信的数据包
typedef struct {
    uint8_t magic_head; // 魔数
    uint8_t magic_end; // 结束魔数
    uint8_t  cmd; // 指令
    uint8_t  BootloaderState; // 运行状态
    uint8_t* data; // 其它信息
} BootloaderPacket_t;

// 新固件信息
typedef struct  {
    float Ver; // 版本号
    uint32_t Size; // 版本大小（字节）
}NewFirmwareInformation_t;

// 全局状态定义
extern AllCmds_t AllCMDs;
extern AllBootloaderStartModes_t AllBootloaderStartModes; // Bootloader开始模式
extern AllBootloaderRunStates_t AllBootloaderRunStates; // Bootloader运行状态

uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len);

HAL_StatusTypeDef Send_BootloaderPacket(uint8_t cmd, uint8_t BootloaderState, uint8_t* data);
void StartFeedIWDG(void);
HAL_StatusTypeDef CopyAtoB(void);
void FeedIWDG(void);
void BootloaderErrorInterFunc(void);
void JumpToBootloader(void);
HAL_StatusTypeDef FlashErase(uint32_t flash_start_addr, uint32_t Size);
HAL_StatusTypeDef Flash_EraseSectorByAddress(uint32_t SectorStartAddress);
static uint8_t Flash_GetHalSectorNumber(uint32_t SectorStartAddress);
uint32_t Flash_GetSectorStartAddressByAddress(uint32_t Address);
void ResetAll(void);
uint32_t CalculateCRC32(const uint8_t *data, uint32_t length);
void print_uint32_with_label(const char* label, uint32_t value);
HAL_StatusTypeDef FlashEraseAllSectors(uint32_t start_addr, uint32_t size);
uint32_t DetectActualProgramSize(uint32_t src_addr, uint32_t max_size);
void MonitorBootloaderCMD(uint8_t* pBuf);
#endif   // __APP_BASE_H

