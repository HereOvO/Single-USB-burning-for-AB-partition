#include "app_base.h"

AllERRORs_t AllERRORs = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
AllCmds_t AllCMDs = {0, 1, 2, 3, 4, 5, 6, 7, &AllERRORs};
AllBootloaderStartModes_t AllBootloaderStartModes = {0, 1, 2, 3};
AllBootloaderRunStates_t AllBootloaderRunStates = {0, 1, 2, 3, 4, 5, 6, 7};

BootloaderState_t BootloaderState;

extern UART_HandleTypeDef huart1;

void StartFeedIWDG(void){
    while(1){
        IWDG->KR = 0xAAAA;
        osDelay(5000);
    }
}

void FeedIWDG(void){
    IWDG->KR = 0xAAAA;
}

HAL_StatusTypeDef Send_BootloaderPacket(uint8_t cmd, uint8_t BootloaderState, uint8_t* data)
{
    uint8_t packet_buffer[16];
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

    if (CDC_Transmit_FS((uint8_t*)packet_buffer, packet_len) == USBD_OK)
    {
        return HAL_OK;
    }
    else
    {
        return HAL_ERROR;
    }
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

HAL_StatusTypeDef CopyAtoB(void)
{   extern UART_HandleTypeDef huart1;

    uint32_t src_addr = APP_A_START_ADDR;
    uint32_t dst_addr = APP_B_START_ADDR;
    uint32_t size = APP_A_SIZE < APP_B_SIZE ? APP_A_SIZE : APP_B_SIZE;

    if (size < 0x1000) {
        Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Copy_A_To_B,AllBootloaderRunStates.Run_Invalid,NULL);

        return HAL_ERROR;
    }

    FeedIWDG();

    if (FlashEraseAllSectors(APP_B_START_ADDR, APP_B_SIZE) != HAL_OK) {
        Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Copy_A_To_B,AllBootloaderRunStates.Run_Invalid,NULL);

        return HAL_ERROR;
    }

    HAL_FLASH_Unlock();

    FeedIWDG();

    uint32_t actual_program_size = DetectActualProgramSize(src_addr, size);

    HAL_FLASH_Unlock();

    uint32_t offset = 0;
    while (offset < actual_program_size) {

        uint32_t current_sector_start = Flash_GetSectorStartAddressByAddress(dst_addr + offset);
        uint32_t current_sector_size = 0;

        if (current_sector_start == FLASH_BASE_ADDR) {
            current_sector_size = SECTOR_SIZE_0;
        } else if (current_sector_start == FLASH_BASE_ADDR + SECTOR_SIZE_0) {
            current_sector_size = SECTOR_SIZE_1;
        } else if (current_sector_start == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1) {
            current_sector_size = SECTOR_SIZE_2;
        } else if (current_sector_start == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2) {
            current_sector_size = SECTOR_SIZE_3;
        } else if (current_sector_start == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3) {
            current_sector_size = SECTOR_SIZE_4;
        } else if (current_sector_start == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4) {
            current_sector_size = SECTOR_SIZE_5;
        } else if (current_sector_start == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5) {
            current_sector_size = SECTOR_SIZE_6;
        } else if (current_sector_start == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5 + SECTOR_SIZE_6) {
            current_sector_size = SECTOR_SIZE_7;
        }

        uint32_t current_sector_end = current_sector_start + current_sector_size;

        uint32_t sector_remaining = current_sector_end - (dst_addr + offset);
        uint32_t chunk_size = (actual_program_size - offset) < sector_remaining ? (actual_program_size - offset) : sector_remaining;

        if (offset + chunk_size > actual_program_size) {
            chunk_size = actual_program_size - offset;
        }

        chunk_size = (chunk_size / 4) * 4;

        if (chunk_size > 0) {

            uint32_t src_chunk_addr = src_addr + offset;
            uint32_t dst_chunk_addr = dst_addr + offset;
            uint32_t word_count = chunk_size / 4;

            for (uint32_t i = 0; i < word_count; i++) {
                uint32_t data = *(volatile uint32_t*)(src_chunk_addr + i * 4);

                HAL_StatusTypeDef status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, dst_chunk_addr + i * 4, data);

                if (status != HAL_OK) {

                    char error_msg[64];
                    int len = sprintf(error_msg, "Flash programming failed at offset 0x%08lX, error: 0x%08lX\r\n",
                                      offset + i * 4, HAL_FLASH_GetError());

                    Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Copy_A_To_B,AllBootloaderRunStates.Run_Invalid,NULL);
                    HAL_FLASH_Lock();
                    return HAL_ERROR;
                }

                if ((i % 100) == 0) {
                    FeedIWDG();
                }
            }

            offset += chunk_size;
        } else {

            uint32_t data = *(volatile uint32_t*)(src_addr + offset);
            HAL_StatusTypeDef status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, dst_addr + offset, data);
            if (status != HAL_OK) {
                char error_msg[64];
                int len = sprintf(error_msg, "Flash programming failed at offset 0x%08lX, error: 0x%08lX\r\n",
                                  offset, HAL_FLASH_GetError());

                Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Copy_A_To_B,AllBootloaderRunStates.Run_Invalid,NULL);
                HAL_FLASH_Lock();
                return HAL_ERROR;
            }
            offset += 4;
        }
    }

    HAL_FLASH_Lock();

    FeedIWDG();

    __DSB();
    __ISB();

    volatile uint32_t delay_counter;
    for(delay_counter = 0; delay_counter < 5000000; delay_counter++);

    __DSB();
    __ISB();

    volatile uint32_t crc_compare_delay_counter;
    for(crc_compare_delay_counter = 0; crc_compare_delay_counter < 1000000; crc_compare_delay_counter++);

    __DSB();
    __ISB();

    volatile uint32_t final_crc_compare_delay_counter;
    for(final_crc_compare_delay_counter = 0; final_crc_compare_delay_counter < 1000000; final_crc_compare_delay_counter++);

    __DSB();
    __ISB();

    uint32_t crc_a = CalculateCRC32((const uint8_t *)src_addr, actual_program_size);
    uint32_t crc_b = CalculateCRC32((const uint8_t *)dst_addr, actual_program_size);

    print_uint32_with_label("crc_a",crc_a);
    print_uint32_with_label("crc_b",crc_b);

    if (crc_a != crc_b) {

        Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_CRC, AllBootloaderRunStates.Run_Invalid, (uint8_t *)&crc_a);
        BootloaderErrorInterFunc();
        return HAL_ERROR;
    }

     return HAL_OK;
}

