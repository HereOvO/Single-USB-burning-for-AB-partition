#include "app_base.h"

//标准状态定义
AllERRORs_t AllERRORs = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06}; // 初始化错误码
AllCmds_t AllCMDs = {0, 1, 2, 3, 4, 5, 6, 7, &AllERRORs};
AllBootloaderStartModes_t AllBootloaderStartModes = {0, 1, 2, 3};//Bootloader开始模式的状态机
AllBootloaderRunStates_t AllBootloaderRunStates = {0, 1, 2, 3, 4, 5, 6, 7};//Bootloader运行状态的状态机

//实际状态结构体
BootloaderState_t BootloaderState;

extern UART_HandleTypeDef huart1;


//====================function==========================

/**
 * @brief 喂狗任务的任务函数,5s
 * 
 */
void StartFeedIWDG(void){
    while(1){
        IWDG->KR = 0xAAAA;
        osDelay(5000);
    }
}

void FeedIWDG(void){
    IWDG->KR = 0xAAAA;
}

/**
 * @brief  发送引导加载程序数据包
 * @param  cmd: 命令码
 * @param  BootloaderState: 状态
 * @param  data: 数据指针（可以为NULL）
 * @return  HAL_StatusTypeDef: 操作结果
 */
HAL_StatusTypeDef Send_BootloaderPacket(uint8_t cmd, uint8_t BootloaderState, uint8_t* data)
{
    uint8_t packet_buffer[16]; // 创建足够大的缓冲区来容纳包头、命令、状态和最多8字节的数据
    uint8_t packet_len = 4; // 默认包长度为4字节（魔数头+魔数尾+命令+状态）

    // 初始化魔数
    packet_buffer[0] = MAGIC_HEADER;
    packet_buffer[1] = MAGIC_FOOTER;

    // 初始化命令和状态
    packet_buffer[2] = cmd;
    packet_buffer[3] = BootloaderState;

    // 如果有数据，则添加到包中
    if (data != NULL) {
        // 对于错误响应，数据通常是4字节的CRC值
        packet_buffer[4] = data[0];
        packet_buffer[5] = data[1];
        packet_buffer[6] = data[2];
        packet_buffer[7] = data[3];
        packet_len += 4; // 增加4字节数据长度
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




/**
 * @brief 计算CRC32校验值
 * @param data 数据指针
 * @param length 数据长度
 * @return CRC32校验值
 */
uint32_t CalculateCRC32(const uint8_t *data, uint32_t length)
{
    // 初始化CRC为0xFFFFFFFF
    uint32_t crc = 0xFFFFFFFF;
    uint32_t i, j;

    for (i = 0; i < length; i++)
    {
        // XOR下一个输入字节到CRC寄存器的最低有效字节
        crc ^= data[i];

        // 对字节中的每一位执行此操作
        for (j = 0; j < 8; j++)
        {
            // 如果LSB为1，则右移一位并异或多项式0xEDB88320
            // 否则仅右移一位
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

    // 最后反转结果
    return crc ^ 0xFFFFFFFF;
}

/**
 * @brief  复制A区程序到B区（备份）
 * @retval HAL_StatusTypeDef
 */
HAL_StatusTypeDef CopyAtoB(void)
{   extern UART_HandleTypeDef huart1;

    uint32_t src_addr = APP_A_START_ADDR;
    uint32_t dst_addr = APP_B_START_ADDR;
    uint32_t size = APP_A_SIZE < APP_B_SIZE ? APP_A_SIZE : APP_B_SIZE; // 取较小的尺寸
    //HAL_UART_Transmit(&huart1,"pace 4\r\n",8,100);//调试输出
    // 检查B区是否有足够的空间
    if (size < 0x1000) { // 至少需要4KB空间
        Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Copy_A_To_B,AllBootloaderRunStates.Run_Invalid,NULL);
        //HAL_UART_Transmit(&huart1,"A false1\r\n",10,100);//调试输出
        return HAL_ERROR;
    }
    //HAL_UART_Transmit(&huart1,"pace 41\r\n",9,100);//调试输出
    FeedIWDG();

    // 完全擦除整个B区，确保所有内容都为0xFF
    // 使用强制擦除函数，擦除整个B区的所有扇区
    if (FlashEraseAllSectors(APP_B_START_ADDR, APP_B_SIZE) != HAL_OK) {
        Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Copy_A_To_B,AllBootloaderRunStates.Run_Invalid,NULL);
        //HAL_UART_Transmit(&huart1,"A false2\r\n",10,100); //调试输出
        return HAL_ERROR;
    }
    //HAL_UART_Transmit(&huart1,"pace 42\r\n",9,100);//调试输出
    // 解锁Flash
    HAL_FLASH_Unlock();

    FeedIWDG();
    //HAL_UART_Transmit(&huart1,"pace 42\r\n",9,100);//调试输出

    // 首先检测实际程序大小，通过扇区级别粗略定位，然后精确查找
    uint32_t actual_program_size = DetectActualProgramSize(src_addr, size);
    
    // 解锁Flash
    HAL_FLASH_Unlock();
    
    // 按扇区边界进行批量编程，只编程实际程序大小的数据
    uint32_t offset = 0;
    while (offset < actual_program_size) {
        // 计算当前地址所属的扇区起始地址
        uint32_t current_sector_start = Flash_GetSectorStartAddressByAddress(dst_addr + offset);
        uint32_t current_sector_size = 0;
        
        // 根据扇区起始地址确定扇区大小
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
        
        // 计算当前扇区剩余的字节数
        uint32_t sector_remaining = current_sector_end - (dst_addr + offset);
        uint32_t chunk_size = (actual_program_size - offset) < sector_remaining ? (actual_program_size - offset) : sector_remaining;
        
        // 确保chunk_size不超过实际程序大小
        if (offset + chunk_size > actual_program_size) {
            chunk_size = actual_program_size - offset;
        }
        
        // 确保chunk_size是字对齐的
        chunk_size = (chunk_size / 4) * 4;
        
        if (chunk_size > 0) {
            // 使用快速编程模式进行批量编程
            uint32_t src_chunk_addr = src_addr + offset;
            uint32_t dst_chunk_addr = dst_addr + offset;
            uint32_t word_count = chunk_size / 4;
            
            // 逐字编程当前块，但按扇区组织
            for (uint32_t i = 0; i < word_count; i++) {
                uint32_t data = *(volatile uint32_t*)(src_chunk_addr + i * 4);
                
                HAL_StatusTypeDef status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, dst_chunk_addr + i * 4, data);
                
                if (status != HAL_OK) {
                    // 输出Flash错误信息
                    char error_msg[64];
                    int len = sprintf(error_msg, "Flash programming failed at offset 0x%08lX, error: 0x%08lX\r\n", 
                                      offset + i * 4, HAL_FLASH_GetError());
                    //HAL_UART_Transmit(&huart1, (uint8_t*)error_msg, len, HAL_MAX_DELAY);  //调试输出
                    
                    Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Copy_A_To_B,AllBootloaderRunStates.Run_Invalid,NULL);
                    HAL_FLASH_Lock();
                    return HAL_ERROR;
                }
                
                // 每编程几个字后喂狗，避免看门狗超时
                if ((i % 100) == 0) {
                    FeedIWDG();
                }
            }
            
            offset += chunk_size;
        } else {
            // 如果chunk_size为0，前进一个字
            uint32_t data = *(volatile uint32_t*)(src_addr + offset);
            HAL_StatusTypeDef status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, dst_addr + offset, data);
            if (status != HAL_OK) {
                char error_msg[64];
                int len = sprintf(error_msg, "Flash programming failed at offset 0x%08lX, error: 0x%08lX\r\n", 
                                  offset, HAL_FLASH_GetError());
                //HAL_UART_Transmit(&huart1, (uint8_t*)error_msg, len, HAL_MAX_DELAY);  调试输出
                
                Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Copy_A_To_B,AllBootloaderRunStates.Run_Invalid,NULL);
                HAL_FLASH_Lock();
                return HAL_ERROR;
            }
            offset += 4;
        }
    }
    
    // 编程完成后锁定Flash
    HAL_FLASH_Lock();
    //HAL_UART_Transmit(&huart1,"pace 43\r\n",9,100);//调试输出
    FeedIWDG();

    // 等待Flash操作完成
    __DSB(); // 数据同步屏障
    __ISB(); // 指令同步屏障

    // 使用更长的循环延时，确保Flash操作完全稳定
    // 添加延时确保Flash操作稳定
    volatile uint32_t delay_counter;
    for(delay_counter = 0; delay_counter < 5000000; delay_counter++); // 增加延时

    // STM32F4没有数据缓存，只需要使用内存屏障来确保内存操作完成
    __DSB();
    __ISB();
    //HAL_UART_Transmit(&huart1,"pace 44\r\n",9,100);//调试输出
    // 添加延时确保Flash操作稳定
    volatile uint32_t crc_compare_delay_counter;
    for(crc_compare_delay_counter = 0; crc_compare_delay_counter < 1000000; crc_compare_delay_counter++); // 简单的循环延时

    // STM32F4没有数据缓存，只需要使用内存屏障来确保内存操作完成
    __DSB();
    __ISB();

    // 添加延时确保Flash操作稳定
    volatile uint32_t final_crc_compare_delay_counter;
    for(final_crc_compare_delay_counter = 0; final_crc_compare_delay_counter < 1000000; final_crc_compare_delay_counter++); // 简单的循环延时

    // STM32F4没有数据缓存，只需要使用内存屏障来确保内存操作完成
    __DSB();
    __ISB();

    // 计算A区和B区的CRC32校验值，只对实际程序区域进行校验
    uint32_t crc_a = CalculateCRC32((const uint8_t *)src_addr, actual_program_size);
    uint32_t crc_b = CalculateCRC32((const uint8_t *)dst_addr, actual_program_size);

    print_uint32_with_label("crc_a",crc_a);
    print_uint32_with_label("crc_b",crc_b);

    // 比较A区和B区的CRC32校验值
    if (crc_a != crc_b) {
        // CRC校验失败，发送错误信息，包括A区的CRC值
        Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_CRC, AllBootloaderRunStates.Run_Invalid, (uint8_t *)&crc_a);
        BootloaderErrorInterFunc();
        return HAL_ERROR;
    }
    //HAL_UART_Transmit(&huart1,"pace 45\r\n",9,100);//调试输出
     return HAL_OK;
}

