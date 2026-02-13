#include <stdio.h>
#include <stdint.h>

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

int main()
{
    // 测试数据
    const char* testData = "123456789";
    uint32_t crcResult = CalculateCRC32((const uint8_t*)testData, 9);
    
    printf("CRC32 of '123456789': 0x%08X\n", crcResult);
    printf("Expected: 0xCBF43926\n");
    
    if (crcResult == 0xCBF43926) {
        printf("CRC32 calculation is correct!\n");
    } else {
        printf("CRC32 calculation is incorrect!\n");
    }
    
    return 0;
}