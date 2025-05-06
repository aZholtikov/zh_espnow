#include "zh_espnow.h"

static const char *TAG = "zh_espnow";

#define ZH_ESPNOW_LOGI(msg, ...) ESP_LOGI(TAG, msg, ##__VA_ARGS__)
#define ZH_ESPNOW_LOGW(msg, ...) ESP_LOGW(TAG, msg, ##__VA_ARGS__)
#define ZH_ESPNOW_LOGE(msg, ...) ESP_LOGE(TAG, msg, ##__VA_ARGS__)
#define ZH_ESPNOW_LOGE_ERR(msg, err, ...) ESP_LOGE(TAG, "[%s:%d:%s] " msg, __FILE__, __LINE__, esp_err_to_name(err), ##__VA_ARGS__)

#define ZH_ESPNOW_CHECK(cond, err, msg, ...) \
    if (!(cond))                             \
    {                                        \
        ZH_ESPNOW_LOGE_ERR(msg, err);        \
        return err;                          \
    }

#define DATA_SEND_SUCCESS BIT0
#define DATA_SEND_FAIL BIT1
#define WAIT_CONFIRM_MAX_TIME 50
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]

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

ESP_EVENT_DEFINE_BASE(ZH_ESPNOW);

static esp_err_t _zh_espnow_init_wifi(const zh_espnow_init_config_t *config);
static esp_err_t _zh_espnow_init_resources(const zh_espnow_init_config_t *config);
static esp_err_t _zh_espnow_validate_config(const zh_espnow_init_config_t *config);
static esp_err_t _zh_espnow_register_callbacks(bool battery_mode);
static esp_err_t _zh_espnow_create_task(const zh_espnow_init_config_t *config);
static void _zh_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);
#if defined CONFIG_IDF_TARGET_ESP8266 || ESP_IDF_VERSION_MAJOR == 4
static void _zh_espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int data_len);
#else
static void _zh_espnow_recv_cb(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len);
#endif
static void _zh_espnow_process_send(_queue_t *queue);
static void _zh_espnow_process_recv(_queue_t *queue);
static void _zh_espnow_processing(void *pvParameter);

static EventGroupHandle_t _event_group_handle = {0};
static QueueHandle_t _queue_handle = {0};
static TaskHandle_t _processing_task_handle = {0};
static zh_espnow_init_config_t _init_config = {0};
static zh_espnow_stats_t _stats = {0};
static bool _is_initialized = false;
static const uint8_t _broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#if defined ESP_NOW_MAX_DATA_LEN_V2
static uint16_t _max_message_size = ESP_NOW_MAX_DATA_LEN_V2;
#else
static uint16_t _max_message_size = ESP_NOW_MAX_DATA_LEN;
#endif