/**
 * @brief 检测实际程序大小，通过扇区级别粗略定位，然后精确查找
 * @param src_addr 源地址
 * @param max_size 最大大小
 * @return 实际程序大小
 */
uint32_t DetectActualProgramSize(uint32_t src_addr, uint32_t max_size)
{
    #ifndef bool
    #define bool uint8_t
    #define true 1
    #define false 0
    #endif
    // 第一步：扇区级别粗略定位，找到最后一个有程序的扇区
    uint32_t last_used_sector_end = src_addr;  // 初始化为起始地址
    
    // 遍历所有扇区，找到最后一个非0xFFFFFFFF的扇区
    uint32_t current_addr = src_addr;
    uint32_t end_addr = src_addr + max_size;
    
    while (current_addr < end_addr) {
        // 获取当前扇区的起始地址和大小
        uint32_t sector_start = Flash_GetSectorStartAddressByAddress(current_addr);
        uint32_t sector_size = 0;
        
        // 根据扇区起始地址确定扇区大小
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
        
        // 检查当前扇区是否包含非0xFFFFFFFF的数据
        uint32_t sector_end = sector_start + sector_size;
        uint32_t check_addr = current_addr;
        uint32_t sector_check_end = (sector_end < end_addr) ? sector_end : end_addr;
        
        bool sector_has_data = false;
        // 检查扇区开头、中间和结尾的一些数据点
        for (uint32_t i = 0; i < sector_check_end - check_addr && !sector_has_data; i += 0x1000) { // 每4KB检查一次
            uint32_t data = *(volatile uint32_t*)(check_addr + i);
            if (data != 0xFFFFFFFF) {
                sector_has_data = true;
                last_used_sector_end = check_addr + i + 4;  // 记录最后使用的地址
            }
        }
        
        // 如果当前扇区有数据，更新最后使用的地址
        if (sector_has_data) {
            // 详细扫描整个扇区找到最后的数据位置
            for (uint32_t scan_addr = check_addr; scan_addr < sector_check_end; scan_addr += 4) {
                uint32_t data = *(volatile uint32_t*)scan_addr;
                if (data != 0xFFFFFFFF) {
                    last_used_sector_end = scan_addr + 4;  // 记录最后使用的地址
                }
            }
        }
        
        current_addr = sector_end;
        if (current_addr >= end_addr) break;
    }
    
    // 如果没有找到任何数据，返回最小值
    if (last_used_sector_end == src_addr) {
        return 4;  // 至少返回4字节
    }
    
    // 第二步：在最后使用的扇区内精确查找程序结束位置
    uint32_t search_start = last_used_sector_end - 4;  // 从最后已知的数据位置开始向前搜索
    uint32_t sector_start_of_interest = Flash_GetSectorStartAddressByAddress(search_start);
    uint32_t sector_size_of_interest = 0;
    
    // 获取扇区大小
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
    
    // 从后往前搜索，找到连续的0xFFFFFFFF序列
    uint32_t consecutive_ff_count = 0;
    uint32_t detected_end = last_used_sector_end;  // 默认为最后使用的地址
    
    for (uint32_t scan_addr = sector_end_of_interest - 4; 
         scan_addr >= search_start && scan_addr < sector_end_of_interest; 
         scan_addr -= 4) {
        uint32_t data = *(volatile uint32_t*)scan_addr;
        if (data == 0xFFFFFFFF) {
            consecutive_ff_count++;
            // 如果找到连续的多个0xFFFFFFFF，认为程序在这里结束
            if (consecutive_ff_count >= 8) {  // 连续8个字（32字节）都是0xFF
                detected_end = scan_addr + 4;
                break;
            }
        } else {
            consecutive_ff_count = 0;  // 重置计数
        }
    }
    
    // 返回检测到的程序大小，确保不小于最小值
    uint32_t actual_size = detected_end - src_addr;
    if (actual_size < 4) actual_size = 4;
    
    return actual_size;
}

