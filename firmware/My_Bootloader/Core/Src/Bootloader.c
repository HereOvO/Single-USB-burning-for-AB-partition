#include "Bootloader.h"
#include "iwdg.h"
#include "main.h"
#include <string.h>
#include "usb_device.h"


/* PV */

extern USBD_HandleTypeDef hUsbDeviceFS;
extern IWDG_HandleTypeDef hiwdg;
extern TIM_HandleTypeDef htim14;
extern UART_HandleTypeDef huart1;

//标准状态定义
extern AllERRORs_t AllERRORs;
extern AllCmds_t AllCMDs;
extern AllBootloaderStartModes_t AllBootloaderStartModes;//Bootloader开始模式的状态机
extern AllBootloaderRunStates_t AllBootloaderRunStates;//Bootloader运行状态的状态机

//实际状态结构体
extern volatile BootloaderState_t BootloaderState;

extern volatile uint16_t TimerCounter_ms;//超时计数

extern uint32_t total_size_to_receive;//准备要接收的bin大小
extern volatile uint16_t received_data_size; // 单次接收到的数据的字节数
extern volatile uint16_t written_data_size; // 单次写入到flash的数据的字节数
extern volatile uint32_t total_received_data_size; // 接收到的数据的总字节数
extern volatile uint32_t total_written_data_size; // 写入到flash的数据的总字节数
extern uint32_t flash_offset; //flash偏移量
extern uint8_t last_byte_flag; //标志位，1表示上一次接收的数据长度为奇数，留下了1个字节
extern uint8_t last_byte; //上一次剩下的一个字节
extern volatile uint8_t data_buffer[612]; //数据缓冲区,一次只接收512字节,但预留100个字节的空间,防止非法访问造成程序崩溃
extern volatile uint8_t is_data_buffer_full;//data_buffer满512个字节的标志位
extern volatile uint16_t data_buffer_offset;

extern uint8_t is_start_transmission;//是否开始传输的标志位
extern uint8_t is_transmission_complete;//传输结束标志位
extern uint8_t is_jump_to_application;//是否跳转到应用程序的标志位
extern uint8_t first_reception; //首次接收标志位

// AB分区相关变量
uint8_t current_boot_partition = 0;      // 当前启动的分区 (固定为A区=0)
uint8_t next_update_partition = 0;       // 下次更新的分区 (固定为A区=0)
uint32_t active_app_start_addr = APP_A_START_ADDR;      // 当前活动应用程序的起始地址，固定从A区启动
uint32_t update_target_addr = APP_A_START_ADDR;         // 更新目标地址，固定更新A区


/* Function */
/**
 * 初始化函数
 * 功能:初始化Bootloader相关资源，包括数据缓冲区、标志位等
 * 注意:USB设备初始化已在main.c的MX_USB_DEVICE_Init()中进行
 */
void Bootloader_Init(void)
{
    /**
     * USB设备初始化已在main.c的MX_USB_DEVICE_Init()中进行
     * 该函数会自动配置USB CDC接收回调为CDC_Receive_FS
     * USB数据接收通过USB_Data_Rx_Handler()中断回调进行处理
     */

    /* 加载分区状态 */
   // LoadPartitionStatus();

    /* 重置所有变量和状态 */
    ResetAll();
    
    // 确保TimerCounter_ms初始为0
    TimerCounter_ms = 0;
    
    /* Bootloader初始化完成，发送初始化完成消息 */
    // 初始化时不发送USB数据，避免影响USB初始化
    // USB_Send_Data((uint8_t*)"Bootloader initialized.\r\n", 26);
    // USB_SendInt16_WithLabel("Current boot partition", current_boot_partition);
    // USB_SendInt16_WithLabel("Next update partition", next_update_partition);
    // USB_SendInt16_WithLabel("Active app start address", active_app_start_addr);
    // USB_SendInt16_WithLabel("Update target address", update_target_addr);
}


