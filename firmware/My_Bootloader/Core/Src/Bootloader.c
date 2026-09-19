#include "Bootloader.h"
#include "iwdg.h"
#include "main.h"
#include <string.h>
#include "usb_device.h"

extern USBD_HandleTypeDef hUsbDeviceFS;
extern IWDG_HandleTypeDef hiwdg;
extern TIM_HandleTypeDef htim14;
extern UART_HandleTypeDef huart1;

extern AllERRORs_t AllERRORs;
extern AllCmds_t AllCMDs;
extern AllBootloaderStartModes_t AllBootloaderStartModes;
extern AllBootloaderRunStates_t AllBootloaderRunStates;

extern volatile BootloaderState_t BootloaderState;

extern volatile uint16_t TimerCounter_ms;

extern uint32_t total_size_to_receive;
extern volatile uint16_t received_data_size;
extern volatile uint16_t written_data_size;
extern volatile uint32_t total_received_data_size;
extern volatile uint32_t total_written_data_size;
extern uint32_t flash_offset;
extern uint8_t last_byte_flag;
extern uint8_t last_byte;
extern volatile uint8_t data_buffer[612];
extern volatile uint8_t is_data_buffer_full;
extern volatile uint16_t data_buffer_offset;

extern uint8_t is_start_transmission;
extern uint8_t is_transmission_complete;
extern uint8_t is_jump_to_application;
extern uint8_t first_reception;

uint8_t current_boot_partition = 0;
uint8_t next_update_partition = 0;
uint32_t active_app_start_addr = APP_A_START_ADDR;
uint32_t update_target_addr = APP_A_START_ADDR;

void Bootloader_Init(void)
{

    ResetAll();

    TimerCounter_ms = 0;

}

static const uint32_t SectorStartAddresses[] = {
    FLASH_BASE_ADDR,
    FLASH_BASE_ADDR + SECTOR_SIZE_0,
    FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1,
    FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2,
    FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3,
    FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4,
    FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5,
    FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5 + SECTOR_SIZE_6
};

static const uint32_t SectorSizes[] = {
    SECTOR_SIZE_0, SECTOR_SIZE_1, SECTOR_SIZE_2, SECTOR_SIZE_3,
    SECTOR_SIZE_4, SECTOR_SIZE_5, SECTOR_SIZE_6, SECTOR_SIZE_7
};

static const uint32_t HalSectorNumbers[] = {
    FLASH_SECTOR_0, FLASH_SECTOR_1, FLASH_SECTOR_2, FLASH_SECTOR_3,
    FLASH_SECTOR_4, FLASH_SECTOR_5, FLASH_SECTOR_6, FLASH_SECTOR_7
};

uint32_t Flash_GetSectorStartAddressByAddress(uint32_t Address)
{

    if (Address < FLASH_BASE_ADDR ||
        Address >= FLASH_BASE_ADDR + FLASH_TOTAL_SIZE) {
        return 0;
    }

    for (int i = 0; i < 8; i++) {
        uint32_t sector_start = SectorStartAddresses[i];
        uint32_t sector_end = sector_start + SectorSizes[i];

        if (Address >= sector_start && Address < sector_end) {
            return sector_start;

        }
    }

    return 0;
}

static uint8_t Flash_GetHalSectorNumber(uint32_t SectorStartAddress)
{
    for (int i = 0; i < 8; i++) {
        if (SectorStartAddress == SectorStartAddresses[i]) {
            return HalSectorNumbers[i];
        }
    }
    return 0xFF;
}

static HAL_StatusTypeDef Flash_EraseSectorByAddress_NoLock(uint32_t SectorStartAddress)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t SectorError = 0;
    uint32_t SectorNumber;

    FeedIwdg();

    SectorNumber = Flash_GetHalSectorNumber(SectorStartAddress);
    if (SectorNumber == 0xFF) {
        return HAL_ERROR;
    }

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR |
                          FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                          FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    EraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORS;
    EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    EraseInitStruct.Sector = SectorNumber;
    EraseInitStruct.NbSectors = 1;
    EraseInitStruct.Banks = FLASH_BANK_1;

    status = HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError);

    return status;
}

