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

#define DATA_SEND_SUCCESS BIT0
#define DATA_SEND_FAIL BIT1
#define WAIT_CONFIRM_MAX_TIME 50

typedef struct
{
    enum
    {
        ON_RECV,
        TO_SEND,
    } id;
    struct
    {
        uint8_t mac_addr[ESP_NOW_ETH_ALEN];
        uint8_t *payload;
        uint16_t payload_len;
    } data;
} _queue_t;

TaskHandle_t zh_espnow = NULL;
static EventGroupHandle_t _event_group_handle = NULL;
static QueueHandle_t _queue_handle = NULL;
static zh_espnow_init_config_t _init_config = {0};
static zh_espnow_stats_t _stats = {0};
volatile static bool _is_initialized = false;
static const uint8_t _broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#if defined ESP_NOW_MAX_DATA_LEN_V2
volatile static uint16_t _max_message_size = ESP_NOW_MAX_DATA_LEN_V2;
#else
volatile static uint16_t _max_message_size = ESP_NOW_MAX_DATA_LEN;
#endif

static esp_err_t _zh_espnow_validate_config(const zh_espnow_init_config_t *config);
static esp_err_t _zh_espnow_wifi_init(const zh_espnow_init_config_t *config);
static esp_err_t _zh_espnow_resources_init(const zh_espnow_init_config_t *config);
static esp_err_t _zh_espnow_task_init(const zh_espnow_init_config_t *config);
static esp_err_t _zh_espnow_callbacks_register(bool battery_mode);

#if (ESP_IDF_VERSION_MAJOR >= 5 && ESP_IDF_VERSION_MINOR >= 5) || ESP_IDF_VERSION_MAJOR >= 6
static void _zh_espnow_send_cb(const esp_now_send_info_t *esp_now_info, esp_now_send_status_t status);
#else
static void _zh_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);
#endif
static void _zh_espnow_recv_cb(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len);
static void _zh_espnow_process_send(_queue_t *queue);
static void _zh_espnow_process_recv(_queue_t *queue);
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
                   ZH_ERROR_CHECK(esp_now_deinit() == ESP_OK, ESP_FAIL, NULL, "ESP-NOW driver remove failed."), "ESP-NOW initialization failed. Resources initialization failed.");
    ZH_ERROR_CHECK(_zh_espnow_task_init(config) == ESP_OK, ESP_FAIL,
                   ZH_ERROR_CHECK(esp_now_deinit() == ESP_OK, ESP_FAIL, NULL, "ESP-NOW driver remove failed.");
                   vEventGroupDelete(_event_group_handle);
                   vQueueDelete(_queue_handle), "ESP-NOW initialization failed. Processing task initialization failed.");
    ZH_ERROR_CHECK(_zh_espnow_callbacks_register(config->battery_mode) == ESP_OK, ESP_FAIL,
                   ZH_ERROR_CHECK(esp_now_deinit() == ESP_OK, ESP_FAIL, NULL, "ESP-NOW driver remove failed.");
                   vEventGroupDelete(_event_group_handle);
                   vQueueDelete(_queue_handle);
                   vTaskDelete(zh_espnow), "ESP-NOW initialization failed. ESP-NOW callbacks registration failed.");
    _init_config = *config;
    _is_initialized = true;
    ZH_LOGI("ESP-NOW initialization completed successfully.");
    return ESP_OK;
}

