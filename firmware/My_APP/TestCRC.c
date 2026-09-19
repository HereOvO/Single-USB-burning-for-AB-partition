#include <stdio.h>
#include <stdint.h>

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

int main()
{

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