HAL_StatusTypeDef Flash_EraseSectorByAddress(uint32_t SectorStartAddress)
{
    HAL_StatusTypeDef status;

    HAL_FLASH_Unlock();

    status = Flash_EraseSectorByAddress_NoLock(SectorStartAddress);

    HAL_FLASH_Lock();

    return status;
}

void JumpToApplication(void)
{
    JumpToSpecificApplication(active_app_start_addr);
}

void JumpToSpecificApplication(uint32_t app_addr)
{

    typedef void (*pFunction)(void);
    pFunction JumpToApp = (pFunction)(*(__IO uint32_t*)(app_addr + 4));

    uint32_t app_start_address = app_addr;
    uint32_t app_stack_ptr = *(__IO uint32_t*)app_start_address;
    uint32_t jump_address = *(__IO uint32_t*)(app_start_address + 4);

    if ((app_stack_ptr & 0xFFF00000) != STACK_ADDR) {
        Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Jump,AllBootloaderRunStates.Run_Invalid,NULL);
        return;
    }

    if (jump_address < FLASH_BASE_ADDR || jump_address >= APP_END_ADDR) {
        Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Jump,AllBootloaderRunStates.Run_Invalid,NULL);
        return;
    }

    __disable_irq();

    HAL_DeInit();

    SCB->VTOR = app_start_address & 0x1FFFFF8;

    __set_MSP(*(__IO uint32_t*)app_start_address);

    JumpToApp();
}

void JumpToBootloader(void)
{

    while (IWDG->SR & IWDG_SR_PVU);
    while (IWDG->SR & IWDG_SR_RVU);

    IWDG->KR = 0x5555;

    while (IWDG->SR & IWDG_SR_PVU);
    IWDG->PR = 0;

    while (IWDG->SR & IWDG_SR_RVU);
    IWDG->RLR = 0;

    IWDG->KR = 0xAAAA;

    while(1) {
        __NOP();
    }
}

void  ResetAll(void)
{

     rx_read_pos = 0;
     tx_send_pos = 0;

    BootloaderState.StartMode=AllBootloaderStartModes.Start_Invalid;
    BootloaderState.RunState=AllBootloaderRunStates.Run_Waiting_Cmd;

    is_start_transmission = 0;
    is_transmission_complete = 0;
    first_reception =1;

    is_data_buffer_full = 0;
    data_buffer_offset = 0;
    received_data_size = 0;

    HAL_TIM_Base_Stop(&htim14);
    TimerCounter_ms=0;

    flash_offset =0;
    data_buffer_offset=0;
    last_byte_flag =0;
    received_data_size =0;
    written_data_size =0;
    total_received_data_size =0;
    total_written_data_size =0;
    memset((void*)data_buffer, 0, 256);

     memset((void*)UserRxBufferFS, '\0', APP_RX_DATA_SIZE);
     memset((void*)UserTxBufferFS, '\0', APP_TX_DATA_SIZE);
     USBD_CDC_ReceivePacket(&hUsbDeviceFS);

}

