#pragma once

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
        .wifi_interface = WIFI_IF_STA   \
    }

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct zh_espnow_init_config_t
    {
        uint8_t task_priority;
        uint16_t stack_size;
        uint8_t queue_size;
        wifi_interface_t wifi_interface;
    } __attribute__((packed)) zh_espnow_init_config_t;

    ESP_EVENT_DECLARE_BASE(ZH_ESPNOW);

    typedef enum zh_espnow_event_type_t
    {
        ZH_ESPNOW_ON_RECV_EVENT,
        ZH_ESPNOW_ON_SEND_EVENT
    } __attribute__((packed)) zh_espnow_event_type_t;

    typedef enum zh_espnow_on_send_event_type_t
    {
        ZH_ESPNOW_SEND_SUCCESS,
        ZH_ESPNOW_SEND_FAIL
    } __attribute__((packed)) zh_espnow_on_send_event_type_t;

    typedef struct zh_espnow_event_on_send_t
    {
        uint8_t mac_addr[ESP_NOW_ETH_ALEN];
        zh_espnow_on_send_event_type_t status;
    } __attribute__((packed)) zh_espnow_event_on_send_t;

    typedef struct zh_espnow_event_on_recv_t
    {
        uint8_t mac_addr[ESP_NOW_ETH_ALEN];
        uint8_t *data;
        uint8_t data_len;
    } __attribute__((packed)) zh_espnow_event_on_recv_t;

    /**
     * @brief      Initialize ESP-NOW interface.
     *
     * @param[in]  config  Pointer to ESP-NOW initialized configuration structure. Can point to a temporary variable.
     *
     * @return
     *              - ESP_OK if initialization was success
     *              - ESP_ERR_INVALID_ARG if parameter error
     *              - ESP_ERR_WIFI_NOT_INIT if WiFi is not initialized by esp_wifi_init
     */
    esp_err_t zh_espnow_init(zh_espnow_init_config_t *config);

    /**
     * @brief      Deinitialize ESP-NOW interface.
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
     * @return
     *              - ESP_OK if sent was success
     *              - ESP_ERR_INVALID_ARG if parameter error
     */
    esp_err_t zh_espnow_send(const uint8_t *target, const uint8_t *data, const uint8_t data_len);

#ifdef __cplusplus
}
#endif