esp_err_t zh_espnow_init(const zh_espnow_init_config_t *config)
{
    ZH_ESPNOW_LOGI("ESP-NOW initialization started.");
    if (_is_initialized == true)
    {
        ZH_ESPNOW_LOGW("ESP-NOW initialization failed. ESP-NOW is already initialized.");
        return ESP_OK;
    }
    esp_err_t err = _zh_espnow_validate_config(config);
    if (err != ESP_OK)
    {
        ZH_ESPNOW_LOGE("ESP-NOW initialization failed. Initial configuration check failed.");
        return err;
    }
    else
    {
        ZH_ESPNOW_LOGI("ESP-NOW initial configuration check completed successfully.");
    }
    _init_config = *config;
    err = _zh_espnow_init_wifi(config);
    if (err != ESP_OK)
    {
        ZH_ESPNOW_LOGE("ESP-NOW initialization failed. WiFi initialization failed.");
        return err;
    }
    else
    {
        ZH_ESPNOW_LOGI("WiFi initialization completed successfully.");
    }
    err = _zh_espnow_init_resources(config);
    if (err != ESP_OK)
    {
        ZH_ESPNOW_LOGE("ESP-NOW initialization failed. Resources initialization failed.");
        goto CLEANUP;
    }
    else
    {
        ZH_ESPNOW_LOGI("ESP-NOW resources initialization completed successfully.");
    }
    err = esp_now_init();
    if (err != ESP_OK)
    {
        ZH_ESPNOW_LOGE_ERR("ESP-NOW initialization failed. ESP-NOW driver initialization failed.", err);
        goto CLEANUP;
    }
    else
    {
        ZH_ESPNOW_LOGI("ESP-NOW driver initialization completed successfully.");
    }
    err = _zh_espnow_register_callbacks(config->battery_mode);
    if (err != ESP_OK)
    {
        ZH_ESPNOW_LOGE("ESP-NOW initialization failed. ESP-NOW callbacks registration failed.");
        goto CLEANUP;
    }
    else
    {
        ZH_ESPNOW_LOGI("ESP-NOW callbacks registered successfully.");
    }
    err = _zh_espnow_create_task(config);
    if (err != ESP_OK)
    {
        ZH_ESPNOW_LOGE("ESP-NOW initialization failed. Processing task initialization failed.");
        goto CLEANUP;
    }
    else
    {
        ZH_ESPNOW_LOGI("ESP-NOW processing task initialization completed successfully.");
    }
    _is_initialized = true;
    ZH_ESPNOW_LOGI("ESP-NOW initialization completed successfully.");
    return ESP_OK;
CLEANUP:
    zh_espnow_deinit();
    return err;
}

esp_err_t zh_espnow_deinit(void)
{
    ZH_ESPNOW_LOGI("ESP-NOW deinitialization started.");
    if (_is_initialized == false)
    {
        ZH_ESPNOW_LOGW("ESP-NOW deinitialization skipped. ESP-NOW is not initialized.");
        return ESP_FAIL;
    }
    esp_err_t final_status = ESP_OK;
    if (_event_group_handle != NULL)
    {
        vEventGroupDelete(_event_group_handle);
        _event_group_handle = NULL;
        ZH_ESPNOW_LOGI("Event group deleted.");
    }
    if (_queue_handle != NULL)
    {
        _queue_t queue = {0};
        while (xQueueReceive(_queue_handle, &queue, 0) == pdTRUE)
        {
            if (queue.data.payload != NULL)
            {
                heap_caps_free(queue.data.payload);
                ZH_ESPNOW_LOGI("Freed payload memory from queue.");
            }
        }
        vQueueDelete(_queue_handle);
        _queue_handle = NULL;
        ZH_ESPNOW_LOGI("Queue deleted.");
    }
    esp_err_t err = esp_now_unregister_send_cb();
    if (err != ESP_OK)
    {
        ZH_ESPNOW_LOGE_ERR("Failed to unregister send callback.", err);
        final_status = err;
    }
    else
    {
        ZH_ESPNOW_LOGI("Send callback unregistered.");
    }
    if (_init_config.battery_mode == false)
    {
        err = esp_now_unregister_recv_cb();
        if (err != ESP_OK)
        {
            ZH_ESPNOW_LOGE_ERR("Failed to unregister receive callback.", err);
            final_status = err;
        }
        else
        {
            ZH_ESPNOW_LOGI("Receive callback unregistered.");
        }
    }
    err = esp_now_deinit();
    if (err != ESP_OK)
    {
        ZH_ESPNOW_LOGE_ERR("ESP-NOW driver deinitialization failed.", err);
        final_status = err;
    }
    else
    {
        ZH_ESPNOW_LOGI("ESP-NOW driver deinitialized.");
    }
    if (_processing_task_handle != NULL)
    {
        vTaskDelete(_processing_task_handle);
        _processing_task_handle = NULL;
        ZH_ESPNOW_LOGI("Processing task deleted.");
    }
    _is_initialized = false;
    ZH_ESPNOW_LOGI("ESP-NOW deinitialization completed successfully.");
    return final_status;
}