HAL_StatusTypeDef Download_Flash(uint32_t Size)
{

      received_data_size = Size;

      total_received_data_size += Size;

      HAL_FLASH_Unlock();

      if(last_byte_flag == 1)
      {

          uint16_t first_halfword;
          if(Size >=1)
          {

              first_halfword = (data_buffer[0] << 8) | last_byte;
              if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, update_target_addr + flash_offset, first_halfword) != HAL_OK)
              {
                  Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Flash_Download,AllBootloaderRunStates.Run_Invalid,NULL);
                  HAL_FLASH_Lock();
                  return HAL_ERROR;
              }
              flash_offset +=2;
          }

          for(uint16_t i=1; i<Size; i+=2)
          {
              FeedIwdg();
              uint32_t write_addr = update_target_addr + flash_offset;
              uint16_t halfword ;
              if(i+1 < Size)
              {

                  halfword = (data_buffer[i+1] << 8) | data_buffer[i];
                  if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, write_addr, halfword) != HAL_OK)
                  {

                      HAL_FLASH_Lock();
                      return HAL_ERROR;
                  }
                  flash_offset +=2;
              }
              else
              {

                  last_byte = data_buffer[i];
                  last_byte_flag = 1;
              }
          }
          if(Size %2 ==1)
          {
              written_data_size = Size +1;
              total_written_data_size += Size +1;
              last_byte_flag =0;
          }
          else
          {
              written_data_size = Size;
              total_written_data_size += Size;
              last_byte_flag =1;
          }
      }
      else
      {

          for(uint16_t i=0; i<Size; i+=2)
          {
              FeedIwdg();
              uint32_t write_addr = update_target_addr + flash_offset;
              uint16_t halfword ;
              if(i+1 < Size)
              {

                  halfword = (data_buffer[i+1] << 8) | data_buffer[i];
                  if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, write_addr, halfword) != HAL_OK)
                  {

                      HAL_FLASH_Lock();
                      return HAL_ERROR;
                  }
                  flash_offset +=2;
              }
              else
              {

                  last_byte = data_buffer[i];
                  last_byte_flag = 1;
              }
          }
          if(Size %2 ==0)
          {
              written_data_size = Size;
              total_written_data_size += Size;
              last_byte_flag =0;
          }
          else
          {
              written_data_size = Size -1;
              total_written_data_size += Size -1;
              last_byte_flag =1;
          }
      }

      HAL_FLASH_Lock();
      is_data_buffer_full=0;
      data_buffer_offset=0;

    return HAL_OK;

}

