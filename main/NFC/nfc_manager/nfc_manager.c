#include "nfc_manager.h"
#include "si522.h"
#include "i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "NFC_Manager"

static bool nfc_initialized = false;

bool nfc_manager_init(i2c_master_bus_handle_t i2c_bus, uint16_t nfc_addr)
{
    if (nfc_initialized) {
        ESP_LOGW(TAG, "NFC already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing NFC manager...");
    ESP_LOGI(TAG, "Using shared I2C bus, NFC Addr: 0x%02X", nfc_addr);

    // 使用共享的I2C总线初始化NFC设备
    esp_err_t ret = i2c_Init_With_Bus(i2c_bus, nfc_addr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C device initialization failed: %s (0x%x)", esp_err_to_name(ret), ret);
        return false;
    }

    // 初始化SI522
    SI522_Init(SI522_1);

    // 验证SI522是否正常工作
    uint8_t version = SI522_ReadRegister(VersionReg, SI522_1);
    ESP_LOGI(TAG, "SI522 Version: 0x%02X", version);

    if (version == 0x00 || version == 0xFF) {
        ESP_LOGE(TAG, "SI522 initialization failed - invalid version register");
        return false;
    }

    nfc_initialized = true;
    ESP_LOGI(TAG, "NFC manager initialized successfully");
    return true;
}

bool nfc_manager_detect_card(uint8_t *uid, uint8_t *uid_len)
{
    if (!nfc_initialized) {
        ESP_LOGE(TAG, "NFC not initialized");
        return false;
    }

    uint8_t tag_type[2];
    uint8_t serial_num[5];

    // 寻卡 - 只用PICC_REQIDL检测未激活的卡
    ESP_LOGD(TAG, "Step 1: Requesting card with PICC_REQIDL...");
    uint8_t req_status = SI522_Request(PICC_REQIDL, tag_type, SI522_1);
    if (req_status != 1) {
        ESP_LOGD(TAG, "PICC_REQIDL failed (status=%d), no card detected", req_status);
        return false;
    }
    ESP_LOGD(TAG, "PICC_REQIDL success, tag_type: 0x%02X%02X", tag_type[0], tag_type[1]);

    // 防冲突，获取卡片序列号
    ESP_LOGD(TAG, "Step 2: Anti-collision to get UID...");
    uint8_t anticoll_status = SI522_Anticoll(serial_num, SI522_1);
    if (anticoll_status != 1) {
        ESP_LOGD(TAG, "Anti-collision failed (status=%d)", anticoll_status);
        return false;
    }
    ESP_LOGD(TAG, "Anti-collision success, UID: %02X %02X %02X %02X (checksum: %02X)", 
             serial_num[0], serial_num[1], serial_num[2], serial_num[3], serial_num[4]);

    // 复制UID
    if (uid != NULL && uid_len != NULL) {
        for (int i = 0; i < 4; i++) {
            uid[i] = serial_num[i];
        }
        *uid_len = 4;
    }

    // 不选择卡片，不Halt，保持卡片在当前状态
    // 这样下次检测时卡片仍然可以被PICC_REQIDL检测到
    
    ESP_LOGD(TAG, "✓ Card detected - UID: %02X%02X%02X%02X", 
             serial_num[0], serial_num[1], serial_num[2], serial_num[3]);

    return true;
}

void nfc_manager_reset_chip(void)
{
    if (!nfc_initialized) {
        ESP_LOGW(TAG, "NFC not initialized, cannot reset");
        return;
    }
    
    ESP_LOGD(TAG, "Resetting SI522 chip...");
    
    // 1. 关闭射频场
    ESP_LOGD(TAG, "Turning off RF field...");
    SI522_AntennaOff(SI522_1);
    vTaskDelay(pdMS_TO_TICKS(10));  // 等待10ms让射频场完全关闭
    
    // 2. 重新初始化芯片（会自动开启射频场）
    ESP_LOGD(TAG, "Re-initializing SI522...");
    SI522_Init(SI522_1);
    
    ESP_LOGD(TAG, "SI522 chip reset complete, RF field is ON");
}

bool nfc_manager_wait_for_card(uint32_t timeout_ms, uint8_t *uid, uint8_t *uid_len)
{
    if (!nfc_initialized) {
        ESP_LOGE(TAG, "NFC not initialized");
        return false;
    }

    ESP_LOGI(TAG, "Waiting for NFC card...");

    uint32_t start_time = xTaskGetTickCount();
    uint32_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    while (true) {
        // 检测卡片
        if (nfc_manager_detect_card(uid, uid_len)) {
            ESP_LOGI(TAG, "NFC card detected!");
            return true;
        }

        // 检查超时
        if (timeout_ms != 0) {
            uint32_t elapsed = xTaskGetTickCount() - start_time;
            if (elapsed >= timeout_ticks) {
                ESP_LOGW(TAG, "NFC card detection timeout");
                return false;
            }
        }

        // 延迟一段时间再检测
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

bool nfc_manager_read_block(uint8_t block_addr, uint8_t *data, uint8_t *uid)
{
    if (!nfc_initialized) {
        ESP_LOGE(TAG, "NFC not initialized");
        return false;
    }

    if (data == NULL || uid == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }

    // 认证
    uint8_t default_key[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (SI522_Auth(PICC_AUTHENT1A, block_addr, default_key, uid, SI522_1) != 1) {
        ESP_LOGE(TAG, "Authentication failed for block %d", block_addr);
        return false;
    }

    // 读取数据
    if (SI522_Read(block_addr, data, SI522_1) != 1) {
        ESP_LOGE(TAG, "Read failed for block %d", block_addr);
        return false;
    }

    ESP_LOGI(TAG, "Block %d read successfully", block_addr);
    ESP_LOG_BUFFER_HEX(TAG, data, 16);

    return true;
}
