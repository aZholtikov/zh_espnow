/**
 * @file zh_espnow.h
 *
 * @brief Thread-safe ESP-NOW communication interface for ESP-IDF.
 *
 * This module provides a high-level API for ESP-NOW with asynchronous message processing.
 * It includes a dedicated task that handles sending and receiving ESP-NOW messages,
 * using a FreeRTOS queue to decouple the ISRs/callbacks from the application logic.
 * Received messages are posted as ESP events (using the ESP event loop library),
 * and send confirmations are also posted as events.
 *
 * The module supports:
 * - Configurable Wi-Fi channel, interface (STA/AP), and message retry attempts.
 * - Battery mode: when enabled, the node does not receive messages (receive callback is not registered).
 * - Statistics tracking for sent/received messages, errors, and stack usage.
 * - Broadcasting and unicast transmission.
 *
 * @note The module internally creates a FreeRTOS task and a queue. The queue size,
 *       task stack size, and priority are configurable via zh_espnow_init_config_t.
 * @note All public functions are thread-safe with respect to the module's internal state,
 *       except for zh_espnow_deinit() which must not be called concurrently with any other
 *       operation (the caller must ensure all other accesses have completed).
 * @warning Do not call zh_espnow_init() twice without an intervening zh_espnow_deinit().
 * @warning Memory allocated for message payloads is freed internally; the user does not need
 *          to manage it. However, the user must provide valid data buffers during zh_espnow_send()
 *          and the data is copied into internal buffers.
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
 * @brief Default initialization configuration for ESP-NOW interface.
 *
 * This macro provides a convenient way to initialise a `zh_espnow_init_config_t` structure with safe default values.
 *
 * @code
 * zh_espnow_init_config_t config = ZH_ESPNOW_INIT_CONFIG_DEFAULT();
 * @endcode
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

    extern TaskHandle_t zh_espnow; /*!< Handle of the internal ESP-NOW processing task. */

    /**
     * @brief Initial configuration structure for the ESP-NOW interface.
     *
     * This structure holds all parameters required to initialise the module.
     * It must be filled by the user (or via the default macro) and passed to zh_espnow_init().
     */
    typedef struct
    {
        uint16_t stack_size;             /*!< Stack size (in bytes) for the internal processing task. @note Recommended value is 2048. */
        uint8_t task_priority;           /*!< Priority of the processing task. @note Recommended value is 5. */
        uint8_t queue_size;              /*!< Size of the internal FreeRTOS queue (number of items). @note Recommended value is 10. */
        uint8_t wifi_channel;            /*!< Wi-Fi channel used for ESP-NOW communication (1-13). */
        uint8_t attempts;                /*!< Maximum number of retry attempts for sending a message. @note It is not recommended to set a value greater than 10. */
        bool battery_mode;               /*!< If true, the node does not register a receive callback (receive is disabled). */
        wifi_interface_t wifi_interface; /*!< Wi-Fi interface (STA or AP) to use for ESP-NOW. */
    } zh_espnow_init_config_t;

    ESP_EVENT_DECLARE_BASE(ZH_ESPNOW);

    /**
     * @brief Enumeration of possible ESP-NOW events posted to the event loop.
     */
    typedef enum
    {
        ZH_ESPNOW_ON_RECV_EVENT, /*!< A message has been received. The event data is a `zh_espnow_event_on_recv_t` structure. */
        ZH_ESPNOW_ON_SEND_EVENT  /*!< A transmission attempt has completed (success or failure). The event data is a `zh_espnow_event_on_send_t` structure. */
    } zh_espnow_event_type_t;

    /**
     * @brief Status of a sent message, reported in the send event.
     */
    typedef enum
    {
        ZH_ESPNOW_SEND_SUCCESS, /*!< The message was successfully sent and acknowledged. */
        ZH_ESPNOW_SEND_FAIL     /*!< The message could not be sent (all retry attempts failed). */
    } zh_espnow_on_send_event_type_t;

    /**
     * @brief Event data structure for a send completion event.
     *
     * This structure is posted to the event loop when a message transmission finishes (either successfully or after exhausting retries).
     *
     * @note This event is posted only when zh_espnow_init() is called with `battery_mode = false`.
     */
    typedef struct
    {
        uint8_t mac_addr[ESP_NOW_ETH_ALEN];    /*!< MAC address of the target device. */
        zh_espnow_on_send_event_type_t status; /*!< Status of the send operation. */
    } zh_espnow_event_on_send_t;

    /**
     * @brief Event data structure for a received message event.
     *
     * This structure is posted to the event loop when an ESP-NOW message is received.
     * The payload is stored in the flexible array member `data`.
     *
     * @note This event is posted only when zh_espnow_init() is called with `battery_mode = false`.
     */
    typedef struct
    {
        uint16_t data_len;                  /*!< Length of the received payload in bytes. */
        uint8_t mac_addr[ESP_NOW_ETH_ALEN]; /*!< MAC address of the sender. */
        uint8_t data[];                     /*!< Flexible array member holding the received payload. */
    } zh_espnow_event_on_recv_t;

    /**
     * @brief Statistics structure for the ESP-NOW interface.
     *
     * Contains counters for various events and errors, as well as the minimum free stack size of the processing task.
     */
    typedef struct
    {
        uint32_t sent_success;         /*!< Number of successfully sent messages. */
        uint32_t sent_fail;            /*!< Number of failed sent messages. */
        uint32_t received;             /*!< Number of received messages (only if receive is enabled). */
        uint32_t espnow_driver_error;  /*!< Number of errors returned by the ESP-NOW driver. */
        uint32_t event_post_error;     /*!< Number of failures when posting events to the event loop. */
        uint32_t queue_overflow_error; /*!< Number of times the internal queue overflowed (dropped messages). */
        uint32_t min_stack_size;       /*!< Minimum free stack size (in bytes) of the processing task. */
    } zh_espnow_stats_t;

    /**
     * @brief Initialise the ESP-NOW interface.
     *
     * This function sets up Wi-Fi, initialises the ESP-NOW driver, creates the internal FreeRTOS queue and task, and registers the necessary callbacks.
     * After a successful initialization, the module is ready to send and receive messages.
     *
     * @note The configuration structure `config` may be a temporary variable; its contents are copied internally.
     * @warning This function must not be called twice without an intervening zh_espnow_deinit(). Doing so will return `ESP_ERR_INVALID_STATE`.
     * @warning The caller must ensure that Wi-Fi is already initialised and started before calling this function.
     *
     * @param[in] config Pointer to the initialization configuration. Must not be NULL.
     *
     * @return ESP_OK on success.
     * @return ESP_ERR_INVALID_ARG if config is NULL or contains invalid values.
     * @return ESP_ERR_INVALID_STATE if the module is already initialised.
     * @return ESP_ERR_NO_MEM if memory allocation fails (queue, event group, etc.).
     * @return ESP_FAIL if any internal initialization step fails.
     */
    esp_err_t zh_espnow_init(const zh_espnow_init_config_t *config);

    /**
     * @brief Deinitialise the ESP-NOW interface.
     *
     * This function unregisters callbacks, deinitialises the ESP-NOW driver, deletes the internal queue, event group, and processing task, and resets the module state.
     *
     * @warning This function is NOT thread-safe. It must not be called concurrently with any other API function (e.g., zh_espnow_send(), event handlers, etc.).
     *          The caller must ensure that all other accesses to the module have completed before invoking zh_espnow_deinit().
     * @note Safe to call even if the module is not initialised (returns ESP_ERR_NOT_FOUND).
     *
     * @return ESP_OK on success.
     * @return ESP_ERR_NOT_FOUND if the module was not initialised.
     * @return ESP_FAIL if deinitialization fails (e.g., driver unregister failure).
     */
    esp_err_t zh_espnow_deinit(void);

    /**
     * @brief Send an ESP-NOW message to a specified target.
     *
     * This function queues the message for transmission. The actual sending is performed asynchronously by the internal processing task.
     * The function returns immediately after placing the message in the queue.
     *
     * @note The message payload is copied into an internal buffer; the caller does not need to keep the data buffer valid after the call returns.
     * @note The queue size is limited; if the queue is almost full (less than 10% of its capacity is free), the function returns ESP_ERR_INVALID_STATE to prevent overflow.
     * @note The target MAC address can be the broadcast address (all 0xFF) for sending to all peers. If `target` is NULL, the broadcast address is used.
     *
     * @param[in] target Pointer to a 6-byte MAC address. If NULL, broadcast is used.
     * @param[in] data Pointer to the payload data to be sent. Must not be NULL.
     * @param[in] data_len Length of the payload in bytes. Must be > 0 and <= the maximum ESP-NOW data length (250 or 1490 bytes).
     *
     * @return ESP_OK on success.
     * @return ESP_ERR_INVALID_ARG if data is NULL, data_len is zero or exceeds the limit.
     * @return ESP_ERR_INVALID_STATE if the module is not initialised or the queue is almost full.
     * @return ESP_ERR_NO_MEM if memory allocation for the internal copy fails.
     * @return ESP_FAIL if sending to the queue fails (e.g., timeout).
     */
    esp_err_t zh_espnow_send(const uint8_t *target, const uint8_t *data, const uint16_t data_len);

    /**
     * @brief Get the ESP-NOW version.
     *
     * @return The version number as an unsigned 8-bit integer, or 0 on failure.
     */
    uint8_t zh_espnow_get_version(void);

    /**
     * @brief Get a pointer to the current statistics structure.
     *
     * The returned pointer is valid as long as the module is initialised.
     * The statistics are updated internally in real time.
     *
     * @return Pointer to the statistics structure (const).
     */
    const zh_espnow_stats_t *zh_espnow_get_stats(void);

    /**
     * @brief Reset all statistics counters to zero.
     *
     * This function resets all fields of the internal statistics structure.
     */
    void zh_espnow_reset_stats(void);

    /**
     * @brief Retrieve the MAC address of the Wi-Fi interface used by ESP-NOW.
     *
     * @param[out] mac_addr Pointer to a 6-byte buffer where the MAC address will be stored.
     *
     * @return ESP_OK on success.
     * @return ESP_ERR_INVALID_ARG if mac_addr is NULL.
     * @return Other errors from esp_wifi_get_mac().
     */
    esp_err_t zh_espnow_get_mac(uint8_t *mac_addr);

#ifdef __cplusplus
}
#endif