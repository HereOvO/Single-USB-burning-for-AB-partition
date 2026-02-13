/* USER CODE BEGIN Header */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "iwdg.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "Bootloader.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_USB_DEVICE_Init();
  MX_IWDG_Init();
  MX_SPI1_Init();
  MX_TIM14_Init();
  /* USER CODE BEGIN 2 */
  ResetAll();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  HAL_TIM_Base_Start_IT(&htim14);
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    while(1){
      FeedIwdg();
       uint32_t temp=TimerCounter_ms;
       print_uint32_with_label("TimerCounter_ms",temp);

      if(BootloaderState.StartMode==AllBootloaderStartModes.Start_Invalid){
        //还没有决定开始模式
        CDC_Transmit_FS("Waiting Start Mode...\r\n",23);
        HAL_UART_Transmit(&huart1,(uint8_t*)"Waiting Start Mode...\r\n",23,100 );
        HAL_Delay(500);
        if(TimerCounter_ms>15000){
          BootloaderState.RunState=AllBootloaderRunStates.Run_Prepare_To_Jump_To_A;
          Send_BootloaderPacket(AllCMDs.Cmd_Update_State,AllBootloaderRunStates.Run_Prepare_To_Jump_To_A,NULL);
          JumpToApplication();
        }
      }

      else if(BootloaderState.StartMode==AllBootloaderStartModes.Start_Dirrectly_Jump_To_A){
        //直接跳转A
        BootloaderState.RunState=AllBootloaderRunStates.Run_Prepare_To_Jump_To_A;
        Send_BootloaderPacket(AllCMDs.Cmd_Update_State,AllBootloaderRunStates.Run_Prepare_To_Jump_To_A,NULL);
        JumpToApplication();
      }

      else if(BootloaderState.StartMode==AllBootloaderStartModes.Start_CopyB_To_A_Jump_To_A){
        //使用B覆盖A
        BootloaderState.RunState=AllBootloaderRunStates.Run_Copy_B_To_A;
        Send_BootloaderPacket(AllCMDs.Cmd_Update_State,AllBootloaderRunStates.Run_Copy_B_To_A,NULL);
        if(CopyBtoA()!=HAL_OK){
          Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Copy_B_To_A,AllBootloaderRunStates.Run_Invalid,NULL);
        }
        JumpToApplication();
      }

      else if(BootloaderState.StartMode==AllBootloaderStartModes.Start_Updata_A){
        //升级A
        //HAL_UART_Transmit(&huart1,(uint8_t*)&total_size_to_receive,4,100 );
        if(BootloaderState.RunState==AllBootloaderRunStates.Run_Flash_Erasure){
          //擦除flash
          //HAL_UART_Transmit(&huart1,(uint8_t*)"erase flash...\r\n",16,100 );
          if(FlashErase(APP_A_START_ADDR,total_size_to_receive)!=HAL_OK){
            //擦除错误
            Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Flash_Erasure,AllBootloaderRunStates.Run_Invalid,NULL);
            //HAL_UART_Transmit(&huart1,(uint8_t*)"erase err...\r\n",14,100 );
          }
          else{
            //HAL_UART_Transmit(&huart1,(uint8_t*)"erase ack...\r\n",14,100 );
            Send_BootloaderPacket(AllCMDs.Cmd_ACK,AllBootloaderRunStates.Run_Invalid,NULL);
            BootloaderState.RunState=AllBootloaderRunStates.Run_Receiving_Bin;
          }
          
        }
        else if(BootloaderState.RunState==AllBootloaderRunStates.Run_Flash_Write){
            //烧写flash
           // HAL_UART_Transmit(&huart1,(uint8_t*)"download flash...\r\n",19,100 );
            if(Download_Flash(data_buffer_offset)==HAL_OK){
                BootloaderState.RunState=AllBootloaderRunStates.Run_Receiving_Bin;
                Send_BootloaderPacket(AllCMDs.Cmd_ACK,AllBootloaderRunStates.Run_Invalid,NULL);
            }
            else{
                Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Flash_Download,AllBootloaderRunStates.Run_Invalid,NULL);

            }
        
        }
        if(BootloaderState.RunState==AllBootloaderRunStates.Run_Receiving_Bin){
        //正在接收bin

        if(TimerCounter_ms>3000){
          if(data_buffer_offset!=0){
            //写入最后一包数据
              BootloaderState.RunState=AllBootloaderRunStates.Run_Flash_Write;
              if(Download_Flash(data_buffer_offset)==HAL_OK){
                BootloaderState.RunState=AllBootloaderRunStates.Run_Prepare_To_Jump_To_A;
                  Send_BootloaderPacket(AllCMDs.Cmd_ACK,AllBootloaderRunStates.Run_Invalid,NULL);
              }
              else{
                Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_Flash_Download,AllBootloaderRunStates.Run_Invalid,NULL);
                BootloaderErrorInterFunc();
              }
          }
          
          //计算并验证CRC
          calculated_crc_value = CalculateCRC32((const uint8_t*)APP_A_START_ADDR, total_written_data_size);
          
          if(calculated_crc_value != expected_crc_value) {
              // CRC校验失败
              uint8_t crc_data[4];
              crc_data[0] = expected_crc_value & 0xFF;
              crc_data[1] = (expected_crc_value >> 8) & 0xFF;
              crc_data[2] = (expected_crc_value >> 16) & 0xFF;
              crc_data[3] = (expected_crc_value >> 24) & 0xFF;
              Send_BootloaderPacket(AllCMDs.AllERRORs->ERROR_CRC,AllBootloaderRunStates.Run_Invalid,crc_data);
              BootloaderErrorInterFunc(); // 进入错误处理函数
          } else {
              // CRC校验成功，跳转到A
              BootloaderState.RunState=AllBootloaderRunStates.Run_Prepare_To_Jump_To_A;
              Send_BootloaderPacket(AllCMDs.Cmd_Update_State,AllBootloaderRunStates.Run_Prepare_To_Jump_To_A,NULL);
              print_uint32_with_label("total write size",total_written_data_size);
              JumpToApplication();
          }

        }
        }
      }


    }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
