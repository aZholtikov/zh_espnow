#pragma once

#include "stdio.h"
#include "string.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_now.h"

#define ZH_ESPNOW_INIT_CONFIG_DEFAULT() \
    {                                   \
        .task_priority = 4,             \
        .stack_size = 2048,             \
        .queue_size = 32,               \
        .max_attempts = 5,              \
        .wifi_interface = WIFI_IF_STA   \
    }

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        uint8_t task_priority;
        uint16_t stack_size;
        uint8_t queue_size;
        uint8_t max_attempts;
        wifi_interface_t wifi_interface;
    } zh_espnow_init_config_t;

    ESP_EVENT_DECLARE_BASE(ZH_ESPNOW);

    typedef enum
    {
        ZH_ESPNOW_ON_RECV_EVENT,
        ZH_ESPNOW_ON_SEND_EVENT
    } zh_espnow_event_type_t;

    typedef struct
    {
        uint8_t mac_addr[6];
        esp_now_send_status_t status;
    } zh_espnow_event_on_send_t;

    typedef struct
    {
        uint8_t mac_addr[6];
        uint8_t *data;
        uint8_t data_len;
    } zh_espnow_event_on_recv_t;

    /**
     * @brief      Initialize ESP-NOW interface.
     *
     * @param[in]  config  Pointer to ESP-NOW initialized configuration structure. Can point to a temporary variable.
     *
     * @return
     *              - ESP_OK if initialization was successful
     *              - ESP_ERR_WIFI_NOT_INIT if WiFi is not initialized by esp_wifi_init
     */
    esp_err_t zh_espnow_init(zh_espnow_init_config_t *config);

    /**
     * @brief      Dinitialize ESP-NOW interface.
     *
     */
    void zh_espnow_deinit(void);

    /**
     * @brief      Send ESP-NOW data.
     *
     * @param[in]  target    Pointer to a buffer containing an eight-byte target MAC. Can be NULL for broadcast.
     * @param[in]  data      Pointer to a buffer containing the data for send.
     * @param[in]  data_len  Sending data length.
     *
     */
    void zh_espnow_send(const uint8_t *target, const uint8_t *data, const uint8_t data_len);

#ifdef __cplusplus
}
#endif