esp_err_t zh_espnow_send(const uint8_t *target, const uint8_t *data, const uint16_t data_len)
{
    ZH_ESPNOW_LOGI("Adding to queue outgoing ESP-NOW data to MAC %02X:%02X:%02X:%02X:%02X:%02X started.", MAC2STR((target == NULL) ? _broadcast_mac : target));
    if (_is_initialized == false)
    {
        ZH_ESPNOW_LOGE("Outgoing ESP-NOW data to MAC %02X:%02X:%02X:%02X:%02X:%02X added to queue failed. ESP-NOW is not initialized.", MAC2STR((target == NULL) ? _broadcast_mac : target));
        return ESP_FAIL;
    }
    if (data == NULL || data_len == 0 || data_len > _max_message_size)
    {
        ZH_ESPNOW_LOGE("Outgoing ESP-NOW data to MAC %02X:%02X:%02X:%02X:%02X:%02X added to queue failed. Invalid arguments.", MAC2STR((target == NULL) ? _broadcast_mac : target));
        return ESP_ERR_INVALID_ARG;
    }
    if (uxQueueSpacesAvailable(_queue_handle) < _init_config.queue_size / 10)
    {
        ZH_ESPNOW_LOGW("Outgoing ESP-NOW data to MAC %02X:%02X:%02X:%02X:%02X:%02X added to queue failed. Queue is almost full.", MAC2STR((target == NULL) ? _broadcast_mac : target));
        return ESP_ERR_INVALID_STATE;
    }
    _queue_t queue = {0};
    queue.id = TO_SEND;
    memcpy(queue.data.mac_addr, (target == NULL) ? _broadcast_mac : target, ESP_NOW_ETH_ALEN);
    queue.data.payload = heap_caps_calloc(1, data_len, MALLOC_CAP_8BIT);
    if (queue.data.payload == NULL)
    {
        ZH_ESPNOW_LOGE("Outgoing ESP-NOW data to MAC %02X:%02X:%02X:%02X:%02X:%02X added to queue failed. Memory allocation failed.", MAC2STR((target == NULL) ? _broadcast_mac : target));
        return ESP_ERR_NO_MEM;
    }
    memcpy(queue.data.payload, data, data_len);
    queue.data.payload_len = data_len;
    if (xQueueSend(_queue_handle, &queue, portTICK_PERIOD_MS) != pdTRUE)
    {
        ZH_ESPNOW_LOGE("Outgoing ESP-NOW data to MAC %02X:%02X:%02X:%02X:%02X:%02X added to queue failed. Failed to add data to queue.", MAC2STR((target == NULL) ? _broadcast_mac : target));
        heap_caps_free(queue.data.payload);
        return ESP_FAIL;
    }
    ZH_ESPNOW_LOGI("Outgoing ESP-NOW data to MAC %02X:%02X:%02X:%02X:%02X:%02X added to queue successfully.", MAC2STR((target == NULL) ? _broadcast_mac : target));
    return ESP_OK;
}

static esp_err_t _zh_espnow_init_wifi(const zh_espnow_init_config_t *config)
{
    esp_err_t err = esp_wifi_set_channel(config->wifi_channel, WIFI_SECOND_CHAN_NONE);
    ZH_ESPNOW_CHECK(err == ESP_OK, err, "WiFi channel setup failed.");
#if defined CONFIG_IDF_TARGET_ESP8266 || CONFIG_IDF_TARGET_ESP32C2
    err = esp_wifi_set_protocol(config->wifi_interface, WIFI_PROTOCOL_11B);
#else
    err = esp_wifi_set_protocol(config->wifi_interface, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_LR);
#endif
    ZH_ESPNOW_CHECK(err == ESP_OK, err, "WiFi protocol setup failed.");
    return ESP_OK;
}

static esp_err_t _zh_espnow_init_resources(const zh_espnow_init_config_t *config)
{
    _event_group_handle = xEventGroupCreate();
    ZH_ESPNOW_CHECK(_event_group_handle != NULL, ESP_FAIL, "Event group creation failed.");
    _queue_handle = xQueueCreate(config->queue_size, sizeof(_queue_t));
    ZH_ESPNOW_CHECK(_queue_handle != 0, ESP_FAIL, "Queue creation failed.");
    return ESP_OK;
}

