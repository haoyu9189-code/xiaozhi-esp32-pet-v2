#include "si522.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "i2c.h"

#define TAG "SI522"

void SI522_WriteRegister(uint8_t addr, uint8_t val, si522_t dev)
{
    i2c_setRegister(NFC1, addr, val);
}

uint8_t SI522_ReadRegister(uint8_t addr, si522_t dev)
{
    return i2c_getRegister(NFC1, addr);
}

void SI522_SetBitMask(uint8_t reg, uint8_t mask, si522_t dev)
{
    uint8_t tmp = SI522_ReadRegister(reg,dev);
    SI522_WriteRegister(reg, tmp | mask,dev);
}

void SI522_ClearBitMask(uint8_t reg, uint8_t mask, si522_t dev)
{
    uint8_t tmp = SI522_ReadRegister(reg, dev);
    SI522_WriteRegister(reg, tmp & (~mask), dev);
}

// 初始化函数 - 现在需要指定设备
void SI522_Init(si522_t dev)
{
    SI522_Reset(dev);

    // 定时器配置
    SI522_WriteRegister(TModeReg, 0x8D, dev);
    SI522_WriteRegister(TPrescalerReg, 0x3E, dev);
    SI522_WriteRegister(TReloadRegL, 30, dev);
    SI522_WriteRegister(TReloadRegH, 0, dev);

    // RF配置
    SI522_WriteRegister(TxASKReg, 0x40, dev);
    SI522_WriteRegister(ModeReg, 0x3D, dev);

    // 开启天线
    SI522_AntennaOn(dev);
}

void SI522_Reset(si522_t dev)
{
    SI522_WriteRegister(CommandReg, PCD_RESETPHASE, dev);
    vTaskDelay(pdMS_TO_TICKS(50));
}

// 开启天线（射频场）
void SI522_AntennaOn(si522_t dev)
{
    uint8_t val = SI522_ReadRegister(TxControlReg, dev);
    if (!(val & 0x03))
    {
        SI522_SetBitMask(TxControlReg, 0x03, dev);
    }
}

// 关闭天线（射频场）
void SI522_AntennaOff(si522_t dev)
{
    SI522_ClearBitMask(TxControlReg, 0x03, dev);
}

// CRC计算 - 需要指定设备
void SI522_CalculateCRC(uint8_t *data, uint8_t length, uint8_t *result, si522_t dev)
{
    SI522_WriteRegister(CommandReg, PCD_IDLE, dev);
    SI522_WriteRegister(DivIrqReg, 0x04, dev);
    SI522_WriteRegister(FIFOLevelReg, 0x80, dev);

    for (uint8_t i = 0; i < length; i++)
    {
        SI522_WriteRegister(FIFODataReg, data[i], dev);
    }

    SI522_WriteRegister(CommandReg, PCD_CALCCRC, dev);

    uint16_t i = 5000;
    while (i--)
    {
        if (SI522_ReadRegister(DivIrqReg, dev) & 0x04)
        {
            break;
        }
        vTaskDelay(1);
    }

    result[0] = SI522_ReadRegister(CRCResultRegL, dev);
    result[1] = SI522_ReadRegister(CRCResultRegH, dev);
}

// 数据收发函数 - 需要指定设备
uint8_t SI522_ToCard(uint8_t command, uint8_t *sendData, uint8_t sendLen, uint8_t *backData, uint16_t *backLen, si522_t dev)
{
    uint8_t status = 0;
    uint8_t irqEn = 0x00;
    uint8_t waitIRq = 0x00;
    uint8_t lastBits;
    uint8_t n;
    uint16_t i;

    if (command == PCD_AUTHENT)
    {
        irqEn = 0x12;
        waitIRq = 0x10;
    }
    if (command == PCD_TRANSCEIVE)
    {
        irqEn = 0x77;
        waitIRq = 0x30;
    }

    SI522_WriteRegister(CommIEnReg, irqEn | 0x80, dev);
    SI522_ClearBitMask(CommIrqReg, 0x80, dev);
    SI522_SetBitMask(FIFOLevelReg, 0x80, dev);

    SI522_WriteRegister(CommandReg, PCD_IDLE, dev);

    for (i = 0; i < sendLen; i++)
    {
        SI522_WriteRegister(FIFODataReg, sendData[i], dev);
    }

    SI522_WriteRegister(CommandReg, command, dev);

    if (command == PCD_TRANSCEIVE)
    {
        SI522_SetBitMask(BitFramingReg, 0x80, dev);
    }

    i = 2000;
    while (1)
    {
        n = SI522_ReadRegister(CommIrqReg, dev);
        if (n & waitIRq)
        {
            break;
        }
        if (n & 0x01)
        {
            break;
        }
        if (--i == 0)
        {
            break;
        }
    }

    SI522_ClearBitMask(BitFramingReg, 0x80, dev);

    if (i != 0)
    {
        if ((SI522_ReadRegister(ErrorReg, dev) & 0x1B) == 0x00)
        {
            status = 1;
            if (n & irqEn & 0x01)
            {
                status = 0;
            }

            if (command == PCD_TRANSCEIVE)
            {
                n = SI522_ReadRegister(FIFOLevelReg, dev);
                lastBits = SI522_ReadRegister(ControlReg, dev) & 0x07;
                if (lastBits)
                {
                    *backLen = (n - 1) * 8 + lastBits;
                }
                else
                {
                    *backLen = n * 8;
                }

                if (n == 0)
                {
                    n = 1;
                }
                if (n > 16)
                {
                    n = 16;
                }

                for (i = 0; i < n; i++)
                {
                    backData[i] = SI522_ReadRegister(FIFODataReg, dev);
                }
            }
        }
        else
        {
            status = 0;
        }
    }

    return status;
}

