#include "zh_espnow.h"

#define ZH_ESPNOW_DATA_SEND_SUCCESS BIT0
#define ZH_ESPNOW_DATA_SEND_FAIL BIT1

static void s_zh_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);
static void s_zh_espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int data_len);
static void s_zh_espnow_processing(void *pvParameter);

static EventGroupHandle_t s_zh_espnow_send_cb_status_event_group_handle = {0};
static QueueHandle_t s_zh_espnow_queue_handle = {0};
static TaskHandle_t s_zh_espnow_processing_task_handle = {0};
static zh_espnow_init_config_t s_zh_espnow_init_config = {0};

typedef enum zh_espnow_queue_id_t
{
    ZH_ESPNOW_RECV,
    ZH_ESPNOW_SEND,
} __attribute__((packed)) zh_espnow_queue_id_t;

typedef struct zh_espnow_queue_data_t
{
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    uint8_t *data;
    uint8_t data_len;
} __attribute__((packed)) zh_espnow_queue_data_t;

typedef struct zh_espnow_queue_t
{
    zh_espnow_queue_id_t id;
    zh_espnow_queue_data_t data;
} __attribute__((packed)) zh_espnow_queue_t;

ESP_EVENT_DEFINE_BASE(ZH_ESPNOW);

esp_err_t zh_espnow_init(zh_espnow_init_config_t *config)
{
    if (esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE) == ESP_ERR_WIFI_NOT_INIT)
    {
        return ESP_ERR_WIFI_NOT_INIT;
    }
    s_zh_espnow_init_config = *config;
    s_zh_espnow_send_cb_status_event_group_handle = xEventGroupCreate();
    s_zh_espnow_queue_handle = xQueueCreate(s_zh_espnow_init_config.queue_size, sizeof(zh_espnow_queue_t));
    esp_now_init();
    esp_now_register_send_cb(s_zh_espnow_send_cb);
    esp_now_register_recv_cb(s_zh_espnow_recv_cb);
    xTaskCreate(&s_zh_espnow_processing, "zh_espnow_processing", s_zh_espnow_init_config.stack_size, NULL, s_zh_espnow_init_config.task_priority, &s_zh_espnow_processing_task_handle);
    return ESP_OK;
}

void zh_espnow_deinit(void)
{
    vEventGroupDelete(s_zh_espnow_send_cb_status_event_group_handle);
    vQueueDelete(s_zh_espnow_queue_handle);
    esp_now_unregister_send_cb();
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    vTaskDelete(s_zh_espnow_processing_task_handle);
}

void zh_espnow_send(const uint8_t *target, const uint8_t *data, const uint8_t data_len)
{
    uint8_t broadcast[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    zh_espnow_queue_t espnow_queue = {0};
    espnow_queue.id = ZH_ESPNOW_SEND;
    zh_espnow_queue_data_t *send_data = &espnow_queue.data;
    if (target == NULL)
    {
        memcpy(send_data->mac_addr, broadcast, ESP_NOW_ETH_ALEN);
    }
    else
    {
        memcpy(send_data->mac_addr, target, ESP_NOW_ETH_ALEN);
    }
    send_data->data = calloc(1, data_len);
    memcpy(send_data->data, data, data_len);
    send_data->data_len = data_len;
    xQueueSend(s_zh_espnow_queue_handle, &espnow_queue, portMAX_DELAY);
}

static void IRAM_ATTR s_zh_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (status == ESP_NOW_SEND_SUCCESS)
    {
        xEventGroupSetBits(s_zh_espnow_send_cb_status_event_group_handle, ZH_ESPNOW_DATA_SEND_SUCCESS);
    }
    else
    {
        xEventGroupSetBits(s_zh_espnow_send_cb_status_event_group_handle, ZH_ESPNOW_DATA_SEND_FAIL);
    }
}

static void IRAM_ATTR s_zh_espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int data_len)
{
    zh_espnow_queue_t espnow_queue = {0};
    zh_espnow_queue_data_t *recv_data = &espnow_queue.data;
    espnow_queue.id = ZH_ESPNOW_RECV;
    memcpy(recv_data->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
    recv_data->data = calloc(1, data_len);
    memcpy(recv_data->data, data, data_len);
    recv_data->data_len = data_len;
    xQueueSend(s_zh_espnow_queue_handle, &espnow_queue, portMAX_DELAY);
}

static void s_zh_espnow_processing(void *pvParameter)
{
    zh_espnow_queue_t espnow_queue = {0};
    while (xQueueReceive(s_zh_espnow_queue_handle, &espnow_queue, portMAX_DELAY) == pdTRUE)
    {
        switch (espnow_queue.id)
        {
        case ZH_ESPNOW_SEND:;
            esp_now_peer_info_t *peer = calloc(1, sizeof(esp_now_peer_info_t));
            peer->ifidx = s_zh_espnow_init_config.wifi_interface;
            zh_espnow_queue_data_t *send_data = &espnow_queue.data;
            memcpy(peer->peer_addr, send_data->mac_addr, ESP_NOW_ETH_ALEN);
            esp_now_add_peer(peer);
            zh_espnow_event_on_send_t *on_send = calloc(1, sizeof(zh_espnow_event_on_send_t));
            memcpy(on_send->mac_addr, send_data->mac_addr, ESP_NOW_ETH_ALEN);
            esp_now_send(send_data->mac_addr, send_data->data, send_data->data_len);
            EventBits_t bit = xEventGroupWaitBits(s_zh_espnow_send_cb_status_event_group_handle, ZH_ESPNOW_DATA_SEND_SUCCESS | ZH_ESPNOW_DATA_SEND_FAIL, pdTRUE, pdFALSE, 50 / portTICK_PERIOD_MS);
            if ((bit & ZH_ESPNOW_DATA_SEND_SUCCESS) != 0)
            {
                on_send->status = ZH_ESPNOW_SEND_SUCCESS;
                esp_event_post(ZH_ESPNOW, ZH_ESPNOW_ON_SEND_EVENT, on_send, sizeof(zh_espnow_event_on_send_t), portMAX_DELAY);
            }
            else
            {
                on_send->status = ZH_ESPNOW_SEND_FAIL;
                esp_event_post(ZH_ESPNOW, ZH_ESPNOW_ON_SEND_EVENT, on_send, sizeof(zh_espnow_event_on_send_t), portMAX_DELAY);
            }
            free(send_data->data);
            esp_now_del_peer(send_data->mac_addr);
            free(peer);
            free(on_send);
            break;
        case ZH_ESPNOW_RECV:;
            zh_espnow_event_on_recv_t *recv_data = (zh_espnow_event_on_recv_t *)&espnow_queue.data;
            esp_event_post(ZH_ESPNOW, ZH_ESPNOW_ON_RECV_EVENT, recv_data, recv_data->data_len + 7, portMAX_DELAY);
            break;
        default:
            break;
        }
    }
    vTaskDelete(NULL);
}