/* 扇区起始地址（根据你的大小定义计算） */
static const uint32_t SectorStartAddresses[] = {
    FLASH_BASE_ADDR,                          // Sector 0
    FLASH_BASE_ADDR + SECTOR_SIZE_0,          // Sector 1
    FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1,           // Sector 2
    FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2,  // Sector 3
    FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3, // Sector 4
    FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4, // Sector 5  512-128*3   
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
 * @brief  [内部使用] 擦除指定起始地址的扇区，不包含Flash解锁/上锁
 * @param  SectorStartAddress: 扇区起始地址
 * @retval HAL_OK: 成功, 其他: 失败
 */
static HAL_StatusTypeDef Flash_EraseSectorByAddress_NoLock(uint32_t SectorStartAddress)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t SectorError = 0;
    uint32_t SectorNumber;
    
    // 每次擦除前都喂狗
    FeedIwdg();

    // 1. 验证地址是否为有效的扇区起始地址
    SectorNumber = Flash_GetHalSectorNumber(SectorStartAddress);
    if (SectorNumber == 0xFF) {
        return HAL_ERROR;
    }

    // 2. 清除所有错误标志(Flash必须已解锁)
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR |
                          FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                          FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    // 3. 配置擦除参数
    EraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORS;
    EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;  // 2.7V-3.6V
    EraseInitStruct.Sector = SectorNumber;                 // 扇区号
    EraseInitStruct.NbSectors = 1;                         // 擦除1个扇区
    EraseInitStruct.Banks = FLASH_BANK_1;                  // F407只有一个Bank

    // 4. 执行擦除
    status = HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError);

    return status;
}

/**
 * @brief  擦除指定起始地址的扇区
 * @param  SectorStartAddress: 扇区起始地址
 * @retval HAL_OK: 成功, 其他: 失败
 */
HAL_StatusTypeDef Flash_EraseSectorByAddress(uint32_t SectorStartAddress)
{
    HAL_StatusTypeDef status;
    
    // 1. 解锁Flash
    HAL_FLASH_Unlock();
    
    // 2. 调用内部无锁版本
    status = Flash_EraseSectorByAddress_NoLock(SectorStartAddress);

    // 3. 锁定Flash
    HAL_FLASH_Lock();

    return status;
}

//跳转到当前活动应用程序的函数
void JumpToApplication(void)
{
    JumpToSpecificApplication(active_app_start_addr);
}

//跳转到指定应用程序的函数
void JumpToSpecificApplication(uint32_t app_addr)
{
    
    typedef void (*pFunction)(void);
    pFunction JumpToApp = (pFunction)(*(__IO uint32_t*)(app_addr + 4)); // 获取复位向量地址并转换为函数指针

    uint32_t app_start_address = app_addr;
    uint32_t app_stack_ptr = *(__IO uint32_t*)app_start_address; // 获取堆栈指针地址
    uint32_t jump_address = *(__IO uint32_t*)(app_start_address + 4); // 获取复位向量地址

    //验证堆栈指针地址是否合法
    if ((app_stack_ptr & 0xFFF00000) != STACK_ADDR) {
        Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Jump,AllBootloaderRunStates.Run_Invalid,NULL);
        return; // 地址无效，直接返回
    }

    //验证复位地址是否合法
    if (jump_address < FLASH_BASE_ADDR || jump_address >= APP_END_ADDR) {
        Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Jump,AllBootloaderRunStates.Run_Invalid,NULL);
        return; // 地址无效，直接返回
    }


    //注销Bootloader
    //关闭中断
    __disable_irq();

    //注销HAL库设置
    HAL_DeInit();

    //重定向中断向量表
    SCB->VTOR = app_start_address & 0x1FFFFF8; // 根据技术要点进行对齐

    // 设置主堆栈指针
    __set_MSP(*(__IO uint32_t*)app_start_address);

    // 跳转到用户应用程序
    JumpToApp();
}


//跳转到Bootloader程序的函数
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
 *
 * @brief 重置所有相关变量和状态
 */
