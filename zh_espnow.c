#include "zh_espnow.h"

static const char *TAG = "zh_espnow";

#define ZH_LOGI(msg, ...) ESP_LOGI(TAG, msg, ##__VA_ARGS__)
#define ZH_LOGE(msg, err, ...) ESP_LOGE(TAG, "[%s:%d:%s] " msg, __FILE__, __LINE__, esp_err_to_name(err), ##__VA_ARGS__)

#define ZH_ERROR_CHECK(cond, err, cleanup, msg, ...) \
    if (!(cond))                                     \
    {                                                \
        ZH_LOGE(msg, err, ##__VA_ARGS__);            \
        cleanup;                                     \
        return err;                                  \
    }

#define ZH_ERROR_CHECK_VOID(cond, cleanup, msg, ...) \
    if (!(cond))                                     \
    {                                                \
        ZH_LOGE(msg, ESP_FAIL, ##__VA_ARGS__);       \
        cleanup;                                     \
        return;                                      \
    }

#define ZH_ERROR_CHECK_CONT(cond, cleanup, msg, ...) \
    if (!(cond))                                     \
    {                                                \
        ZH_LOGE(msg, ESP_FAIL, ##__VA_ARGS__);       \
        cleanup;                                     \
        continue;                                    \
    }

#define DATA_SEND_SUCCESS BIT0   /*!< Event group bit set when ESP-NOW send operation succeeds */
#define DATA_SEND_FAIL BIT1      /*!< Event group bit set when ESP-NOW send operation fails */
#define WAIT_CONFIRM_MAX_TIME 50 /*!< Maximum time (in ticks) to wait for send confirmation from the ESP-NOW driver */

/**
 * @brief Internal queue item structure.
 *
 * This union/structure is used to pass both send requests and received messages
 * through the FreeRTOS queue.
 */
typedef struct
{
    enum
    {
        ON_RECV, /*!< Item is a received message to be processed */
        TO_SEND, /*!< Item is a send request */
    } id;
    struct
    {
        uint16_t payload_len;               /*!< Length of the payload */
        uint8_t mac_addr[ESP_NOW_ETH_ALEN]; /*!< MAC address (source for receive, destination for send) */
        uint8_t *payload;                   /*!< Pointer to the payload data (heap-allocated) */
    } data;
} _queue_t;

TaskHandle_t zh_espnow = NULL;                                                                /*!< Handle of the internal ESP-NOW processing task */
static EventGroupHandle_t _event_group_handle = NULL;                                         /*!< FreeRTOS event group for synchronization between callbacks and processing task */
static QueueHandle_t _queue_handle = NULL;                                                    /*!< FreeRTOS queue for passing send requests and received messages between callbacks and processing task */
static zh_espnow_init_config_t _init_config = {0};                                            /*!< Stored initialization configuration copied from the user-provided config */
static zh_espnow_stats_t _stats = {0};                                                        /*!< Runtime statistics counters for sent/received messages and errors */
volatile static bool _is_initialized = false;                                                 /*!< Flag indicating whether the module has been successfully initialized */
static const uint8_t _broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; /*!< Predefined broadcast MAC address used when target is NULL */
#if defined ESP_NOW_MAX_DATA_LEN_V2
volatile static uint16_t _max_message_size = ESP_NOW_MAX_DATA_LEN_V2; /*!< Maximum message size for ESP-IDF >= 5.x with V2 support (1490 bytes) */
#else
volatile static uint16_t _max_message_size = ESP_NOW_MAX_DATA_LEN; /*!< Maximum message size for older ESP-IDF versions (250 bytes) */
#endif

/**
 * @brief Validate the initialization configuration parameters.
 *
 * Checks that all configuration values are within acceptable ranges:
 * WiFi channel (1-14), task priority (>= 1), stack size (>= minimal),
 * queue size (>= 1), and retry attempts (>= 1).
 *
 * @param config Pointer to the configuration structure to validate
 *
 * @return ESP_OK if all parameters are valid
 * @return ESP_ERR_INVALID_ARG if any parameter is out of range
 */
static esp_err_t _zh_espnow_validate_config(const zh_espnow_init_config_t *config);

/**
 * @brief Initialize WiFi and ESP-NOW driver.
 *
 * Sets the WiFi channel, configures supported protocols (B/G/N, and LR on non-ESP32-C2 targets),
 * and initializes the ESP-NOW driver.
 *
 * @param config Pointer to the initialization configuration containing WiFi settings
 *
 * @return ESP_OK on success
 * @return ESP_FAIL if any WiFi or ESP-NOW initialization step fails
 */
static esp_err_t _zh_espnow_wifi_init(const zh_espnow_init_config_t *config);

/**
 * @brief Create internal FreeRTOS resources (event group and queue).
 *
 * Allocates the event group used for send confirmation synchronization
 * and the queue used for passing messages between callbacks and the processing task.
 *
 * @param config Pointer to the initialization configuration containing queue size
 *
 * @return ESP_OK on success
 * @return ESP_FAIL if memory allocation for event group or queue fails
 */
static esp_err_t _zh_espnow_resources_init(const zh_espnow_init_config_t *config);

/**
 * @brief Create the internal processing task.
 *
 * Creates a FreeRTOS task that handles all ESP-NOW send and receive processing.
 * The task is not pinned to any core (tskNO_AFFINITY).
 *
 * @param config Pointer to the initialization configuration containing task parameters
 *
 * @return ESP_OK on success
 * @return ESP_FAIL if task creation fails
 */
static esp_err_t _zh_espnow_task_init(const zh_espnow_init_config_t *config);

/**
 * @brief Register ESP-NOW send and receive callbacks.
 *
 * Always registers the send callback. The receive callback is registered
 * only when battery_mode is false (i.e., the node is not in battery-saving mode).
 *
 * @param battery_mode if true, skip receive callback registration to save power
 *
 * @return ESP_OK on success
 * @return ESP_FAIL if callback registration fails
 */
static esp_err_t _zh_espnow_callbacks_register(bool battery_mode);

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
/**
 * @brief ESP-NOW send callback (IRAM).
 *
 * Called by the ESP-NOW driver upon completion of a send operation.
 * Sets the corresponding bit in the event group to notify the processing task
 * whether the send succeeded or failed.
 *
 * @param esp_now_info Pointer to send info
 * @param status Send status (ESP_NOW_SEND_SUCCESS or ESP_NOW_SEND_FAIL)
 */
static void _zh_espnow_send_cb(const esp_now_send_info_t *esp_now_info, esp_now_send_status_t status);
#else
/**
 * @brief ESP-NOW send callback (IRAM).
 *
 * Called by the ESP-NOW driver upon completion of a send operation.
 * Sets the corresponding bit in the event group to notify the processing task
 * whether the send succeeded or failed.
 *
 * @param mac_addr Pointer to send info
 * @param status Send status (ESP_NOW_SEND_SUCCESS or ESP_NOW_SEND_FAIL)
 */
static void _zh_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);
#endif
static void _zh_espnow_recv_cb(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len);

/**
 * @brief Process an outgoing ESP-NOW message from the queue.
 *
 * Adds the target peer, attempts to send the payload with retries, waits for
 * confirmation via the event group, and posts a send completion event to the
 * ESP event loop. Frees all allocated memory upon completion.
 *
 * @param queue Pointer to the queue item containing the send request
 */
static void _zh_espnow_process_send(_queue_t *queue);

/**
 * @brief ESP-NOW receive callback.
 *
 * Called by the ESP-NOW driver upon receiving a message. Copies the received data
 * into a heap-allocated buffer and sends it through the internal queue for
 * asynchronous processing by the task. Drops the message if the queue is almost full
 * or if memory allocation fails.
 *
 * @param esp_now_info Pointer to receive info containing source address and metadata
 * @param data Pointer to the received payload data
 * @param data_len Length of the received payload in bytes
 */
static void _zh_espnow_process_recv(_queue_t *queue);

/**
 * @brief Internal ESP-NOW processing task entry point.
 *
 * Main loop that receives items from the internal queue and dispatches them
 * to the appropriate handler (_zh_espnow_process_send or _zh_espnow_process_recv).
 * Tracks minimum stack water mark for monitoring.
 *
 * @param pvParameter Unused parameter (task standard signature)
 */
static void _zh_espnow_processing(void *pvParameter);

ESP_EVENT_DEFINE_BASE(ZH_ESPNOW);

esp_err_t zh_espnow_init(const zh_espnow_init_config_t *config)
{
    ZH_LOGI("ESP-NOW initialization started.");
    ZH_ERROR_CHECK(config != NULL, ESP_ERR_INVALID_ARG, NULL, "ESP-NOW initialization failed. Invalid argument.");
    ZH_ERROR_CHECK(_is_initialized == false, ESP_ERR_INVALID_STATE, NULL, "ESP-NOW initialization failed. ESP-NOW is already initialized.");
    ZH_ERROR_CHECK(_zh_espnow_validate_config(config) == ESP_OK, ESP_FAIL, NULL, "ESP-NOW initialization failed. Initial configuration check failed.");
    ZH_ERROR_CHECK(_zh_espnow_wifi_init(config) == ESP_OK, ESP_FAIL, NULL, "ESP-NOW initialization failed. WiFi initialization failed.");
    ZH_ERROR_CHECK(_zh_espnow_resources_init(config) == ESP_OK, ESP_FAIL,
                   {ZH_ERROR_CHECK(esp_now_deinit() == ESP_OK, ESP_FAIL, NULL, "ESP-NOW driver remove failed.")}, "ESP-NOW initialization failed. Resources initialization failed.");
    ZH_ERROR_CHECK(_zh_espnow_task_init(config) == ESP_OK, ESP_FAIL,
                   {ZH_ERROR_CHECK(esp_now_deinit() == ESP_OK, ESP_FAIL, NULL, "ESP-NOW driver remove failed.")};
                   vEventGroupDelete(_event_group_handle); vQueueDelete(_queue_handle), "ESP-NOW initialization failed. Processing task initialization failed.");
    _init_config = *config;
    ZH_ERROR_CHECK(_zh_espnow_callbacks_register(config->battery_mode) == ESP_OK, ESP_FAIL,
                   {ZH_ERROR_CHECK(esp_now_deinit() == ESP_OK, ESP_FAIL, NULL, "ESP-NOW driver remove failed.")};
                   vEventGroupDelete(_event_group_handle); vQueueDelete(_queue_handle); vTaskDelete(zh_espnow), "ESP-NOW initialization failed. ESP-NOW callbacks registration failed.");
    _stats.min_stack_size = config->stack_size;
    _is_initialized = true;
    ZH_LOGI("ESP-NOW initialization completed successfully.");
    return ESP_OK;
}

esp_err_t zh_espnow_deinit(void)
{
    ZH_LOGI("ESP-NOW deinitialization started.");
    ZH_ERROR_CHECK(_is_initialized == true, ESP_ERR_NOT_FOUND, NULL, "ESP-NOW deinitialization failed. ESP-NOW not initialized.");
    ZH_ERROR_CHECK(esp_now_unregister_send_cb() == ESP_OK, ESP_FAIL, NULL, "ESP-NOW deinitialization failed. ESP-NOW callbacks unregistration failed.");
    if (_init_config.battery_mode == false)
    {
        ZH_ERROR_CHECK(esp_now_unregister_recv_cb() == ESP_OK, ESP_FAIL, NULL, "ESP-NOW deinitialization failed. ESP-NOW callbacks unregistration failed.");
    }
    ZH_ERROR_CHECK(esp_now_deinit() == ESP_OK, ESP_FAIL, NULL, "ESP-NOW deinitialization failed. ESP-NOW driver remove failed.");
    vEventGroupDelete(_event_group_handle);
    vQueueDelete(_queue_handle);
    vTaskDelete(zh_espnow);
    _is_initialized = false;
    ZH_LOGI("ESP-NOW deinitialization completed successfully.");
    return ESP_OK;
}

esp_err_t zh_espnow_send(const uint8_t *target, const uint8_t *data, const uint16_t data_len)
{
    ZH_LOGI("Adding to queue outgoing ESP-NOW data started.");
    ZH_ERROR_CHECK(_is_initialized == true, ESP_ERR_NOT_FOUND, NULL, "Adding to queue outgoing ESP-NOW data failed. ESP-NOW is not initialized.");
    ZH_ERROR_CHECK(data != NULL && data_len > 0 && data_len <= _max_message_size, ESP_ERR_INVALID_ARG, NULL, "Adding to queue outgoing ESP-NOW data failed. Invalid argument.");
    ZH_ERROR_CHECK(uxQueueSpacesAvailable(_queue_handle) > _init_config.queue_size / 10, ESP_ERR_INVALID_STATE, ++_stats.queue_overflow_error, "Adding to queue outgoing ESP-NOW data failed. Queue is almost full.");
    _queue_t queue = {0};
    queue.id = TO_SEND;
    memcpy(queue.data.mac_addr, (target == NULL) ? _broadcast_mac : target, ESP_NOW_ETH_ALEN);
    queue.data.payload = heap_caps_calloc(1, data_len, MALLOC_CAP_8BIT);
    ZH_ERROR_CHECK(queue.data.payload != NULL, ESP_ERR_NO_MEM, NULL, "Adding to queue outgoing ESP-NOW data failed. Memory allocation fail or no free memory in the heap.");
    memcpy(queue.data.payload, data, data_len);
    queue.data.payload_len = data_len;
    ZH_ERROR_CHECK(xQueueSend(_queue_handle, &queue, 1000 / portTICK_PERIOD_MS) == pdTRUE, ESP_FAIL, ++_stats.queue_overflow_error; heap_caps_free(queue.data.payload); queue.data.payload = NULL, "Adding to queue outgoing ESP-NOW data failed. Failed to add data to queue.");
    ZH_LOGI("Adding to queue outgoing ESP-NOW data completed successfully.");
    return ESP_OK;
}

uint8_t zh_espnow_get_version(void)
{
    ZH_LOGI("ESP-NOW version receipt started.");
    uint32_t version = 0;
    ZH_ERROR_CHECK(esp_now_get_version(&version) == ESP_OK, 0, NULL, "ESP-NOW version receiption failed.");
    ZH_LOGI("ESP-NOW version receiption successfully.");
    return (uint8_t)version;
}

const zh_espnow_stats_t *zh_espnow_get_stats(void)
{
    return &_stats;
}

void zh_espnow_reset_stats(void)
{
    ZH_LOGI("Error statistic reset started.");
    _stats.sent_success = 0;
    _stats.sent_fail = 0;
    _stats.received = 0;
    _stats.espnow_driver_error = 0;
    _stats.event_post_error = 0;
    _stats.queue_overflow_error = 0;
    _stats.min_stack_size = 0;
    ZH_LOGI("ESP-NOW statistic reset successfully.");
}

esp_err_t zh_espnow_get_mac(uint8_t *mac_addr)
{
    return esp_wifi_get_mac(_init_config.wifi_interface, mac_addr);
}

static esp_err_t _zh_espnow_validate_config(const zh_espnow_init_config_t *config)
{
    ZH_ERROR_CHECK(config->wifi_channel > 0 && config->wifi_channel < 15, ESP_ERR_INVALID_ARG, NULL, "Invalid WiFi channel.");
    ZH_ERROR_CHECK(config->task_priority >= 1 && config->stack_size >= configMINIMAL_STACK_SIZE, ESP_ERR_INVALID_ARG, NULL, "Invalid task settings.");
    ZH_ERROR_CHECK(config->queue_size >= 1, ESP_ERR_INVALID_ARG, NULL, "Invalid queue size.");
    ZH_ERROR_CHECK(config->attempts > 0, ESP_ERR_INVALID_ARG, NULL, "Invalid number of attempts.");
    return ESP_OK;
}

static esp_err_t _zh_espnow_wifi_init(const zh_espnow_init_config_t *config)
{
    ZH_ERROR_CHECK(esp_wifi_set_channel(config->wifi_channel, WIFI_SECOND_CHAN_NONE) == ESP_OK, ESP_FAIL, NULL, "WiFi channel setup failed.");
#if defined CONFIG_IDF_TARGET_ESP32C2
    ZH_ERROR_CHECK(esp_wifi_set_protocol(config->wifi_interface, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N) == ESP_OK, ESP_FAIL, NULL, "WiFi protocol setup failed.");
#else
    ZH_ERROR_CHECK(esp_wifi_set_protocol(config->wifi_interface, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR) == ESP_OK, ESP_FAIL, NULL, "WiFi protocol setup failed.");
#endif
    ZH_ERROR_CHECK(esp_now_init() == ESP_OK, ESP_FAIL, NULL, "ESP-NOW driver initialization failed.");
    return ESP_OK;
}

static esp_err_t _zh_espnow_resources_init(const zh_espnow_init_config_t *config)
{
    _event_group_handle = xEventGroupCreate();
    ZH_ERROR_CHECK(_event_group_handle != NULL, ESP_FAIL, NULL, "Event group creation failed.");
    _queue_handle = xQueueCreate(config->queue_size, sizeof(_queue_t));
    ZH_ERROR_CHECK(_queue_handle != NULL, ESP_FAIL, NULL, "Queue creation failed.");
    return ESP_OK;
}

static esp_err_t _zh_espnow_task_init(const zh_espnow_init_config_t *config)
{
    ZH_ERROR_CHECK(xTaskCreatePinnedToCore(&_zh_espnow_processing, "zh_espnow_processing", config->stack_size, NULL, config->task_priority, &zh_espnow, tskNO_AFFINITY) == pdPASS, ESP_FAIL, NULL, "Task creation failed.");
    return ESP_OK;
}

static esp_err_t _zh_espnow_callbacks_register(bool battery_mode)
{
    ZH_ERROR_CHECK(esp_now_register_send_cb(_zh_espnow_send_cb) == ESP_OK, ESP_FAIL, NULL, "Send callback registration failed.");
    if (battery_mode == false)
    {
        ZH_ERROR_CHECK(esp_now_register_recv_cb(_zh_espnow_recv_cb) == ESP_OK, ESP_FAIL, NULL, "Receive callback registration failed.");
    }
    return ESP_OK;
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
static void IRAM_ATTR _zh_espnow_send_cb(const esp_now_send_info_t *esp_now_info, esp_now_send_status_t status)
{
    ZH_ERROR_CHECK_VOID(esp_now_info != NULL, NULL, "Send callback received NULL MAC address.");
#else
static void IRAM_ATTR _zh_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    ZH_ERROR_CHECK_VOID(mac_addr != NULL, NULL, "Send callback received NULL MAC address.");
#endif
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xEventGroupSetBitsFromISR(_event_group_handle, (status == ESP_NOW_SEND_SUCCESS) ? DATA_SEND_SUCCESS : DATA_SEND_FAIL, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken == pdTRUE)
    {
        portYIELD_FROM_ISR();
    };
}

static void IRAM_ATTR _zh_espnow_recv_cb(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len)
{
    ZH_ERROR_CHECK_VOID(esp_now_info != NULL && data != NULL && data_len > 0, NULL, "Receive callback received invalid arguments.");
    ZH_ERROR_CHECK_VOID(uxQueueSpacesAvailable(_queue_handle) > _init_config.queue_size / 10, ++_stats.queue_overflow_error, "Queue is almost full. Dropping incoming ESP-NOW data.");
    _queue_t queue = {0};
    queue.id = ON_RECV;
    memcpy(queue.data.mac_addr, esp_now_info->src_addr, ESP_NOW_ETH_ALEN);
    queue.data.payload = heap_caps_calloc(1, data_len, MALLOC_CAP_8BIT);
    ZH_ERROR_CHECK_VOID(queue.data.payload != NULL, NULL, "Memory allocation failed for incoming ESP-NOW data.");
    memcpy(queue.data.payload, data, data_len);
    queue.data.payload_len = data_len;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    ZH_ERROR_CHECK_VOID(xQueueSendFromISR(_queue_handle, &queue, &xHigherPriorityTaskWoken) == pdTRUE, ++_stats.queue_overflow_error;
                        heap_caps_free(queue.data.payload); queue.data.payload = NULL, "Failed to add incoming ESP-NOW data to queue.");
    if (xHigherPriorityTaskWoken == pdTRUE)
    {
        portYIELD_FROM_ISR();
    };
}

static void _zh_espnow_process_send(_queue_t *queue)
{
    esp_now_peer_info_t *peer = heap_caps_calloc(1, sizeof(esp_now_peer_info_t), MALLOC_CAP_8BIT);
    ZH_ERROR_CHECK_VOID(peer != NULL, heap_caps_free(queue->data.payload); queue->data.payload = NULL, "Outgoing ESP-NOW data processed failed. Failed to allocate memory.");
    peer->ifidx = _init_config.wifi_interface;
    memcpy(peer->peer_addr, queue->data.mac_addr, ESP_NOW_ETH_ALEN);
    ZH_ERROR_CHECK_VOID(esp_now_add_peer(peer) == ESP_OK, ++_stats.espnow_driver_error; heap_caps_free(queue->data.payload); queue->data.payload = NULL;
                        heap_caps_free(peer); peer = NULL, "Outgoing ESP-NOW data processed failed. Failed to add peer.");
    zh_espnow_event_on_send_t *on_send = heap_caps_calloc(1, sizeof(zh_espnow_event_on_send_t), MALLOC_CAP_8BIT);
    ZH_ERROR_CHECK_VOID(on_send != NULL, heap_caps_free(queue->data.payload); queue->data.payload = NULL; esp_now_del_peer(peer->peer_addr);
                        heap_caps_free(peer); peer = NULL, "Outgoing ESP-NOW data processed failed. Failed to allocate memory.");
    memcpy(on_send->mac_addr, queue->data.mac_addr, ESP_NOW_ETH_ALEN);
    on_send->status = ZH_ESPNOW_SEND_FAIL;
    for (uint8_t attempt = 0; attempt < _init_config.attempts; ++attempt)
    {
        ZH_ERROR_CHECK_CONT(esp_now_send(queue->data.mac_addr, queue->data.payload, queue->data.payload_len) == ESP_OK, ++_stats.espnow_driver_error, "Outgoing ESP-NOW data processed failed. ESP-NOW driver error.");
        EventBits_t bits = xEventGroupWaitBits(_event_group_handle, DATA_SEND_SUCCESS | DATA_SEND_FAIL, pdTRUE, pdFALSE, WAIT_CONFIRM_MAX_TIME / portTICK_PERIOD_MS);
        if (bits & DATA_SEND_SUCCESS)
        {
            on_send->status = ZH_ESPNOW_SEND_SUCCESS;
            ++_stats.sent_success;
            break;
        }
    }
    if (on_send->status == ZH_ESPNOW_SEND_FAIL)
    {
        ++_stats.sent_fail;
    }
    // clang-format off
    ZH_ERROR_CHECK_VOID(esp_event_post(ZH_ESPNOW, ZH_ESPNOW_ON_SEND_EVENT, on_send, sizeof(zh_espnow_event_on_send_t), 1000 / portTICK_PERIOD_MS) == ESP_OK,
                        ++_stats.event_post_error; heap_caps_free(queue->data.payload); queue->data.payload = NULL; esp_now_del_peer(peer->peer_addr);
                        heap_caps_free(peer); peer = NULL; heap_caps_free(on_send); on_send = NULL, "Outgoing ESP-NOW data processed failed. Failed to post send event.");
    // clang-format on
    heap_caps_free(queue->data.payload);
    queue->data.payload = NULL;
    esp_now_del_peer(peer->peer_addr);
    heap_caps_free(peer);
    peer = NULL;
    heap_caps_free(on_send);
    on_send = NULL;
}

static void _zh_espnow_process_recv(_queue_t *queue)
{
    zh_espnow_event_on_recv_t *recv_data = heap_caps_calloc(1, (sizeof(zh_espnow_event_on_recv_t) + queue->data.payload_len), MALLOC_CAP_8BIT);
    ZH_ERROR_CHECK_VOID(recv_data != NULL, heap_caps_free(queue->data.payload); queue->data.payload = NULL, "Incoming ESP-NOW data processing failed. Memory allocation failed.");
    memcpy(recv_data->mac_addr, queue->data.mac_addr, ESP_NOW_ETH_ALEN);
    recv_data->data_len = queue->data.payload_len;
    memcpy(recv_data->data, queue->data.payload, queue->data.payload_len);
    ++_stats.received;
    // clang-format off
    ZH_ERROR_CHECK_VOID(esp_event_post(ZH_ESPNOW, ZH_ESPNOW_ON_RECV_EVENT, recv_data, (sizeof(zh_espnow_event_on_recv_t) + queue->data.payload_len), 1000 / portTICK_PERIOD_MS) == ESP_OK,
                        ++_stats.event_post_error; heap_caps_free(recv_data); recv_data = NULL; heap_caps_free(queue->data.payload); queue->data.payload = NULL, "Incoming ESP-NOW data processing failed. Failed to post event.");
    // clang-format on
    heap_caps_free(recv_data);
    recv_data = NULL;
    heap_caps_free(queue->data.payload);
    queue->data.payload = NULL;
}

static void _zh_espnow_processing(void *pvParameter)
{
    (void)pvParameter;
    _queue_t queue = {0};
    while (xQueueReceive(_queue_handle, &queue, portMAX_DELAY) == pdTRUE)
    {
        switch (queue.id)
        {
        case TO_SEND:
            _zh_espnow_process_send(&queue);
            break;
        case ON_RECV:
            _zh_espnow_process_recv(&queue);
            break;
        default:
            break;
        }
        _stats.min_stack_size = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    }
    vTaskDelete(NULL);
}