/**
 * @brief 强制擦除指定地址范围内的所有扇区
 * @param start_addr 起始地址
 * @param size 大小
 * @return HAL_StatusTypeDef
 */
HAL_StatusTypeDef FlashEraseAllSectors(uint32_t start_addr, uint32_t size)
{
    HAL_FLASH_Unlock();

    uint32_t end_addr = start_addr + size - 1;
    
    // 确保不超出B区范围
    if (start_addr < APP_B_START_ADDR) {
        start_addr = APP_B_START_ADDR;
    }
    if (end_addr > APP_B_END_ADDR) {
        end_addr = APP_B_END_ADDR;
    }
    
    // 获取起始和结束扇区
    uint32_t start_sector_addr = Flash_GetSectorStartAddressByAddress(start_addr);
    uint32_t end_sector_addr = Flash_GetSectorStartAddressByAddress(end_addr);

    if (start_sector_addr == 0 || end_sector_addr == 0) {
        HAL_FLASH_Lock();
        return HAL_ERROR;  // 无效地址
    }

    // 擦除从起始扇区到结束扇区的所有扇区
    uint32_t current_sector_addr = start_sector_addr;
    while (current_sector_addr <= end_sector_addr) {
        // 确保不超出B区范围
        if (current_sector_addr >= APP_B_START_ADDR && current_sector_addr <= APP_B_END_ADDR) {
            HAL_StatusTypeDef status = Flash_EraseSectorByAddress(current_sector_addr);
            if (status != HAL_OK) {
                HAL_FLASH_Lock();
                return status;
            }
        }

        // 移动到下一个扇区
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

/**
 * @brief Construct a new Reset All object
 *
 */
void ResetAll(void){
    ;
}


/**
 * @brief 所有的Bootloader类的错误都会进入这个函数,可以在函数内输出日志
 * 
 */
void BootloaderErrorInterFunc(void){
extern UART_HandleTypeDef huart1;
    while(1){
        //uint32_t temp=IWDG->KR;
        //print_uint32_with_label("IWDG->KR", temp);//调试输出
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





/* 扇区起始地址（根据你的大小定义计算） */
static const uint32_t SectorStartAddresses[] = {
    FLASH_BASE_ADDR,                          // Sector 0
    FLASH_BASE_ADDR + SECTOR_SIZE_0,          // Sector 1
    FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1,           // Sector 2
    FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2,  // Sector 3
    FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3, // Sector 4
    FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4, // Sector 5
    FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5, // Sector 6
    FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5 + SECTOR_SIZE_6 // Sector 7
};

/* 扇区大小数组 */
static const uint32_t SectorSizes[] = {
    SECTOR_SIZE_0, SECTOR_SIZE_1, SECTOR_SIZE_2, SECTOR_SIZE_3,
    SECTOR_SIZE_4, SECTOR_SIZE_5, SECTOR_SIZE_6, SECTOR_SIZE_7
};

/* HAL扇区号定义 */
static const uint32_t HalSectorNumbers[] = {
    FLASH_SECTOR_0, FLASH_SECTOR_1, FLASH_SECTOR_2, FLASH_SECTOR_3,
    FLASH_SECTOR_4, FLASH_SECTOR_5, FLASH_SECTOR_6, FLASH_SECTOR_7
};

/**
 * @brief  根据地址获取扇区起始地址
 * @param  Address: 要查询的地址
 * @retval 扇区起始地址，0表示无效地址
 */
uint32_t Flash_GetSectorStartAddressByAddress(uint32_t Address)
{
    // 检查地址是否在Flash范围内
    if (Address < FLASH_BASE_ADDR ||
        Address >= FLASH_BASE_ADDR + FLASH_TOTAL_SIZE) {
        return 0;  // 无效地址
    }

    // 遍历所有扇区，找到包含该地址的扇区
    for (int i = 0; i < 8; i++) {
        uint32_t sector_start = SectorStartAddresses[i];
        uint32_t sector_end = sector_start + SectorSizes[i];

        if (Address >= sector_start && Address < sector_end) {
            return sector_start;
        }
    }

    return 0;  // 不应该执行到这里
}

/**
 * @brief  根据起始地址获取HAL扇区号
 * @param  SectorStartAddress: 扇区起始地址
 * @retval HAL扇区号，0xFF表示无效
 */
static uint8_t Flash_GetHalSectorNumber(uint32_t SectorStartAddress)
{
    for (int i = 0; i < 8; i++) {
        if (SectorStartAddress == SectorStartAddresses[i]) {
            return HalSectorNumbers[i];
        }
    }
    return 0xFF;  // 无效扇区地址
}

/**
 * @brief  擦除指定起始地址的扇区
 * @param  SectorStartAddress: 扇区起始地址
 * @retval HAL_OK: 成功, 其他: 失败
 */
HAL_StatusTypeDef Flash_EraseSectorByAddress(uint32_t SectorStartAddress)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t SectorError = 0;
    uint32_t SectorNumber;

    // 1. 验证地址是否为有效的扇区起始地址
    SectorNumber = Flash_GetHalSectorNumber(SectorStartAddress);
    if (SectorNumber == 0xFF) {
        return HAL_ERROR;
    }

    // 2. 解锁Flash
    HAL_FLASH_Unlock();

    // 3. 清除所有错误标志
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR |
                          FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                          FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    // 4. 配置擦除参数
    EraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORS;
    EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;  // 2.7V-3.6V
    EraseInitStruct.Sector = SectorNumber;                 // 扇区号
    EraseInitStruct.NbSectors = 1;                         // 擦除1个扇区
    EraseInitStruct.Banks = FLASH_BANK_1;                  // F407只有一个Bank

    // 5. 执行擦除
    status = HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError);

    // 6. 锁定Flash
    HAL_FLASH_Lock();

    return status;
}

/**
 * @brief 擦除flash（按需擦除）
 * @param flash_start_addr 需要擦除flash的起始地址
 * @param Size 需要的flash大小（字节）
 * @return 成功擦除返回0
 */
HAL_StatusTypeDef FlashErase(uint32_t flash_start_addr, uint32_t Size)
{
    HAL_FLASH_Unlock();

    if (Size == 0) {
        return HAL_OK;
    }

    // 确保地址4字节对齐
    flash_start_addr = flash_start_addr & ~0x03;
    uint32_t end_addr = flash_start_addr + Size - 1;
    end_addr = end_addr & ~0x03;  // 对齐到字边界

    // 计算采样间隔（确保不为0）
    uint32_t SampleInterval;
    if (Size <= 16 * 4) {
        SampleInterval = 4;  // 按字采样
    } else {
        SampleInterval = Size / 16;
        if (SampleInterval < 4) SampleInterval = 4;  // 最小间隔为4字节
        SampleInterval = (SampleInterval + 3) & ~0x03;  // 对齐到4字节
    }

    // 用数组记录需要擦除的扇区（避免重复擦除）
    #define MAX_SECTORS 32
    uint32_t sectors_to_erase[MAX_SECTORS];
    uint8_t sector_count = 0;

    // 采样检查
    for (uint32_t addr = flash_start_addr; addr <= end_addr; addr += SampleInterval)
    {
        // 读取数据（确保地址对齐）
        uint32_t aligned_addr = addr & ~0x03;
        uint32_t data = *(volatile uint32_t*)aligned_addr;

        if (data != 0xFFFFFFFF)
        {
            // 获取这个地址所在的扇区
            uint32_t sector_addr = Flash_GetSectorStartAddressByAddress(aligned_addr);

            // 检查这个扇区是否已经在待擦除列表中
            uint8_t already_in_list = 0;
            for (int j = 0; j < sector_count; j++) {
                if (sectors_to_erase[j] == sector_addr) {
                    already_in_list = 1;
                    break;
                }
            }

            // 如果不在列表中，添加进去
            if (!already_in_list && sector_count < MAX_SECTORS) {
                sectors_to_erase[sector_count++] = sector_addr;
            }
        }

        // 边界检查：避免最后一次循环越界
        if (addr + SampleInterval > end_addr && addr < end_addr) {
            // 强制检查最后一个地址
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

    // 擦除所有需要擦除的扇区
    for (int i = 0; i < sector_count; i++) {
        Flash_EraseSectorByAddress(sectors_to_erase[i]);
    }
    HAL_FLASH_Lock();
    return HAL_OK;
}


/**
 * @brief 跳转到Bootloader程序的函数
 * 
 */
void JumpToBootloader(void)
{
        /* 1. 等待上次操作完成（检查状态寄存器） */
    while (IWDG->SR & IWDG_SR_PVU);  // 等待PR更新完成
    while (IWDG->SR & IWDG_SR_RVU);  // 等待RLR更新完成
    
    /* 2. 解锁寄存器写保护（写入0x5555到KR）*/
    IWDG->KR = 0x5555;
    
    /* 3. 修改预分频器为最小值（4分频）*/
    // PR寄存器值：0=4分频，1=8分频，2=16分频... 6=256分频
    while (IWDG->SR & IWDG_SR_PVU);  // 再次确认PR可写
    IWDG->PR = 0;                    // 设置为4分频（PR=0）
    
    /* 4. 修改重装载值为0（最小超时）*/
    while (IWDG->SR & IWDG_SR_RVU);  // 等待RLR可写
    IWDG->RLR = 0;                   // 重装载值设为0
    
    /* 5. 重新加载计数器（喂狗）使新值生效 */
    // 注意：这次喂狗会让计数器从新的RLR值（0）开始递减
    IWDG->KR = 0xAAAA;
    
    /* 6. 此时计数器值为0，下一个LSI时钟周期就会复位 */
    // LSI约32kHz，周期约31.25μs，所以约31.25μs后复位
    
    /* 为防止意外，可以在这里等待复位 */
    while(1) {
        __NOP();
    }
}


/**
 * @brief 在USB中断中检查传来的是不是升级指令
 * 
 * @param pBuf 
 */
void MonitorBootloaderCMD(uint8_t* pBuf){
    if(pBuf[0]==MAGIC_HEADER && pBuf[1]==MAGIC_FOOTER){
        //魔数匹配
        //  HAL_UART_Transmit(&huart1,"back2\r\n",7,100);//调试输出
        if(pBuf[2]==AllCMDs.Cmd_Updata_A){
            //要升级A
        //    HAL_UART_Transmit(&huart1,"back3\r\n",7,100);//调试输出
            Send_BootloaderPacket(AllCMDs.Cmd_Update_State,AllBootloaderRunStates.Run_Prepare_To_Jump_To_B,NULL);
            Send_BootloaderPacket(AllCMDs.Cmd_ACK,AllBootloaderRunStates.Run_Invalid,NULL);
            JumpToBootloader();
        }
        else if(pBuf[3]==AllCMDs.Cmd_Which_State){
            //回馈状态
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