void  ResetAll(void)
{

   
     // extern uint16_t rx_read_pos; //清除接收读取位置 // 不要在函数内extern
     // extern uint16_t tx_send_pos; //清除发送趋势位置
     rx_read_pos = 0; //清除接收读取位置
     tx_send_pos = 0; //清除发送趋势位置
     
     // 确保BootloaderState被重置, 除非你想保留它(例如如果是从APP跳转回来的)
     // 这里有一个策略问题: 如果是从APP跳转回来, StartMode可能已经被设置为 UpdateA 等等
     // 但如果是上电复位, 则应该是 Invalid
     // 目前的代码全盘重置, 这意味着即使是APP请求跳转回来, 也会被重置? 
     // 不, APP跳转回来是软件复位(JumpToBootloader函数做了复位), RAM内容如果不放在NoInit段会被初始化为0?
     // Startup code会将data段初始化, bss段清零.
     // 所以每次复位, BootloaderState.StartMode 都会变为0 (Start_Invalid).
     // 确实需要上位机重新发送指令.
     
     // 如果你的逻辑是: APP跳转回来后, 保持某种状态? 
     // 通常APP跳转回Bootloader后, 也是等待上位机发指令.
     // 所以这里重置是正确的.

    BootloaderState.StartMode=AllBootloaderStartModes.Start_Invalid;
    BootloaderState.RunState=AllBootloaderRunStates.Run_Waiting_Cmd;


    is_start_transmission = 0; //取消传输
    is_transmission_complete = 0; //清除传输结束标志位
    first_reception =1; //重置首次接收标志位(特殊!第一次接收之后置为0)
    
    is_data_buffer_full = 0;
    data_buffer_offset = 0;
    received_data_size = 0;
    
    //超时相关
    HAL_TIM_Base_Stop(&htim14); //停止定时器
    TimerCounter_ms=0;//重置计时器

    //烧录相关
    flash_offset =0; //重置flash偏移量
    data_buffer_offset=0;
    last_byte_flag =0; //重置奇数长度处理标志位
    received_data_size =0;
    written_data_size =0;
    total_received_data_size =0;
    total_written_data_size =0;
    memset((void*)data_buffer, 0, 256); //清除缓冲区
    
    //USB相关
     memset((void*)UserRxBufferFS, '\0', APP_RX_DATA_SIZE); //清除缓冲区
     memset((void*)UserTxBufferFS, '\0', APP_TX_DATA_SIZE); //清除缓冲区
     USBD_CDC_ReceivePacket(&hUsbDeviceFS);//开启USB接收中断

}

/**
 * @brief 通过USB接收数据并写入Flash,支持奇数长度数据处理
 *        刷新received_data_size,written_data_size ,total_received_data_size ,total_written_data_size
 * @param Size 接收到的USB数据长度
 */
HAL_StatusTypeDef Download_Flash(uint32_t Size)
{
      //更新接收数据长度变量
      received_data_size = Size;
      // total_received_data_size += Size; // total_received_data_size 在这里增加是不对的？不，是对的，接收了多少就是多少
      // 但对于CRC校验来说，我们校验的是写入到Flash的数据量
      // total_received_data_size 用于显示进度之类的
      total_received_data_size += Size;


      //烧写flash的代码
      //解锁flash
      HAL_FLASH_Unlock();
      //使用16位对齐的方式写入数据

      //写入flash
      //有以下四种情况:
      //上次留下一个字节: 1.这次数据长度为奇数 => 这次发送偶数个,不会留下字节
      //                 2.这次数据长度为偶数 => 这次发送奇数个,会留下字节
      //上次没有留下字节: 1.这次数据长度为奇数 => 这次发送奇数个,会留下字节
      //                 2.这次数据长度为偶数 => 这次发送偶数个,不会留下字节
      if(last_byte_flag == 1)
      {
          //上次留下一个字节
          uint16_t first_halfword;
          if(Size >=1)
          {
              //这次数据长度至少为1
              // 将上次留下的字节作为低位字节，本次第一个字节作为高位字节
              first_halfword = (data_buffer[0] << 8) | last_byte;
              if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, update_target_addr + flash_offset, first_halfword) != HAL_OK)
              {
                  Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Flash_Download,AllBootloaderRunStates.Run_Invalid,NULL);
                  HAL_FLASH_Lock();
                  return HAL_ERROR; // 写入失败，直接返回
              }
              flash_offset +=2;
          }
          //循环写入
          for(uint16_t i=1; i<Size; i+=2)
          {
              FeedIwdg(); // 避免长时间循环导致看门狗复位
              uint32_t write_addr = update_target_addr + flash_offset;
              uint16_t halfword ;
              if(i+1 < Size)
              {
                  //正常情况
                  halfword = (data_buffer[i+1] << 8) | data_buffer[i];
                  if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, write_addr, halfword) != HAL_OK)
                  {
                      // Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Flash_Download,AllBootloaderRunStates.Run_Invalid,NULL);
                      HAL_FLASH_Lock();
                      return HAL_ERROR; // 写入失败，直接返回
                  }
                  flash_offset +=2;
              }
              else
              {
                  //数据长度为奇数,最后一个字节留下
                  last_byte = data_buffer[i];
                  last_byte_flag = 1;
              }
          }
          if(Size %2 ==1)//这次数据长度为奇数
          {
              written_data_size = Size +1; //加上上次留下的字节
              total_written_data_size += Size +1;
              last_byte_flag =0; //这次数据长度为奇数,向flash宏写入偶数个,没有留下字节
          }
          else
          {
              written_data_size = Size;
              total_written_data_size += Size;
              last_byte_flag =1; //这次数据长度为偶数,留下了字节
          }
      }
      else
      {
          //上次没有留下字节
          for(uint16_t i=0; i<Size; i+=2)
          {
              FeedIwdg(); // 避免长时间循环导致看门狗复位
              uint32_t write_addr = update_target_addr + flash_offset;
              uint16_t halfword ;
              if(i+1 < Size)
              {
                  //正常情况
                  halfword = (data_buffer[i+1] << 8) | data_buffer[i];
                  if(HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, write_addr, halfword) != HAL_OK)
                  {
                      // Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Flash_Download,AllBootloaderRunStates.Run_Invalid,NULL);
                      HAL_FLASH_Lock();
                      return HAL_ERROR; // 写入失败，直接返回
                  }
                  flash_offset +=2;
              }
              else
              {
                  //数据长度为奇数,最后一个字节留下
                  last_byte = data_buffer[i];
                  last_byte_flag = 1;
              }
          }
          if(Size %2 ==0)
          {
              written_data_size = Size;
              total_written_data_size += Size;
              last_byte_flag =0; //这次数据长度为偶数,没有留下字节
          }
          else
          {
              written_data_size = Size -1; //减去留下的字节
              total_written_data_size += Size -1;
              last_byte_flag =1; //这次数据长度为奇数,留下了字节
          }
      }


      //操作完成后锁定flash
      HAL_FLASH_Lock();
      is_data_buffer_full=0;
      data_buffer_offset=0;
      //清除缓冲区,准备下一次接收
      //memset((void*)data_buffer, 0, 256);
      //USB数据已经由USB中断处理，无需重新启动接收
    return HAL_OK;

}