static esp_err_t _zh_espnow_create_task(const zh_espnow_init_config_t *config)
{
    BaseType_t err = xTaskCreatePinnedToCore(
        &_zh_espnow_processing,
        "zh_espnow_processing",
        config->stack_size,
        NULL,
        config->task_priority,
        &_processing_task_handle,
        tskNO_AFFINITY);
    ZH_ESPNOW_CHECK(err == pdPASS, ESP_FAIL, "Task creation failed.");
    return ESP_OK;
}

static esp_err_t _zh_espnow_validate_config(const zh_espnow_init_config_t *config)
{
    ZH_ESPNOW_CHECK(config != NULL, ESP_ERR_INVALID_ARG, "Invalid configuration.");
    ZH_ESPNOW_CHECK(config->wifi_channel > 0 && config->wifi_channel < 15, ESP_ERR_INVALID_ARG, "Invalid WiFi channel.");
    ZH_ESPNOW_CHECK(config->task_priority >= 5 && config->stack_size >= 2048, ESP_ERR_INVALID_ARG, "Invalid task settings.");
    ZH_ESPNOW_CHECK(config->queue_size >= 16, ESP_ERR_INVALID_ARG, "Invalid queue size.");
    ZH_ESPNOW_CHECK(config->attempts > 0, ESP_ERR_INVALID_ARG, "Invalid number of attempts.");
    return ESP_OK;
}

static esp_err_t _zh_espnow_register_callbacks(bool battery_mode)
{
    esp_err_t err = esp_now_register_send_cb(_zh_espnow_send_cb);
    ZH_ESPNOW_CHECK(err == ESP_OK, err, "Send callback registration failed.");
    if (battery_mode == false)
    {
        err = esp_now_register_recv_cb(_zh_espnow_recv_cb);
        ZH_ESPNOW_CHECK(err == ESP_OK, err, "Receive callback registration failed.");
    }
    return ESP_OK;
}

static void IRAM_ATTR _zh_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (mac_addr == NULL)
    {
        ZH_ESPNOW_LOGE("Send callback received NULL MAC address.");
        return;
    }
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    ZH_ESPNOW_LOGI("ESP-NOW send callback: %s for MAC %02X:%02X:%02X:%02X:%02X:%02X.", (status == ESP_NOW_SEND_SUCCESS) ? "SUCCESS" : "FAIL", MAC2STR(mac_addr));
    xEventGroupSetBitsFromISR(_event_group_handle, (status == ESP_NOW_SEND_SUCCESS) ? DATA_SEND_SUCCESS : DATA_SEND_FAIL, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken == pdTRUE)
    {
        portYIELD_FROM_ISR();
    };
}