uint32_t DetectActualProgramSize(uint32_t src_addr, uint32_t max_size)
{
    #ifndef bool
    #define bool uint8_t
    #define true 1
    #define false 0
    #endif

    uint32_t last_used_sector_end = src_addr;

    uint32_t current_addr = src_addr;
    uint32_t end_addr = src_addr + max_size;

    while (current_addr < end_addr) {

        uint32_t sector_start = Flash_GetSectorStartAddressByAddress(current_addr);
        uint32_t sector_size = 0;

        if (sector_start == FLASH_BASE_ADDR) {
            sector_size = SECTOR_SIZE_0;
        } else if (sector_start == FLASH_BASE_ADDR + SECTOR_SIZE_0) {
            sector_size = SECTOR_SIZE_1;
        } else if (sector_start == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1) {
            sector_size = SECTOR_SIZE_2;
        } else if (sector_start == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2) {
            sector_size = SECTOR_SIZE_3;
        } else if (sector_start == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3) {
            sector_size = SECTOR_SIZE_4;
        } else if (sector_start == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4) {
            sector_size = SECTOR_SIZE_5;
        } else if (sector_start == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5) {
            sector_size = SECTOR_SIZE_6;
        } else if (sector_start == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5 + SECTOR_SIZE_6) {
            sector_size = SECTOR_SIZE_7;
        }

        uint32_t sector_end = sector_start + sector_size;
        uint32_t check_addr = current_addr;
        uint32_t sector_check_end = (sector_end < end_addr) ? sector_end : end_addr;

        bool sector_has_data = false;

        for (uint32_t i = 0; i < sector_check_end - check_addr && !sector_has_data; i += 0x1000) {
            uint32_t data = *(volatile uint32_t*)(check_addr + i);
            if (data != 0xFFFFFFFF) {
                sector_has_data = true;
                last_used_sector_end = check_addr + i + 4;
            }
        }

        if (sector_has_data) {

            for (uint32_t scan_addr = check_addr; scan_addr < sector_check_end; scan_addr += 4) {
                uint32_t data = *(volatile uint32_t*)scan_addr;
                if (data != 0xFFFFFFFF) {
                    last_used_sector_end = scan_addr + 4;
                }
            }
        }

        current_addr = sector_end;
        if (current_addr >= end_addr) break;
    }

    if (last_used_sector_end == src_addr) {
        return 4;
    }

    uint32_t search_start = last_used_sector_end - 4;
    uint32_t sector_start_of_interest = Flash_GetSectorStartAddressByAddress(search_start);
    uint32_t sector_size_of_interest = 0;

    if (sector_start_of_interest == FLASH_BASE_ADDR) {
        sector_size_of_interest = SECTOR_SIZE_0;
    } else if (sector_start_of_interest == FLASH_BASE_ADDR + SECTOR_SIZE_0) {
        sector_size_of_interest = SECTOR_SIZE_1;
    } else if (sector_start_of_interest == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1) {
        sector_size_of_interest = SECTOR_SIZE_2;
    } else if (sector_start_of_interest == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2) {
        sector_size_of_interest = SECTOR_SIZE_3;
    } else if (sector_start_of_interest == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3) {
        sector_size_of_interest = SECTOR_SIZE_4;
    } else if (sector_start_of_interest == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4) {
        sector_size_of_interest = SECTOR_SIZE_5;
    } else if (sector_start_of_interest == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5) {
        sector_size_of_interest = SECTOR_SIZE_6;
    } else if (sector_start_of_interest == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5 + SECTOR_SIZE_6) {
        sector_size_of_interest = SECTOR_SIZE_7;
    }

    uint32_t sector_end_of_interest = sector_start_of_interest + sector_size_of_interest;
    if (sector_end_of_interest > src_addr + max_size) {
        sector_end_of_interest = src_addr + max_size;
    }

    uint32_t consecutive_ff_count = 0;
    uint32_t detected_end = last_used_sector_end;

    for (uint32_t scan_addr = sector_end_of_interest - 4;
         scan_addr >= search_start && scan_addr < sector_end_of_interest;
         scan_addr -= 4) {
        uint32_t data = *(volatile uint32_t*)scan_addr;
        if (data == 0xFFFFFFFF) {
            consecutive_ff_count++;

            if (consecutive_ff_count >= 8) {
                detected_end = scan_addr + 4;
                break;
            }
        } else {
            consecutive_ff_count = 0;
        }
    }

    uint32_t actual_size = detected_end - src_addr;
    if (actual_size < 4) actual_size = 4;

    return actual_size;
}