// 寻卡
uint8_t SI522_Request(uint8_t reqMode, uint8_t *TagType, si522_t dev)
{
    uint16_t backBits;
    SI522_WriteRegister(BitFramingReg, 0x07,dev);
    TagType[0] = reqMode;
    uint8_t status = SI522_ToCard(PCD_TRANSCEIVE, TagType, 1, TagType, &backBits,dev);
    if ((status != 1) || (backBits != 0x10))
    {
        status = 0;
    }
    return status;
}

uint8_t SI522_Anticoll(uint8_t *serNum, si522_t dev)
{
    SI522_WriteRegister(BitFramingReg, 0x00,dev);
    serNum[0] = PICC_ANTICOLL;
    serNum[1] = 0x20;
    uint16_t unLen;
    uint8_t status = SI522_ToCard(PCD_TRANSCEIVE, serNum, 2, serNum, &unLen,dev);

    if (status == 1)
    {
        uint8_t serNumCheck = 0;
        for (uint8_t i = 0; i < 4; i++)
        {
            serNumCheck ^= serNum[i];
        }
        if (serNumCheck != serNum[4])
            status = 0;
    }
    return status;
}

// 选择卡片
uint8_t SI522_SelectTag(uint8_t *serNum, si522_t dev)
{
    uint8_t i;
    uint8_t status;
    uint8_t size;
    uint16_t recvBits;
    uint8_t buffer[9];

    buffer[0] = PICC_SELECTTAG;
    buffer[1] = 0x70;
    for (i = 0; i < 5; i++)
    {
        buffer[i + 2] = serNum[i];
    }
    SI522_CalculateCRC(buffer, 7, &buffer[7],dev);

    status = SI522_ToCard(PCD_TRANSCEIVE, buffer, 9, buffer, &recvBits,dev);

    if ((status == 1) && (recvBits == 0x18))
    {
        size = buffer[0];
    }
    else
    {
        size = 0;
    }

    return size;
}

// 认证卡片
uint8_t SI522_Auth(uint8_t authMode, uint8_t BlockAddr, uint8_t *Sectorkey, uint8_t *serNum, si522_t dev)
{
    uint8_t status;
    uint16_t recvBits;
    uint8_t i;
    uint8_t buff[12];

    buff[0] = authMode;
    buff[1] = BlockAddr;
    for (i = 0; i < 6; i++)
    {
        buff[i + 2] = Sectorkey[i];
    }
    for (i = 0; i < 4; i++)
    {
        buff[i + 8] = serNum[i];
    }

    status = SI522_ToCard(PCD_AUTHENT, buff, 12, buff, &recvBits,dev);

    if ((status != 1) || (!(SI522_ReadRegister(Status2Reg,dev) & 0x08)))
    {
        status = 0;
    }

    return status;
}

// 读取块数据
uint8_t SI522_Read(uint8_t blockAddr, uint8_t *recvData, si522_t dev)
{
    uint8_t status;
    uint16_t unLen;

    recvData[0] = PICC_READ;
    recvData[1] = blockAddr;
    SI522_CalculateCRC(recvData, 2, &recvData[2],dev);

    status = SI522_ToCard(PCD_TRANSCEIVE, recvData, 4, recvData, &unLen,dev);

    if ((status != 1) || (unLen != 0x90))
    {
        status = 0;
    }

    return status;
}

// 停止卡片
void SI522_Halt(si522_t dev)
{
    uint8_t status;
    uint16_t unLen;
    uint8_t buff[4];

    buff[0] = PICC_HALT;
    buff[1] = 0;
    SI522_CalculateCRC(buff, 2, &buff[2],dev);

    status = SI522_ToCard(PCD_TRANSCEIVE, buff, 4, buff, &unLen,dev);
}

void printHex(uint8_t *data, uint8_t length)
{
    for (uint8_t i = 0; i < length; i++)
    {
        printf("%02X ", data[i]);
    }
}