#if defined CONFIG_IDF_TARGET_ESP8266 || ESP_IDF_VERSION_MAJOR == 4
static void IRAM_ATTR _zh_espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int data_len)
#else
static void IRAM_ATTR _zh_espnow_recv_cb(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int data_len)
#endif
{
#if defined CONFIG_IDF_TARGET_ESP8266 || ESP_IDF_VERSION_MAJOR == 4
    if (mac_addr == NULL || data == NULL || data_len <= 0)
#else
    if (esp_now_info == NULL || data == NULL || data_len <= 0)
#endif
    {
        ZH_ESPNOW_LOGE("Receive callback received invalid arguments.");
        return;
    }
    if (uxQueueSpacesAvailable(_queue_handle) < _init_config.queue_size / 10)
    {
        ZH_ESPNOW_LOGE("Queue is almost full. Dropping incoming ESP-NOW data.");
        return;
    }
    _queue_t queue = {0};
    queue.id = ON_RECV;
#if defined CONFIG_IDF_TARGET_ESP8266 || ESP_IDF_VERSION_MAJOR == 4
    memcpy(queue.data.mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
#else
    memcpy(queue.data.mac_addr, esp_now_info->src_addr, ESP_NOW_ETH_ALEN);
#endif
    queue.data.payload = heap_caps_calloc(1, data_len, MALLOC_CAP_8BIT);
    if (queue.data.payload == NULL)
    {
        ZH_ESPNOW_LOGE("Memory allocation failed for incoming ESP-NOW data.");
        return;
    }
    memcpy(queue.data.payload, data, data_len);
    queue.data.payload_len = data_len;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xQueueSendFromISR(_queue_handle, &queue, &xHigherPriorityTaskWoken) != pdTRUE)
    {
        ZH_ESPNOW_LOGE("Failed to add incoming ESP-NOW data to queue.");
        heap_caps_free(queue.data.payload);
        return;
    }
#if defined CONFIG_IDF_TARGET_ESP8266 || ESP_IDF_VERSION_MAJOR == 4
    ZH_ESPNOW_LOGI("Incoming ESP-NOW data from MAC %02X:%02X:%02X:%02X:%02X:%02X added to queue successfully.", MAC2STR(mac_addr));
#else
    ZH_ESPNOW_LOGI("Incoming ESP-NOW data from MAC %02X:%02X:%02X:%02X:%02X:%02X added to queue successfully.", MAC2STR(esp_now_info->src_addr));
    if (xHigherPriorityTaskWoken == pdTRUE)
    {
        portYIELD_FROM_ISR();
    };
#endif
}

static void _zh_espnow_process_send(_queue_t *queue)
{
    ZH_ESPNOW_LOGI("Processing outgoing ESP-NOW data to MAC %02X:%02X:%02X:%02X:%02X:%02X started.", MAC2STR(queue->data.mac_addr));
    esp_now_peer_info_t *peer = heap_caps_calloc(1, sizeof(esp_now_peer_info_t), MALLOC_CAP_8BIT);
    if (peer == NULL)
    {
        ZH_ESPNOW_LOGE("Outgoing ESP-NOW data to MAC %02X:%02X:%02X:%02X:%02X:%02X processed failed. Failed to allocate memory.", MAC2STR(queue->data.mac_addr));
        heap_caps_free(queue->data.payload);
        return;
    }
    peer->ifidx = _init_config.wifi_interface;
    memcpy(peer->peer_addr, queue->data.mac_addr, ESP_NOW_ETH_ALEN);
    esp_err_t err = esp_now_add_peer(peer);
    if (err != ESP_OK)
    {
        ZH_ESPNOW_LOGE_ERR("Outgoing ESP-NOW data to MAC %02X:%02X:%02X:%02X:%02X:%02X processed failed. Failed to add peer.", err, MAC2STR(queue->data.mac_addr));
        heap_caps_free(queue->data.payload);
        heap_caps_free(peer);
        return;
    }
    zh_espnow_event_on_send_t *on_send = heap_caps_calloc(1, sizeof(zh_espnow_event_on_send_t), MALLOC_CAP_8BIT);
    if (on_send == NULL)
    {
        ZH_ESPNOW_LOGE("Outgoing ESP-NOW data to MAC %02X:%02X:%02X:%02X:%02X:%02X processed failed. Failed to allocate memory.", MAC2STR(queue->data.mac_addr));
        heap_caps_free(queue->data.payload);
        esp_now_del_peer(peer->peer_addr);
        heap_caps_free(peer);
        return;
    }
    memcpy(on_send->mac_addr, queue->data.mac_addr, ESP_NOW_ETH_ALEN);
    for (uint8_t attempt = 0; attempt < _init_config.attempts; ++attempt)
    {
        err = esp_now_send(queue->data.mac_addr, queue->data.payload, queue->data.payload_len);
        if (err != ESP_OK)
        {
            ZH_ESPNOW_LOGE_ERR("Outgoing ESP-NOW data to MAC %02X:%02X:%02X:%02X:%02X:%02X processed failed. ESP-NOW driver error.", err, MAC2STR(queue->data.mac_addr));
            continue;
        }
        EventBits_t bits = xEventGroupWaitBits(_event_group_handle, DATA_SEND_SUCCESS | DATA_SEND_FAIL, pdTRUE, pdFALSE, WAIT_CONFIRM_MAX_TIME / portTICK_PERIOD_MS);
        if (bits & DATA_SEND_SUCCESS)
        {
            ESP_LOGI(TAG, "ESP-NOW data sent successfully to MAC %02X:%02X:%02X:%02X:%02X:%02X after %d attempts.", MAC2STR(queue->data.mac_addr), attempt + 1);
            on_send->status = ZH_ESPNOW_SEND_SUCCESS;
            ++_stats.sent_success;
            break;
        }
        else
        {
            ESP_LOGW(TAG, "ESP-NOW data send failed to MAC %02X:%02X:%02X:%02X:%02X:%02X on attempt %d.", MAC2STR(queue->data.mac_addr), attempt + 1);
            on_send->status = ZH_ESPNOW_SEND_FAIL;
        }
    }
    if (on_send->status != ZH_ESPNOW_SEND_SUCCESS)
    {
        ESP_LOGE(TAG, "Failed to send ESP-NOW data to MAC %02X:%02X:%02X:%02X:%02X:%02X after %d attempts.", MAC2STR(queue->data.mac_addr), _init_config.attempts);
        on_send->status = ZH_ESPNOW_SEND_FAIL;
        ++_stats.sent_fail;
    }
    err = esp_event_post(ZH_ESPNOW, ZH_ESPNOW_ON_SEND_EVENT, on_send, sizeof(zh_espnow_event_on_send_t), portTICK_PERIOD_MS);
    if (err == ESP_OK)
    {
        ZH_ESPNOW_LOGI("Outgoing ESP-NOW data to MAC %02X:%02X:%02X:%02X:%02X:%02X processed successfully.", MAC2STR(queue->data.mac_addr));
    }
    else
    {
        ZH_ESPNOW_LOGE_ERR("Outgoing ESP-NOW data to MAC %02X:%02X:%02X:%02X:%02X:%02X processed failed. Failed to post send event.", err, MAC2STR(queue->data.mac_addr));
    }
    heap_caps_free(queue->data.payload);
    esp_now_del_peer(peer->peer_addr);
    heap_caps_free(peer);
    heap_caps_free(on_send);
}