HAL_StatusTypeDef FlashEraseAllSectors(uint32_t start_addr, uint32_t size)
{
    HAL_FLASH_Unlock();

    uint32_t end_addr = start_addr + size - 1;

    if (start_addr < APP_B_START_ADDR) {
        start_addr = APP_B_START_ADDR;
    }
    if (end_addr > APP_B_END_ADDR) {
        end_addr = APP_B_END_ADDR;
    }

    uint32_t start_sector_addr = Flash_GetSectorStartAddressByAddress(start_addr);
    uint32_t end_sector_addr = Flash_GetSectorStartAddressByAddress(end_addr);

    if (start_sector_addr == 0 || end_sector_addr == 0) {
        HAL_FLASH_Lock();
        return HAL_ERROR;
    }

    uint32_t current_sector_addr = start_sector_addr;
    while (current_sector_addr <= end_sector_addr) {

        if (current_sector_addr >= APP_B_START_ADDR && current_sector_addr <= APP_B_END_ADDR) {
            HAL_StatusTypeDef status = Flash_EraseSectorByAddress(current_sector_addr);
            if (status != HAL_OK) {
                HAL_FLASH_Lock();
                return status;
            }
        }

        if (current_sector_addr == FLASH_BASE_ADDR) {
            current_sector_addr += SECTOR_SIZE_0;
        } else if (current_sector_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0) {
            current_sector_addr += SECTOR_SIZE_1;
        } else if (current_sector_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1) {
            current_sector_addr += SECTOR_SIZE_2;
        } else if (current_sector_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2) {
            current_sector_addr += SECTOR_SIZE_3;
        } else if (current_sector_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3) {
            current_sector_addr += SECTOR_SIZE_4;
        } else if (current_sector_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4) {
            current_sector_addr += SECTOR_SIZE_5;
        } else if (current_sector_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5) {
            current_sector_addr += SECTOR_SIZE_6;
        } else if (current_sector_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5 + SECTOR_SIZE_6) {
            current_sector_addr += SECTOR_SIZE_7;
        } else {
            break;
        }
    }

    HAL_FLASH_Lock();
    return HAL_OK;
}

void ResetAll(void){
    ;
}

