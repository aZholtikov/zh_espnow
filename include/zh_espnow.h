/**
 * @file zh_espnow.h
 */

#pragma once

#include "string.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

/**
 * @brief ESP-NOW interface initial default values.
 */
#define ZH_ESPNOW_INIT_CONFIG_DEFAULT()         \
    {                                           \
        .task_priority = 1,                     \
        .stack_size = configMINIMAL_STACK_SIZE, \
        .queue_size = 1,                        \
        .wifi_interface = WIFI_IF_STA,          \
        .wifi_channel = 1,                      \
        .attempts = 1,                          \
        .battery_mode = false}

#ifdef __cplusplus
extern "C"
{
#endif

    extern TaskHandle_t zh_espnow; /*!< ESP-NOW interface Task Handle. */

    /**
     * @brief Structure for initial initialization of ESP-NOW interface.
     */
    typedef struct
    {
        uint8_t task_priority;           /*!< Task priority for the ESP-NOW messages processing. @note Recommended value is 5. */
        uint16_t stack_size;             /*!< Stack size for task for the ESP-NOW messages processing. @note Recommended value is 2048. */
        uint8_t queue_size;              /*!< Queue size for task for the ESP-NOW messages processing. @note Recommended value is 10. */
        wifi_interface_t wifi_interface; /*!< WiFi interface (STA or AP) used for ESP-NOW operation. */
        uint8_t wifi_channel;            /*!< Wi-Fi channel uses to send/receive ESP-NOW data. */
        uint8_t attempts;                /*!< Maximum number of attempts to send a message. @note It is not recommended to set a value greater than 10. */
        bool battery_mode;               /*!< Battery operation mode. If true the node does not receive messages. */
    } zh_espnow_init_config_t;

    ESP_EVENT_DECLARE_BASE(ZH_ESPNOW);

    /**
     * @brief Enumeration of possible ESP-NOW events.
     */
    typedef enum
    {
        ZH_ESPNOW_ON_RECV_EVENT, /*!< The event when the ESP-NOW message was received. */
        ZH_ESPNOW_ON_SEND_EVENT  /*!< The event when the ESP-NOW message was sent. */
    } zh_espnow_event_type_t;

    /**
     * @brief Enumeration of possible status of sent ESP-NOW message.
     */
    typedef enum
    {
        ZH_ESPNOW_SEND_SUCCESS, /*!< If ESP-NOW message was sent success. */
        ZH_ESPNOW_SEND_FAIL     /*!< If ESP-NOW message was sent fail. */
    } zh_espnow_on_send_event_type_t;

    /**
     * @brief Structure for sending data to the event handler when an ESP-NOW message was sent.
     *
     * @note Should be used with ZH_ESPNOW event base and ZH_ESPNOW_ON_SEND_EVENT event.
     */
    typedef struct
    {
        uint8_t mac_addr[ESP_NOW_ETH_ALEN];    /*!< MAC address of the device to which the ESP-NOW message was sent. */
        zh_espnow_on_send_event_type_t status; /*!< Status of sent ESP-NOW message. */
    } zh_espnow_event_on_send_t;

    /**
     * @brief Structure for sending data to the event handler when an ESP-NOW message was received.
     *
     * @note Should be used with ZH_ESPNOW event base and ZH_ESPNOW_ON_RECV_EVENT event.
     */
    typedef struct
    {
        uint8_t mac_addr[ESP_NOW_ETH_ALEN]; /*!< MAC address of the sender ESP-NOW message. */
        uint8_t *data;                      /*!< Pointer to the data of the received ESP-NOW message. */
        uint16_t data_len;                  /*!< Size of the received ESP-NOW message. */
    } zh_espnow_event_on_recv_t;

    /**
     * @brief Structure for message statistics storage.
     */
    typedef struct
    {
        uint32_t sent_success; /*!< Number of successfully sent messages. */
        uint32_t sent_fail;    /*!< Number of failed sent messages. */
        uint32_t received;     /*!< Number of received messages. */
    } zh_espnow_stats_t;

    /**
     * @brief Initialize ESP-NOW interface.
     *
     * @note Before initialize ESP-NOW interface recommend initialize zh_espnow_init_config_t structure with default values.
     *
     * @code zh_espnow_init_config_t config = ZH_ESPNOW_INIT_CONFIG_DEFAULT() @endcode
     *
     * @param[in] config Pointer to ESP-NOW initialized configuration structure. Can point to a temporary variable.
     *
     * @return ESP_OK if success or an error code otherwise.
     */
    esp_err_t zh_espnow_init(const zh_espnow_init_config_t *config);

    /**
     * @brief Deinitialize ESP-NOW interface.
     *
     * @return ESP_OK if success or an error code otherwise.
     */
    esp_err_t zh_espnow_deinit(void);

    /**
     * @brief Send ESP-NOW data.
     *
     * @param[in] target Pointer to a buffer containing an eight-byte target MAC. Can be NULL for broadcast.
     * @param[in] data Pointer to a buffer containing the data for send.
     * @param[in] data_len Sending data length.
     *
     * @note The function will return an error if less than 10% of the size set at initialization remains in the message queue.
     *
     * @return ESP_OK if success or an error code otherwise.
     */
    esp_err_t zh_espnow_send(const uint8_t *target, const uint8_t *data, const uint16_t data_len);

    /**
     * @brief Get ESP-NOW version.
     *
     * @return ESP-NOW version if success or 0 otherwise.
     */
    uint8_t zh_espnow_get_version(void);

    /**
     * @brief Get ESP-NOW statistics.
     *
     * @return Pointer to the statistics structure.
     */
    const zh_espnow_stats_t *zh_espnow_get_stats(void);

    /**
     * @brief Reset ESP-NOW statistics.
     */
    void zh_espnow_reset_stats(void);

    /**
     * @brief Get MAC address of the node.
     *
     * @param[out] mac_addr Pointer to a buffer containing an eight-byte MAC.
     *
     * @return ESP_OK if success or an error code otherwise.
     */
    esp_err_t zh_espnow_get_mac(uint8_t *mac_addr);

#ifdef __cplusplus
}
#endif