static void _zh_espnow_process_recv(_queue_t *queue)
{
    ZH_ESPNOW_LOGI("Processing incoming ESP-NOW data from MAC %02X:%02X:%02X:%02X:%02X:%02X started.", MAC2STR(queue->data.mac_addr));
    zh_espnow_event_on_recv_t *recv_data = (zh_espnow_event_on_recv_t *)&queue->data;
    ++_stats.received;
    esp_err_t err = esp_event_post(ZH_ESPNOW, ZH_ESPNOW_ON_RECV_EVENT, recv_data, sizeof(zh_espnow_event_on_recv_t), portTICK_PERIOD_MS);
    if (err == ESP_OK)
    {
        ZH_ESPNOW_LOGI("Incoming ESP-NOW data from MAC %02X:%02X:%02X:%02X:%02X:%02X processed successfully.", MAC2STR(queue->data.mac_addr));
    }
    else
    {
        ZH_ESPNOW_LOGE_ERR("Incoming ESP-NOW data from MAC %02X:%02X:%02X:%02X:%02X:%02X processed failed. Failed to post receive event.", err, MAC2STR(queue->data.mac_addr));
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
            ZH_ESPNOW_LOGW("Unknown queue ID: %d.", queue.id);
            break;
        }
    }
    vTaskDelete(NULL);
}

uint8_t zh_espnow_get_version(void)
{
    ZH_ESPNOW_LOGI("ESP-NOW version receipt started.");
    uint32_t version = 0;
    esp_err_t err = esp_now_get_version(&version);
    if (err != ESP_OK)
    {
        ZH_ESPNOW_LOGE_ERR("ESP-NOW version receiption failed.", err);
        return 0;
    }
    ZH_ESPNOW_LOGI("ESP-NOW version receiption successfully.");
    return (uint8_t)version;
}

