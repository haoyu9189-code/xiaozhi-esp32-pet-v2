#ifndef NFC_MANAGER_H
#define NFC_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化NFC管理器（使用外部I2C总线）
 * @param i2c_bus I2C总线句柄
 * @param nfc_addr NFC设备I2C地址
 * @return true 初始化成功, false 初始化失败
 */
bool nfc_manager_init(i2c_master_bus_handle_t i2c_bus, uint16_t nfc_addr);

/**
 * @brief 重置NFC芯片（重新初始化SI522）
 */
void nfc_manager_reset_chip(void);

/**
 * @brief 检测NFC卡片
 * @param uid 输出参数，存储卡片UID (4字节)
 * @param uid_len 输出参数，存储UID长度
 * @return true 检测到卡片, false 未检测到卡片
 */
bool nfc_manager_detect_card(uint8_t *uid, uint8_t *uid_len);

/**
 * @brief 等待NFC卡片
 * @param timeout_ms 超时时间(毫秒)，0表示无限等待
 * @param uid 输出参数，存储卡片UID (4字节)
 * @param uid_len 输出参数，存储UID长度
 * @return true 检测到卡片, false 超时
 */
bool nfc_manager_wait_for_card(uint32_t timeout_ms, uint8_t *uid, uint8_t *uid_len);

/**
 * @brief 读取NFC卡片数据块
 * @param block_addr 块地址
 * @param data 输出参数，存储读取的数据 (16字节)
 * @param uid 卡片UID (4字节)
 * @return true 读取成功, false 读取失败
 */
bool nfc_manager_read_block(uint8_t block_addr, uint8_t *data, uint8_t *uid);

#ifdef __cplusplus
}
#endif

#endif // NFC_MANAGER_H