esp_err_t zh_espnow_deinit(void)
{
    ZH_LOGI("ESP-NOW deinitialization started.");
    ZH_ERROR_CHECK(_is_initialized == false, ESP_ERR_NOT_FOUND, NULL, "ESP-NOW deinitialization failed. ESP-NOW not initialized.");
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
    ZH_ERROR_CHECK(uxQueueSpacesAvailable(_queue_handle) > _init_config.queue_size / 10, ESP_ERR_INVALID_STATE, NULL, "Adding to queue outgoing ESP-NOW data failed. Queue is almost full.");
    _queue_t queue = {0};
    queue.id = TO_SEND;
    memcpy(queue.data.mac_addr, (target == NULL) ? _broadcast_mac : target, ESP_NOW_ETH_ALEN);
    queue.data.payload = heap_caps_calloc(1, data_len, MALLOC_CAP_8BIT);
    ZH_ERROR_CHECK(queue.data.payload != NULL, ESP_ERR_NO_MEM, NULL, "Adding to queue outgoing ESP-NOW data failed. Memory allocation fail or no free memory in the heap.");
    memcpy(queue.data.payload, data, data_len);
    queue.data.payload_len = data_len;
    ZH_ERROR_CHECK(xQueueSend(_queue_handle, &queue, 1000 / portTICK_PERIOD_MS) == pdTRUE, ESP_FAIL, NULL, "Adding to queue outgoing ESP-NOW data failed. Failed to add data to queue.");
    ZH_LOGI("Adding to queue outgoing ESP-NOW data completed successfully.");
    return ESP_OK;
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
    ZH_ERROR_CHECK(esp_wifi_set_protocol(config->wifi_interface, WIFI_PROTOCOL_11B) == ESP_OK, ESP_FAIL, NULL, "WiFi protocol setup failed.");
#else
    ZH_ERROR_CHECK(esp_wifi_set_protocol(config->wifi_interface, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_LR) == ESP_OK, ESP_FAIL, NULL, "WiFi protocol setup failed.");
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
    ZH_ERROR_CHECK(xTaskCreatePinnedToCore(&_zh_espnow_processing, "zh_espnow_processing", config->stack_size, NULL, config->task_priority, &zh_espnow, tskNO_AFFINITY) == pdPASS,
                   ESP_FAIL, NULL, "Task creation failed.");
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

#if (ESP_IDF_VERSION_MAJOR >= 5 && ESP_IDF_VERSION_MINOR >= 5) || ESP_IDF_VERSION_MAJOR >= 6
static void IRAM_ATTR _zh_espnow_send_cb(const esp_now_send_info_t *esp_now_info, esp_now_send_status_t status)
{
    if (esp_now_info == NULL)
    {
#else
static void IRAM_ATTR _zh_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (mac_addr == NULL)
    {
#endif
        ZH_LOGE("Send callback received NULL MAC address.", ESP_ERR_INVALID_MAC);
        return;
    }
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xEventGroupSetBitsFromISR(_event_group_handle, (status == ESP_NOW_SEND_SUCCESS) ? DATA_SEND_SUCCESS : DATA_SEND_FAIL, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken == pdTRUE)
    {
        portYIELD_FROM_ISR();
    };
}

static void IRAM_ATTR _zh_espnow_recv_cb(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len)
{
    if (esp_now_info == NULL || data == NULL || data_len <= 0)
    {
        ZH_LOGE("Receive callback received invalid arguments.", ESP_ERR_INVALID_ARG);
        return;
    }
    if (uxQueueSpacesAvailable(_queue_handle) < _init_config.queue_size / 10)
    {
        ZH_LOGE("Queue is almost full. Dropping incoming ESP-NOW data.", ESP_ERR_INVALID_STATE);
        return;
    }
    _queue_t queue = {0};
    queue.id = ON_RECV;
    memcpy(queue.data.mac_addr, esp_now_info->src_addr, ESP_NOW_ETH_ALEN);
    queue.data.payload = heap_caps_calloc(1, data_len, MALLOC_CAP_8BIT);
    if (queue.data.payload == NULL)
    {
        ZH_LOGE("Memory allocation failed for incoming ESP-NOW data.", ESP_ERR_NO_MEM);
        return;
    }
    memcpy(queue.data.payload, data, data_len);
    queue.data.payload_len = data_len;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xQueueSendFromISR(_queue_handle, &queue, &xHigherPriorityTaskWoken) != pdTRUE)
    {
        ZH_LOGE("Failed to add incoming ESP-NOW data to queue.", ESP_ERR_NO_MEM);
        heap_caps_free(queue.data.payload);
        return;
    }
    if (xHigherPriorityTaskWoken == pdTRUE)
    {
        portYIELD_FROM_ISR();
    };
}

static void _zh_espnow_process_send(_queue_t *queue)
{
    esp_now_peer_info_t *peer = heap_caps_calloc(1, sizeof(esp_now_peer_info_t), MALLOC_CAP_8BIT);
    if (peer == NULL)
    {
        ZH_LOGE("Outgoing ESP-NOW data processed failed. Failed to allocate memory.", ESP_ERR_NO_MEM);
        heap_caps_free(queue->data.payload);
        return;
    }
    peer->ifidx = _init_config.wifi_interface;
    memcpy(peer->peer_addr, queue->data.mac_addr, ESP_NOW_ETH_ALEN);
    if (esp_now_add_peer(peer) != ESP_OK)
    {
        ZH_LOGE("Outgoing ESP-NOW data processed failed. Failed to add peer.", ESP_ERR_INVALID_STATE);
        heap_caps_free(queue->data.payload);
        heap_caps_free(peer);
        return;
    }
    zh_espnow_event_on_send_t *on_send = heap_caps_calloc(1, sizeof(zh_espnow_event_on_send_t), MALLOC_CAP_8BIT);
    if (on_send == NULL)
    {
        ZH_LOGE("Outgoing ESP-NOW data processed failed. Failed to allocate memory.", ESP_ERR_NO_MEM);
        heap_caps_free(queue->data.payload);
        esp_now_del_peer(peer->peer_addr);
        heap_caps_free(peer);
        return;
    }
    memcpy(on_send->mac_addr, queue->data.mac_addr, ESP_NOW_ETH_ALEN);
    for (uint8_t attempt = 0; attempt < _init_config.attempts; ++attempt)
    {
        if (esp_now_send(queue->data.mac_addr, queue->data.payload, queue->data.payload_len) != ESP_OK)
        {
            ZH_LOGE("Outgoing ESP-NOW data processed failed. ESP-NOW driver error.", ESP_ERR_INVALID_RESPONSE);
            continue;
        }
        EventBits_t bits = xEventGroupWaitBits(_event_group_handle, DATA_SEND_SUCCESS | DATA_SEND_FAIL, pdTRUE, pdFALSE, WAIT_CONFIRM_MAX_TIME / portTICK_PERIOD_MS);
        if (bits & DATA_SEND_SUCCESS)
        {
            on_send->status = ZH_ESPNOW_SEND_SUCCESS;
            ++_stats.sent_success;
            break;
        }
        else
        {
            on_send->status = ZH_ESPNOW_SEND_FAIL;
        }
    }
    if (on_send->status != ZH_ESPNOW_SEND_SUCCESS)
    {
        on_send->status = ZH_ESPNOW_SEND_FAIL;
        ++_stats.sent_fail;
    }
    if (esp_event_post(ZH_ESPNOW, ZH_ESPNOW_ON_SEND_EVENT, on_send, sizeof(zh_espnow_event_on_send_t), portTICK_PERIOD_MS) != ESP_OK)
    {
        ZH_LOGE("Outgoing ESP-NOW data processed failed. Failed to post send event.", ESP_ERR_INVALID_STATE);
    }
    heap_caps_free(queue->data.payload);
    esp_now_del_peer(peer->peer_addr);
    heap_caps_free(peer);
    heap_caps_free(on_send);
}

static void _zh_espnow_process_recv(_queue_t *queue)
{
    zh_espnow_event_on_recv_t *recv_data = (zh_espnow_event_on_recv_t *)&queue->data;
    ++_stats.received;
    if (esp_event_post(ZH_ESPNOW, ZH_ESPNOW_ON_RECV_EVENT, recv_data, sizeof(zh_espnow_event_on_recv_t), 1000 / portTICK_PERIOD_MS) != ESP_OK)
    {
        ZH_LOGE("Incoming ESP-NOW data processing failed. Failed to post event.", ESP_ERR_INVALID_STATE);
    }
}

static void IRAM_ATTR _zh_espnow_processing(void *pvParameter)
{
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
    }
    vTaskDelete(NULL);
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
    ZH_LOGI("ESP-NOW statistic reset successfully.");
}

esp_err_t zh_espnow_get_mac(uint8_t *mac_addr)
{
    return esp_wifi_get_mac(_init_config.wifi_interface, mac_addr);
}