const zh_espnow_stats_t *zh_espnow_get_stats(void)
{
    return &_stats;
}

void zh_espnow_reset_stats(void)
{
    _stats.sent_success = 0;
    _stats.sent_fail = 0;
    _stats.received = 0;
    ZH_ESPNOW_LOGI("ESP-NOW statistic reset successfully.");
}

bool zh_espnow_is_initialized(void)
{
    return _is_initialized;
}

uint8_t zh_espnow_get_attempts(void)
{
    return _init_config.attempts;
}

esp_err_t zh_espnow_set_attempts(uint8_t attempts)
{
    ZH_ESPNOW_CHECK(_is_initialized == true, ESP_ERR_INVALID_STATE, "Number of attempts set failed. ESP-NOW is not initialized.");
    ZH_ESPNOW_CHECK(attempts > 0, ESP_ERR_INVALID_ARG, "Number of attempts set failed. Invalid number.");
    _init_config.attempts = attempts;
    ZH_ESPNOW_LOGI("Number of attempts set successfully.");
    return ESP_OK;
}

uint8_t zh_espnow_get_channel(void)
{
    if (_is_initialized == false)
    {
        ZH_ESPNOW_LOGE("ESP-NOW channel receiption failed. ESP-NOW is not initialized.");
        return 0;
    }
    uint8_t prim_channel = 0;
    wifi_second_chan_t sec_channel = 0;
    esp_err_t err = esp_wifi_get_channel(&prim_channel, &sec_channel);
    if (err != ESP_OK)
    {
        ZH_ESPNOW_LOGE_ERR("ESP-NOW channel receiption failed.", err);
        return 0;
    }
    _init_config.wifi_channel = prim_channel;
    ZH_ESPNOW_LOGI("ESP-NOW channel receiption successfully.");
    return prim_channel;
}

esp_err_t zh_espnow_set_channel(uint8_t channel)
{
    ZH_ESPNOW_CHECK(_is_initialized == true, ESP_ERR_INVALID_STATE, "ESP-NOW channel set failed. ESP-NOW is not initialized.");
    ZH_ESPNOW_CHECK(channel > 0 && channel < 15, ESP_ERR_INVALID_ARG, "ESP-NOW channel set failed. Invalid channel.");
    esp_err_t err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK)
    {
        ZH_ESPNOW_LOGE_ERR("ESP-NOW channel set failed.", err);
        return err;
    }
    _init_config.wifi_channel = channel;
    ZH_ESPNOW_LOGI("ESP-NOW channel set successfully.");
    return err;
}

bool zh_espnow_get_battery_mode(void)
{
    return _init_config.battery_mode;
}

esp_err_t zh_espnow_set_battery_mode(bool battery_mode)
{
    ZH_ESPNOW_CHECK(_is_initialized == true, ESP_ERR_INVALID_STATE, "Battery mode set failed. ESP-NOW is not initialized.");
    esp_err_t err = esp_now_unregister_send_cb();
    ZH_ESPNOW_CHECK(err == ESP_OK, err, "Battery mode set failed. Failed to unregister send callback.");
    if (_init_config.battery_mode == false)
    {
        err = esp_now_unregister_recv_cb();
        ZH_ESPNOW_CHECK(err == ESP_OK, err, "Battery mode set failed. Failed to unregister receive callback.");
    }
    err = _zh_espnow_register_callbacks(battery_mode);
    ZH_ESPNOW_CHECK(err == ESP_OK, err, "Battery mode set failed. Failed to register callbacks.");
    _init_config.battery_mode = battery_mode;
    ZH_ESPNOW_LOGI("Battery mode set successfully.");
    return ESP_OK;
}

esp_err_t zh_espnow_get_mac(uint8_t *mac_addr)
{
    return esp_wifi_get_mac(_init_config.wifi_interface, mac_addr);
}