HAL_StatusTypeDef FlashErase(uint32_t flash_start_addr, uint32_t Size)
{
    if (Size == 0) {
        return HAL_OK;
    }

    HAL_FLASH_Unlock();

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR |
                          FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                          FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    FeedIwdg();

    if (flash_start_addr >= APP_A_START_ADDR && flash_start_addr <= APP_A_END_ADDR) {

        uint32_t sector6_addr = APP_A_START_ADDR;
        uint32_t *sector6_start = (uint32_t *)sector6_addr;
        uint8_t needs_erase_sector6 = 0;

        for (int i = 0; i < 64 && i < SECTOR_SIZE_6 / 4; i++) {
            if (sector6_start[i] != 0xFFFFFFFF) {
                needs_erase_sector6 = 1;
                break;
            }
        }

        if (needs_erase_sector6) {
            Flash_EraseSectorByAddress_NoLock(sector6_addr);
            FeedIwdg();
        }

        uint32_t sector7_addr = APP_A_START_ADDR + SECTOR_SIZE_6;
        uint32_t *sector7_start = (uint32_t *)sector7_addr;
        uint8_t needs_erase_sector7 = 0;

        for (int i = 0; i < 64 && i < SECTOR_SIZE_7 / 4; i++) {
            if (sector7_start[i] != 0xFFFFFFFF) {
                needs_erase_sector7 = 1;
                break;
            }
        }

        if (needs_erase_sector7) {
            Flash_EraseSectorByAddress_NoLock(sector7_addr);
            FeedIwdg();
        }
    }

    else if (flash_start_addr >= APP_B_START_ADDR && flash_start_addr <= APP_B_END_ADDR) {

        uint32_t sector2_addr = APP_B_START_ADDR;
        uint32_t *sector2_start = (uint32_t *)sector2_addr;
        uint8_t needs_erase_sector2 = 0;

        for (int i = 0; i < 64 && i < SECTOR_SIZE_2 / 4; i++) {
            if (sector2_start[i] != 0xFFFFFFFF) {
                needs_erase_sector2 = 1;
                break;
            }
        }

        if (needs_erase_sector2) {
            Flash_EraseSectorByAddress_NoLock(sector2_addr);
            FeedIwdg();
        }

        uint32_t sector3_addr = APP_B_START_ADDR + SECTOR_SIZE_2;
        uint32_t *sector3_start = (uint32_t *)sector3_addr;
        uint8_t needs_erase_sector3 = 0;

        for (int i = 0; i < 64 && i < SECTOR_SIZE_3 / 4; i++) {
            if (sector3_start[i] != 0xFFFFFFFF) {
                needs_erase_sector3 = 1;
                break;
            }
        }

        if (needs_erase_sector3) {
            Flash_EraseSectorByAddress_NoLock(sector3_addr);
            FeedIwdg();
        }

        uint32_t sector4_addr = APP_B_START_ADDR + SECTOR_SIZE_2 + SECTOR_SIZE_3;
        uint32_t *sector4_start = (uint32_t *)sector4_addr;
        uint8_t needs_erase_sector4 = 0;

        for (int i = 0; i < 64 && i < SECTOR_SIZE_4 / 4; i++) {
            if (sector4_start[i] != 0xFFFFFFFF) {
                needs_erase_sector4 = 1;
                break;
            }
        }

        if (needs_erase_sector4) {
            Flash_EraseSectorByAddress_NoLock(sector4_addr);
            FeedIwdg();
        }

        uint32_t sector5_addr = APP_B_START_ADDR + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4;
        uint32_t *sector5_start = (uint32_t *)sector5_addr;
        uint8_t needs_erase_sector5 = 0;

        for (int i = 0; i < 64 && i < SECTOR_SIZE_5 / 4; i++) {
            if (sector5_start[i] != 0xFFFFFFFF) {
                needs_erase_sector5 = 1;
                break;
            }
        }

        if (needs_erase_sector5) {
            Flash_EraseSectorByAddress_NoLock(sector5_addr);
            FeedIwdg();
        }
    }

    else {

        uint32_t start_sector_addr = Flash_GetSectorStartAddressByAddress(flash_start_addr);
        uint32_t end_addr = flash_start_addr + Size - 1;
        uint32_t end_sector_addr = Flash_GetSectorStartAddressByAddress(end_addr);

        uint32_t current_addr = start_sector_addr;
        while (current_addr <= end_sector_addr && current_addr != 0) {
            FeedIwdg();

            uint8_t needs_erase = 0;
            uint32_t *sector_start = (uint32_t *)current_addr;

            uint32_t sector_size = 0;
            if (current_addr == FLASH_BASE_ADDR) sector_size = SECTOR_SIZE_0;
            else if (current_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0) sector_size = SECTOR_SIZE_1;
            else if (current_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1) sector_size = SECTOR_SIZE_2;
            else if (current_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2) sector_size = SECTOR_SIZE_3;
            else if (current_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3) sector_size = SECTOR_SIZE_4;
            else if (current_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4) sector_size = SECTOR_SIZE_5;
            else if (current_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5) sector_size = SECTOR_SIZE_6;
            else if (current_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5 + SECTOR_SIZE_6) sector_size = SECTOR_SIZE_7;

            if (sector_size > 0) {

                uint32_t max_words_to_check = (sector_size < 256) ? sector_size / 4 : 64;

                for (int i = 0; i < max_words_to_check; i++) {
                    if (sector_start[i] != 0xFFFFFFFF) {
                        needs_erase = 1;
                        break;
                    }
                }

                if (needs_erase) {
                    Flash_EraseSectorByAddress_NoLock(current_addr);
                    FeedIwdg();
                }
            }

            if (current_addr == FLASH_BASE_ADDR) current_addr = FLASH_BASE_ADDR + SECTOR_SIZE_0;
            else if (current_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0) current_addr = FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1;
            else if (current_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1) current_addr = FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2;
            else if (current_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2) current_addr = FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3;
            else if (current_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3) current_addr = FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4;
            else if (current_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4) current_addr = FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5;
            else if (current_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5) current_addr = FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5 + SECTOR_SIZE_6;
            else if (current_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5 + SECTOR_SIZE_6) current_addr = FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5 + SECTOR_SIZE_6 + SECTOR_SIZE_7;
            else break;
        }
    }

    HAL_FLASH_Lock();
    return HAL_OK;
}