/**
 * @brief 擦除flash（检查每个扇区前256字节，非FF则擦除该扇区）
 *        注意：此函数会长时间阻塞，必须喂狗
 * @param flash_start_addr 需要擦除flash的起始地址
 * @param Size 需要的flash大小（字节）
 * @return 成功擦除返回0
 */
HAL_StatusTypeDef FlashErase(uint32_t flash_start_addr, uint32_t Size)
{
    if (Size == 0) {
        return HAL_OK;
    }

    HAL_FLASH_Unlock();
    // 清除错误标志
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR |
                          FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                          FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    // 喂狗
    FeedIwdg();

    // 如果是要擦除A区，检查A区的每个扇区
    if (flash_start_addr >= APP_A_START_ADDR && flash_start_addr <= APP_A_END_ADDR) {
        // 检查A区的扇区6
        uint32_t sector6_addr = APP_A_START_ADDR;
        uint32_t *sector6_start = (uint32_t *)sector6_addr;
        uint8_t needs_erase_sector6 = 0;
        
        // 检查扇区6的前64个字（256字节）
        for (int i = 0; i < 64 && i < SECTOR_SIZE_6 / 4; i++) {
            if (sector6_start[i] != 0xFFFFFFFF) {
                needs_erase_sector6 = 1;
                break;
            }
        }
        
        if (needs_erase_sector6) {
            Flash_EraseSectorByAddress_NoLock(sector6_addr);
            FeedIwdg(); // 擦除后喂狗
        }
        
        // 检查A区的扇区7
        uint32_t sector7_addr = APP_A_START_ADDR + SECTOR_SIZE_6;
        uint32_t *sector7_start = (uint32_t *)sector7_addr;
        uint8_t needs_erase_sector7 = 0;
        
        // 检查扇区7的前64个字（256字节）
        for (int i = 0; i < 64 && i < SECTOR_SIZE_7 / 4; i++) {
            if (sector7_start[i] != 0xFFFFFFFF) {
                needs_erase_sector7 = 1;
                break;
            }
        }
        
        if (needs_erase_sector7) {
            Flash_EraseSectorByAddress_NoLock(sector7_addr);
            FeedIwdg(); // 擦除后喂狗
        }
    }
    // 如果是要擦除B区，检查B区的每个扇区
    else if (flash_start_addr >= APP_B_START_ADDR && flash_start_addr <= APP_B_END_ADDR) {
        // 检查B区的扇区2
        uint32_t sector2_addr = APP_B_START_ADDR;
        uint32_t *sector2_start = (uint32_t *)sector2_addr;
        uint8_t needs_erase_sector2 = 0;
        
        // 检查扇区2的前64个字（256字节）
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
        
        // 检查B区的扇区3
        uint32_t sector3_addr = APP_B_START_ADDR + SECTOR_SIZE_2;
        uint32_t *sector3_start = (uint32_t *)sector3_addr;
        uint8_t needs_erase_sector3 = 0;
        
        // 检查扇区3的前64个字（256字节）
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
        
        // 检查B区的扇区4
        uint32_t sector4_addr = APP_B_START_ADDR + SECTOR_SIZE_2 + SECTOR_SIZE_3;
        uint32_t *sector4_start = (uint32_t *)sector4_addr;
        uint8_t needs_erase_sector4 = 0;
        
        // 检查扇区4的前64个字（256字节）
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
        
        // 检查B区的扇区5
        uint32_t sector5_addr = APP_B_START_ADDR + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4;
        uint32_t *sector5_start = (uint32_t *)sector5_addr;
        uint8_t needs_erase_sector5 = 0;
        
        // 检查扇区5的前64个字（256字节）
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
    // 对于其他区域，按需擦除
    else {
        // 确定要擦除的扇区范围
        uint32_t start_sector_addr = Flash_GetSectorStartAddressByAddress(flash_start_addr);
        uint32_t end_addr = flash_start_addr + Size - 1;
        uint32_t end_sector_addr = Flash_GetSectorStartAddressByAddress(end_addr);

        // 检查并擦除范围内的所有扇区
        uint32_t current_addr = start_sector_addr;
        while (current_addr <= end_sector_addr && current_addr != 0) {
            FeedIwdg(); // 每次循环喂狗

            // 检查扇区的前256个字节是否为0xFFFFFFFF
            uint8_t needs_erase = 0;
            uint32_t *sector_start = (uint32_t *)current_addr;
            
            // 获取当前扇区的大小
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
                // 检查扇区的前64个字（256字节），但不超过扇区大小
                uint32_t max_words_to_check = (sector_size < 256) ? sector_size / 4 : 64;
                
                for (int i = 0; i < max_words_to_check; i++) {
                    if (sector_start[i] != 0xFFFFFFFF) {
                        needs_erase = 1;
                        break;
                    }
                }

                // 如果需要擦除，则擦除整个扇区
                if (needs_erase) {
                    Flash_EraseSectorByAddress_NoLock(current_addr);
                    FeedIwdg(); // 擦除后喂狗
                }
            }

            // 移动到下一个扇区
            if (current_addr == FLASH_BASE_ADDR) current_addr = FLASH_BASE_ADDR + SECTOR_SIZE_0;
            else if (current_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0) current_addr = FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1;
            else if (current_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1) current_addr = FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2;
            else if (current_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2) current_addr = FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3;
            else if (current_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3) current_addr = FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4;
            else if (current_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4) current_addr = FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5;
            else if (current_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5) current_addr = FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5 + SECTOR_SIZE_6;
            else if (current_addr == FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5 + SECTOR_SIZE_6) current_addr = FLASH_BASE_ADDR + SECTOR_SIZE_0 + SECTOR_SIZE_1 + SECTOR_SIZE_2 + SECTOR_SIZE_3 + SECTOR_SIZE_4 + SECTOR_SIZE_5 + SECTOR_SIZE_6 + SECTOR_SIZE_7;
            else break; // 未知扇区地址
        }
    }

    HAL_FLASH_Lock();
    return HAL_OK;
}

