#pragma once

#include "string.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#define ZH_ESPNOW_INIT_CONFIG_DEFAULT() \
    {                                   \
        .task_priority = 10,            \
        .stack_size = 3072,             \
        .queue_size = 64,               \
        .wifi_interface = WIFI_IF_STA,  \
        .wifi_channel = 1,              \
        .attempts = 3,                  \
        .battery_mode = false}

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct // Structure for initial initialization of ESP-NOW interface.
    {
        uint8_t task_priority;           // Task priority for the ESP-NOW messages processing. @note The minimum size is 5.
        uint16_t stack_size;             // Stack size for task for the ESP-NOW messages processing. @note The minimum size is 2048.
        uint8_t queue_size;              // Queue size for task for the ESP-NOW messages processing. @note The size depends on the number of messages to be processed. The minimum size is 16.
        wifi_interface_t wifi_interface; // WiFi interface (STA or AP) used for ESP-NOW operation. @note The MAC address of the device depends on the selected WiFi interface.
        uint8_t wifi_channel;            // Wi-Fi channel uses to send/receive ESP-NOW data. @note Values from 1 to 14.
        uint8_t attempts;                // Maximum number of attempts to send a message. @note It is not recommended to set a value greater than 10.
        bool battery_mode;               // Battery operation mode. If true, the node does not receive messages.
    } zh_espnow_init_config_t;

    ESP_EVENT_DECLARE_BASE(ZH_ESPNOW);

    typedef enum // Enumeration of possible ESP-NOW events.
    {
        ZH_ESPNOW_ON_RECV_EVENT, // The event when the ESP-NOW message was received.
        ZH_ESPNOW_ON_SEND_EVENT  // The event when the ESP-NOW message was sent.
    } zh_espnow_event_type_t;

    typedef enum // Enumeration of possible status of sent ESP-NOW message.
    {
        ZH_ESPNOW_SEND_SUCCESS, // If ESP-NOW message was sent success.
        ZH_ESPNOW_SEND_FAIL     // If ESP-NOW message was sent fail.
    } zh_espnow_on_send_event_type_t;

    typedef struct // Structure for sending data to the event handler when an ESP-NOW message was sent. @note Should be used with ZH_ESPNOW event base and ZH_ESPNOW_ON_SEND_EVENT event.
    {
        uint8_t mac_addr[ESP_NOW_ETH_ALEN];    // MAC address of the device to which the ESP-NOW message was sent.
        zh_espnow_on_send_event_type_t status; // Status of sent ESP-NOW message.
    } zh_espnow_event_on_send_t;

    typedef struct // Structure for sending data to the event handler when an ESP-NOW message was received. @note Should be used with ZH_ESPNOW event base and ZH_ESPNOW_ON_RECV_EVENT event.
    {
        uint8_t mac_addr[ESP_NOW_ETH_ALEN]; // MAC address of the sender ESP-NOW message.
        uint8_t *data;                      // Pointer to the data of the received ESP-NOW message.
        uint16_t data_len;                  // Size of the received ESP-NOW message.
    } zh_espnow_event_on_recv_t;

    typedef struct // Structure for message statistics storage.
    {
        uint32_t sent_success; // Number of successfully sent messages.
        uint32_t sent_fail;    // Number of failed sent messages.
        uint32_t received;     // Number of received messages.
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
     * @brief Check ESP-NOW initialization status.
     *
     * @return True if ESP-NOW is initialized false otherwise.
     */
    bool zh_espnow_is_initialized(void);

    /**
     * @brief Get number of attempts.
     *
     * @return Attemps number.
     */
    uint8_t zh_espnow_get_attempts(void);

    /**
     * @brief Set number of attempts.
     *
     * @param[in] attempts Attemps number.
     *
     * @return ESP_OK if success or an error code otherwise.
     */
    esp_err_t zh_espnow_set_attempts(uint8_t attempts);

    /**
     * @brief Get ESP-NOW channel.
     *
     * @return ESP-NOW channel if success or 0 otherwise.
     */
    uint8_t zh_espnow_get_channel(void);

    /**
     * @brief Set ESP-NOW channel.
     *
     * @param[in] channel ESP-NOW channel (1-14).
     *
     * @return ESP_OK if success or an error code otherwise.
     */
    esp_err_t zh_espnow_set_channel(uint8_t channel);

#ifdef __cplusplus
}
#endif