HAL_StatusTypeDef CopyBtoA(void)
{
    uint32_t src_addr = APP_B_START_ADDR;
    uint32_t dst_addr = APP_A_START_ADDR;
    uint32_t size = APP_B_SIZE < APP_A_SIZE ? APP_B_SIZE : APP_A_SIZE;

    if (FlashErase(dst_addr, size) != HAL_OK) {
        Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Flash_Erasure,AllBootloaderRunStates.Run_Invalid,NULL);
        return HAL_ERROR;
    }

    HAL_FLASH_Unlock();

    for (uint32_t offset = 0; offset < size; offset += 4) {
        uint32_t data = *(volatile uint32_t*)(src_addr + offset);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, dst_addr + offset, data) != HAL_OK) {
            Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Flash_Download,AllBootloaderRunStates.Run_Invalid,NULL);
            HAL_FLASH_Lock();
            return HAL_ERROR;
        }
    }

    HAL_FLASH_Lock();

    uint32_t crc_b = CalculateCRC32((const uint8_t*)src_addr, size);
    uint32_t crc_a = CalculateCRC32((const uint8_t*)dst_addr, size);

    print_uint32_with_label("crc_a",crc_a);
    print_uint32_with_label("crc_b",crc_b);

    if(crc_b != crc_a) {

        uint8_t crc_data[4];
        crc_data[0] = crc_b & 0xFF;
        crc_data[1] = (crc_b >> 8) & 0xFF;
        crc_data[2] = (crc_b >> 16) & 0xFF;
        crc_data[3] = (crc_b >> 24) & 0xFF;
        Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_CRC,AllBootloaderRunStates.Run_Invalid,crc_data);
        BootloaderErrorInterFunc();
        return HAL_ERROR;
    }

    return HAL_OK;
}

uint8_t CopyAtoB(void)
 {

     return 0;
}

uint32_t CalculateCRC32(const uint8_t *data, uint32_t length)
{

    uint32_t crc = 0xFFFFFFFF;
    uint32_t i, j;

    for (i = 0; i < length; i++)
    {

        crc ^= data[i];

        for (j = 0; j < 8; j++)
        {

            if (crc & 1)
            {
                crc = (crc >> 1) ^ 0xEDB88320;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc ^ 0xFFFFFFFF;
}

void FeedIwdg(void)
{
    HAL_IWDG_Refresh(&hiwdg);
}

void CheckAndUpdateWatchdog(void)
{

    HAL_IWDG_Refresh(&hiwdg);
}

HAL_StatusTypeDef Send_BootloaderPacket(uint8_t cmd, uint8_t BootloaderState, uint8_t* data)
{
    static uint8_t packet_buffer[16];
    uint8_t packet_len = 4;

    packet_buffer[0] = MAGIC_HEADER;
    packet_buffer[1] = MAGIC_FOOTER;

    packet_buffer[2] = cmd;
    packet_buffer[3] = BootloaderState;

    if (data != NULL) {

        packet_buffer[4] = data[0];
        packet_buffer[5] = data[1];
        packet_buffer[6] = data[2];
        packet_buffer[7] = data[3];
        packet_len += 4;
    }

    uint32_t start_tick = HAL_GetTick();
    while (CDC_Transmit_FS((uint8_t*)packet_buffer, packet_len) == USBD_BUSY)
    {

        if (HAL_GetTick() - start_tick > 1000)
        {
            return HAL_TIMEOUT;
        }
    }

    return HAL_OK;
}

HAL_StatusTypeDef Parse_ReceivedPacket(uint8_t* ReceivedPacket)
{
   return HAL_OK;
}

extern UART_HandleTypeDef huart1;
void print_uint32_with_label(const char* label, uint32_t value)
{
    char buffer[64];
    int len = sprintf(buffer, "[%s] Value = %lu (0x%08lX)\r\n", label, value, value);
    HAL_UART_Transmit(&huart1, (uint8_t*)buffer, len, HAL_MAX_DELAY);
}

void BootloaderErrorInterFunc(void){

    HAL_UART_Transmit(&huart1,(uint8_t*)"[FATAL] Bootloader Error! Resetting...\r\n",40,100);

    HAL_Delay(500);

    HAL_NVIC_SystemReset();
}