/**
 * @brief  复制B区程序到A区
 * @retval 0成功，非0失败
 */
HAL_StatusTypeDef CopyBtoA(void)
{   
    uint32_t src_addr = APP_B_START_ADDR;
    uint32_t dst_addr = APP_A_START_ADDR;
    uint32_t size = APP_B_SIZE < APP_A_SIZE ? APP_B_SIZE : APP_A_SIZE; // 取较小的尺寸
    
    // 擦除A区
    if (FlashErase(dst_addr, size) != HAL_OK) {
        Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Flash_Erasure,AllBootloaderRunStates.Run_Invalid,NULL);
        return HAL_ERROR;
    }
    
    // 解锁Flash
    HAL_FLASH_Unlock();
    
    // 逐字复制数据
    for (uint32_t offset = 0; offset < size; offset += 4) {
        uint32_t data = *(volatile uint32_t*)(src_addr + offset);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, dst_addr + offset, data) != HAL_OK) {
            Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Flash_Download,AllBootloaderRunStates.Run_Invalid,NULL);
            HAL_FLASH_Lock();
            return HAL_ERROR;
        }
    }
    
    // 锁定Flash
    HAL_FLASH_Lock();

    // 计算B区和A区的CRC值进行比较
    uint32_t crc_b = CalculateCRC32((const uint8_t*)src_addr, size);
    uint32_t crc_a = CalculateCRC32((const uint8_t*)dst_addr, size);

    print_uint32_with_label("crc_a",crc_a);//调试输出
    print_uint32_with_label("crc_b",crc_b);//调试输出

    if(crc_b != crc_a) {


        // CRC校验失败
        uint8_t crc_data[4];
        crc_data[0] = crc_b & 0xFF;
        crc_data[1] = (crc_b >> 8) & 0xFF;
        crc_data[2] = (crc_b >> 16) & 0xFF;
        crc_data[3] = (crc_b >> 24) & 0xFF;
        Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_CRC,AllBootloaderRunStates.Run_Invalid,crc_data);
        BootloaderErrorInterFunc(); // 进入错误处理函数
        return HAL_ERROR;
    }

    return HAL_OK;
}

