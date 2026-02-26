#ifndef SI522_H
#define SI522_H

#include <stdint.h>
#include <stdbool.h>

// SI522寄存器定义
#define PCD_IDLE 0x00
#define PCD_AUTHENT 0x0E
#define PCD_RECEIVE 0x08
#define PCD_TRANSMIT 0x04
#define PCD_TRANSCEIVE 0x0C
#define PCD_RESETPHASE 0x0F
#define PCD_CALCCRC 0x03

// SI522命令定义
#define PICC_REQIDL 0x26 // 寻卡（仅检测未休眠的卡）
#define PICC_REQALL 0x52 // 寻卡（检测所有卡，包括已激活的）
#define PICC_ANTICOLL 0x93
#define PICC_SELECTTAG 0x93
#define PICC_AUTHENT1A 0x60
#define PICC_AUTHENT1B 0x61
#define PICC_READ 0x30
#define PICC_WRITE 0xA0
#define PICC_DECREMENT 0xC0
#define PICC_INCREMENT 0xC1
#define PICC_RESTORE 0xC2
#define PICC_TRANSFER 0xB0
#define PICC_HALT 0x50

// SI522寄存器地址
#define Reserved00 0x00
#define CommandReg 0x01
#define CommIEnReg 0x02
#define DivlEnReg 0x03
#define CommIrqReg 0x04
#define DivIrqReg 0x05
#define ErrorReg 0x06
#define Status1Reg 0x07
#define Status2Reg 0x08
#define FIFODataReg 0x09
#define FIFOLevelReg 0x0A
#define WaterLevelReg 0x0B
#define ControlReg 0x0C
#define BitFramingReg 0x0D
#define CollReg 0x0E
#define Reserved01 0x0F
#define Reserved10 0x10
#define ModeReg 0x11
#define TxModeReg 0x12
#define RxModeReg 0x13
#define TxControlReg 0x14
#define TxASKReg 0x15
#define TxSelReg 0x16
#define RxSelReg 0x17
#define RxThresholdReg 0x18
#define DemodReg 0x19
#define Reserved11 0x1A
#define Reserved12 0x1B
#define MifareReg 0x1C
#define Reserved13 0x1D
#define Reserved14 0x1E
#define SerialSpeedReg 0x1F
#define Reserved20 0x20
#define CRCResultRegH 0x21
#define CRCResultRegL 0x22
#define Reserved21 0x23
#define ModWidthReg 0x24
#define Reserved22 0x25
#define RFCfgReg 0x26
#define GsNReg 0x27
#define CWGsPReg 0x28
#define ModGsPReg 0x29
#define TModeReg 0x2A
#define TPrescalerReg 0x2B
#define TReloadRegH 0x2C
#define TReloadRegL 0x2D
#define TCounterValueRegH 0x2E
#define TCounterValueRegL 0x2F
#define Reserved30 0x30
#define TestSel1Reg 0x31
#define TestSel2Reg 0x32
#define TestPinEnReg 0x33
#define TestPinValueReg 0x34
#define TestBusReg 0x35
#define AutoTestReg 0x36
#define VersionReg 0x37
#define AnalogTestReg 0x38
#define TestDAC1Reg 0x39
#define TestDAC2Reg 0x3A
#define TestADCReg 0x3B
#define Reserved31 0x3C
#define Reserved32 0x3D
#define Reserved33 0x3E
#define Reserved34 0x3F

// 认证密钥
static uint8_t defaultKey[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

//NFC外设
typedef enum{
    SI522_1 = 1,
    SI522_2,
}si522_t;

typedef enum
{
    RC522_PICC_TYPE_UNKNOWN = -1,
    RC522_PICC_TYPE_UNDEFINED = 0,
    RC522_PICC_TYPE_ISO_14443_4,     // PICC compliant with ISO/IEC 14443-4
    RC522_PICC_TYPE_ISO_18092,       // PICC compliant with ISO/IEC 18092 (NFC)
    RC522_PICC_TYPE_MIFARE_MINI,     // MIFARE Classic protocol, 320 bytes
    RC522_PICC_TYPE_MIFARE_1K,       // MIFARE Classic protocol, 1KB
    RC522_PICC_TYPE_MIFARE_4K,       // MIFARE Classic protocol, 4KB
    RC522_PICC_TYPE_MIFARE_UL,       // MIFARE Ultralight or Ultralight C
    RC522_PICC_TYPE_MIFARE_PLUS,     // MIFARE Plus
    RC522_PICC_TYPE_MIFARE_DESFIRE,  // MIFARE DESFire
    RC522_PICC_TYPE_TNP3XXX,         // Only mentioned in NXP AN 10833 MIFARE Type Identification Procedure
    RC522_PICC_TYPE_MIFARE_UL_,      // Ultralight MF0ICU1; 16x 4-byte pages
    RC522_PICC_TYPE_MIFARE_UL_C,     // Ultralight C MF0ICU2; 48x 4-byte pages
    RC522_PICC_TYPE_MIFARE_UL_EV1_1, // Ultralight EV1 MF0UL11; 20x 4-byte pages
    RC522_PICC_TYPE_MIFARE_UL_EV1_2, // Ultralight EV1 MF0UL21; 41x 4-byte pages
    RC522_PICC_TYPE_MIFARE_UL_NANO,  // Ultralight NANO; 40 bytes (probably 10x 4-byte pages)
    RC522_PICC_TYPE_MIFARE_UL_AES,   // Ultralight AES MF0AES(H)20; 60x 4-byte pages
    RC522_PICC_TYPE_NTAG2xx,         // NTAG 2xx; further information unknown
    RC522_PICC_TYPE_NTAG213,         // NTAG213; 45x 4-byte pages
    RC522_PICC_TYPE_NTAG215,         // NTAG215; 135x 4-byte pages
    RC522_PICC_TYPE_NTAG216,         // NTAG216; 231x 4-byte pages
} rc522_picc_type_t;

// 函数声明
void SI522_Init(si522_t dev);
void SI522_Reset(si522_t dev);
void SI522_AntennaOn(si522_t dev);
void SI522_AntennaOff(si522_t dev);
void SI522_WriteRegister(uint8_t addr, uint8_t val, si522_t dev);
uint8_t SI522_ReadRegister(uint8_t addr, si522_t dev);
void SI522_SetBitMask(uint8_t reg, uint8_t mask, si522_t dev);
void SI522_ClearBitMask(uint8_t reg, uint8_t mask, si522_t dev);
void SI522_CalculateCRC(uint8_t *data, uint8_t length, uint8_t *result, si522_t dev);
uint8_t SI522_ToCard(uint8_t command, uint8_t *sendData, uint8_t sendLen, uint8_t *backData, uint16_t *backLen, si522_t dev);
uint8_t SI522_Request(uint8_t reqMode, uint8_t *TagType, si522_t dev);
uint8_t SI522_Anticoll(uint8_t *serNum, si522_t dev);
uint8_t SI522_SelectTag(uint8_t *serNum, si522_t dev);
uint8_t SI522_Auth(uint8_t authMode, uint8_t BlockAddr, uint8_t *Sectorkey, uint8_t *serNum, si522_t dev);
uint8_t SI522_Read(uint8_t blockAddr, uint8_t *recvData, si522_t dev);
void SI522_Halt(si522_t dev);
void printHex(uint8_t *data, uint8_t length);

#endif