void BootloaderErrorInterFunc(void){
extern UART_HandleTypeDef huart1;
    while(1){

        FeedIWDG();
        if(HAL_UART_Transmit(&huart1,"A false\r\n",9,100)!=HAL_OK){
            HAL_UART_Transmit(&huart1,"A erro\r\n",9,100);
        }
        __HAL_UART_CLEAR_FLAG(&huart1, UART_FLAG_TC);
        HAL_GPIO_WritePin(GPIOB,GPIO_PIN_12,GPIO_PIN_SET);
        HAL_Delay(1000);
        HAL_GPIO_WritePin(GPIOB,GPIO_PIN_12,GPIO_PIN_RESET);
    }
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

HAL_StatusTypeDef Flash_EraseSectorByAddress(uint32_t SectorStartAddress)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t SectorError = 0;
    uint32_t SectorNumber;

    SectorNumber = Flash_GetHalSectorNumber(SectorStartAddress);
    if (SectorNumber == 0xFF) {
        return HAL_ERROR;
    }

    HAL_FLASH_Unlock();

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR |
                          FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                          FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    EraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORS;
    EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    EraseInitStruct.Sector = SectorNumber;
    EraseInitStruct.NbSectors = 1;
    EraseInitStruct.Banks = FLASH_BANK_1;

    status = HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError);

    HAL_FLASH_Lock();

    return status;
}

HAL_StatusTypeDef FlashErase(uint32_t flash_start_addr, uint32_t Size)
{
    HAL_FLASH_Unlock();

    if (Size == 0) {
        return HAL_OK;
    }

    flash_start_addr = flash_start_addr & ~0x03;
    uint32_t end_addr = flash_start_addr + Size - 1;
    end_addr = end_addr & ~0x03;

    uint32_t SampleInterval;
    if (Size <= 16 * 4) {
        SampleInterval = 4;
    } else {
        SampleInterval = Size / 16;
        if (SampleInterval < 4) SampleInterval = 4;
        SampleInterval = (SampleInterval + 3) & ~0x03;
    }

    #define MAX_SECTORS 32
    uint32_t sectors_to_erase[MAX_SECTORS];
    uint8_t sector_count = 0;

    for (uint32_t addr = flash_start_addr; addr <= end_addr; addr += SampleInterval)
    {

        uint32_t aligned_addr = addr & ~0x03;
        uint32_t data = *(volatile uint32_t*)aligned_addr;

        if (data != 0xFFFFFFFF)
        {

            uint32_t sector_addr = Flash_GetSectorStartAddressByAddress(aligned_addr);

            uint8_t already_in_list = 0;
            for (int j = 0; j < sector_count; j++) {
                if (sectors_to_erase[j] == sector_addr) {
                    already_in_list = 1;
                    break;
                }
            }

            if (!already_in_list && sector_count < MAX_SECTORS) {
                sectors_to_erase[sector_count++] = sector_addr;
            }
        }

        if (addr + SampleInterval > end_addr && addr < end_addr) {

            uint32_t last_data = *(volatile uint32_t*)end_addr;
            if (last_data != 0xFFFFFFFF) {
                uint32_t last_sector = Flash_GetSectorStartAddressByAddress(end_addr);
                uint8_t already_in_list = 0;
                for (int j = 0; j < sector_count; j++) {
                    if (sectors_to_erase[j] == last_sector) {
                        already_in_list = 1;
                        break;
                    }
                }
                if (!already_in_list && sector_count < MAX_SECTORS) {
                    sectors_to_erase[sector_count++] = last_sector;
                }
            }
            break;
        }
    }

    for (int i = 0; i < sector_count; i++) {
        Flash_EraseSectorByAddress(sectors_to_erase[i]);
    }
    HAL_FLASH_Lock();
    return HAL_OK;
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

void MonitorBootloaderCMD(uint8_t* pBuf){
    if(pBuf[0]==MAGIC_HEADER && pBuf[1]==MAGIC_FOOTER){

        if(pBuf[2]==AllCMDs.Cmd_Updata_A){

            Send_BootloaderPacket(AllCMDs.Cmd_Update_State,AllBootloaderRunStates.Run_Prepare_To_Jump_To_B,NULL);
            Send_BootloaderPacket(AllCMDs.Cmd_ACK,AllBootloaderRunStates.Run_Invalid,NULL);
            JumpToBootloader();
        }
    }
}

void print_uint32_with_label(const char* label, uint32_t value)
{
    extern UART_HandleTypeDef huart1;
    char buffer[64];
    int len = sprintf(buffer, "[%s] Value = %lu (0x%08lX)\r\n", label, value, value);
    HAL_UART_Transmit(&huart1, (uint8_t*)buffer, len, HAL_MAX_DELAY);
}