/**
 * @brief  复制A区程序到B区（备份）
 * @retval 0成功，非0失败
 */
uint8_t CopyAtoB(void)
 {
//     uint32_t src_addr = APP_A_START_ADDR;
//     uint32_t dst_addr = APP_B_START_ADDR;
//     uint32_t size = APP_A_SIZE < APP_B_SIZE ? APP_A_SIZE : APP_B_SIZE; // 取较小的尺寸
    
//     // 检查B区是否有足够的空间
//     if (size < 0x1000) { // 至少需要4KB空间
//         USB_Send_Data((uint8_t*)"B partition too small for backup\r\n", 34);
//         return 1;
//     }
    
//     // 擦除B区
//     if (FlashErase(dst_addr, size) != 0) {
//         USB_Send_Data((uint8_t*)"Failed to erase B partition for backup\r\n", 40);
//         return 1;
//     }
    
//     // 解锁Flash
//     HAL_FLASH_Unlock();
    
//     // 逐字复制数据
//     for (uint32_t offset = 0; offset < size; offset += 4) {
//         uint32_t data = *(volatile uint32_t*)(src_addr + offset);
//         if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, dst_addr + offset, data) != HAL_OK) {
//             USB_Send_Data((uint8_t*)"Failed to copy data from A to B\r\n", 34);
//             HAL_FLASH_Lock();
//             return 1;
//         }
//     }
    

//     // 锁定Flash
//     HAL_FLASH_Lock();
    

//     USB_Send_Data((uint8_t*)"Successfully backed up A partition to B partition\r\n", 52);
     return 0;
}



/**
 * @brief  计算CRC32校验值 (使用CRC32-CRC算法)
 * @param  data 数据指针
 * @param  length 数据长度
 * @retval CRC32校验值
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
 * @brief  喂独立看门狗
 */
void FeedIwdg(void)
{
    HAL_IWDG_Refresh(&hiwdg);
}

/**
 * @brief  检查并更新看门狗状态
 */
void CheckAndUpdateWatchdog(void)
{
    // 在正常操作中定期喂狗
    HAL_IWDG_Refresh(&hiwdg);
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
    static uint8_t packet_buffer[16]; // 使用static避免栈内存失效问题
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

    uint32_t start_tick = HAL_GetTick();
    while (CDC_Transmit_FS((uint8_t*)packet_buffer, packet_len) == USBD_BUSY)
    {
        // 简单的超时与重试机制
        if (HAL_GetTick() - start_tick > 1000) // 1秒超时
        {
            return HAL_TIMEOUT;
        }
    }
    
    return HAL_OK;
}



/**
 * @brief 解析接收到的数据包
 * @param ReceivedPacket: 指向接收到的数据包的指针
 * @return 处理结果
 **/
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

/**
 * @brief 所有的Bootloader类的错误都会进入这个函数,可以在函数内输出日志
 * 
 */
void BootloaderErrorInterFunc(void){

    HAL_UART_Transmit(&huart1,(uint8_t*)"[FATAL] Bootloader Error! Resetting...\r\n",40,100);
    // 发生严重错误，等待复位
    HAL_Delay(500); // 等待串口发送完成
    // 不喂狗，让看门狗复位系统
    // 或者主动复位
    HAL_NVIC_SystemReset();
}

