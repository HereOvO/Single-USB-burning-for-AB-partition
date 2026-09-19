#include "main.h"
#include "iwdg.h"
#include "tim.h"
#include "usart.h"
#include "spi.h"
#include "usbd_cdc_if.h"
#include "Bootloader.h"

uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];
uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];
uint16_t rx_read_pos = 0;
uint16_t tx_send_pos = 0;

uint32_t total_size_to_receive = 0;
volatile uint16_t received_data_size = 0;
volatile uint32_t total_received_data_size = 0;
volatile uint16_t written_data_size = 0;
volatile uint32_t total_written_data_size = 0;
uint32_t flash_offset = 0;
volatile uint8_t data_buffer[612] = {0};
uint8_t last_byte_flag = 0;
uint8_t last_byte = 0;
volatile uint8_t is_data_buffer_full = 0;
volatile uint16_t data_buffer_offset = 0;

uint8_t is_start_transmission = 0;
uint8_t is_transmission_complete = 0;
uint8_t is_jump_to_application = 0;
uint8_t first_reception = 0;
volatile uint16_t TimerCounter_ms = 0;
uint32_t expected_crc_value = 0;
uint32_t calculated_crc_value = 0;
BootloaderPacket_t BootloaderPacket_TX;
BootloaderPacket_t BootloaderPacket_RX;
NewFirmwareInformation_t NewFirmwareInformation;

AllERRORs_t AllERRORs = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
AllCmds_t AllCMDs = {0, 1, 2, 3, 4, 5, 6, 7, &AllERRORs};
AllBootloaderStartModes_t AllBootloaderStartModes = {0, 1, 2, 3};
AllBootloaderRunStates_t AllBootloaderRunStates = {0, 1, 2, 3, 4, 5, 6, 7};

volatile BootloaderState_t BootloaderState;

