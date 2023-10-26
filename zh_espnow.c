#include "zh_espnow.h"

#define DATA_SEND_SUCCESS BIT0
#define DATA_SEND_FAIL BIT1

static void s_zh_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);
static void s_zh_espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int data_len);
static void s_zh_espnow_processing(void *pvParameter);

static EventGroupHandle_t s_zh_espnow_send_cb_status = {0};
static QueueHandle_t s_zh_espnow_queue = {0};
static TaskHandle_t s_zh_espnow_processing_task = {0};
static zh_espnow_init_config_t s_zh_espnow_init_config = {0};

typedef enum
{
    ZH_ESPNOW_RECV,
    ZH_ESPNOW_SEND,
} zh_espnow_queue_id_t;

typedef struct
{
    uint8_t mac_addr[6];
    uint8_t *data;
    uint8_t data_len;
} zh_espnow_queue_data_t;

typedef struct
{
    zh_espnow_queue_id_t id;
    zh_espnow_queue_data_t data;
} zh_espnow_queue_t;

ESP_EVENT_DEFINE_BASE(ZH_ESPNOW);

esp_err_t zh_espnow_init(zh_espnow_init_config_t *config)
{
    if (esp_wifi_set_channel(1, 1) == ESP_ERR_WIFI_NOT_INIT)
    {
        return ESP_ERR_WIFI_NOT_INIT;
    }
    s_zh_espnow_init_config = *config;
    s_zh_espnow_send_cb_status = xEventGroupCreate();
    s_zh_espnow_queue = xQueueCreate(s_zh_espnow_init_config.queue_size, sizeof(zh_espnow_queue_t));
    esp_now_init();
    esp_now_register_send_cb(s_zh_espnow_send_cb);
    esp_now_register_recv_cb(s_zh_espnow_recv_cb);
    xTaskCreatePinnedToCore(&s_zh_espnow_processing, "zh_espnow_processing", s_zh_espnow_init_config.stack_size, NULL, s_zh_espnow_init_config.task_priority, &s_zh_espnow_processing_task, tskNO_AFFINITY);
    return ESP_OK;
}

void zh_espnow_deinit(void)
{
    vEventGroupDelete(s_zh_espnow_send_cb_status);
    vQueueDelete(s_zh_espnow_queue);
    esp_now_unregister_send_cb();
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    vTaskDelete(s_zh_espnow_processing_task);
}

void zh_espnow_send(const uint8_t *target, const uint8_t *data, const uint8_t data_len)
{
    uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    zh_espnow_queue_t espnow_queue = {0};
    espnow_queue.id = ZH_ESPNOW_SEND;
    zh_espnow_queue_data_t *send_data = &espnow_queue.data;
    if (target == NULL)
    {
        memcpy(send_data->mac_addr, broadcast, 6);
    }
    else
    {
        memcpy(send_data->mac_addr, target, 6);
    }
    send_data->data = calloc(1, data_len);
    memcpy(send_data->data, data, data_len);
    send_data->data_len = data_len;
    xQueueSend(s_zh_espnow_queue, &espnow_queue, portMAX_DELAY);
}

static void s_zh_espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (status == ESP_NOW_SEND_SUCCESS)
    {
        xEventGroupSetBits(s_zh_espnow_send_cb_status, DATA_SEND_SUCCESS);
    }
    else
    {
        xEventGroupSetBits(s_zh_espnow_send_cb_status, DATA_SEND_FAIL);
    }
}

static void s_zh_espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int data_len)
{
    zh_espnow_queue_t espnow_queue = {0};
    zh_espnow_queue_data_t *recv_data = &espnow_queue.data;
    espnow_queue.id = ZH_ESPNOW_RECV;
    memcpy(recv_data->mac_addr, mac_addr, 6);
    recv_data->data = calloc(1, data_len);
    memcpy(recv_data->data, data, data_len);
    recv_data->data_len = data_len;
    xQueueSend(s_zh_espnow_queue, &espnow_queue, portMAX_DELAY);
}

static void s_zh_espnow_processing(void *pvParameter)
{
    zh_espnow_queue_t espnow_queue = {0};
    esp_now_peer_info_t *peer = NULL;
    zh_espnow_queue_data_t *recv_data = NULL;
    zh_espnow_event_on_send_t *on_send = NULL;
    while (xQueueReceive(s_zh_espnow_queue, &espnow_queue, portMAX_DELAY) == pdTRUE)
    {
        switch (espnow_queue.id)
        {
        case ZH_ESPNOW_SEND:
            peer = calloc(1, sizeof(esp_now_peer_info_t));
            peer->ifidx = s_zh_espnow_init_config.wifi_interface;
            zh_espnow_queue_data_t *send_data = &espnow_queue.data;
            memcpy(peer->peer_addr, send_data->mac_addr, 6);
            esp_now_add_peer(peer);
            uint8_t attempted_transmission = {0};
            on_send = calloc(1, sizeof(zh_espnow_event_on_send_t));
            memcpy(on_send->mac_addr, send_data->mac_addr, 6);
        RESEND_ESPNOW_MESSAGE:
            esp_now_send(send_data->mac_addr, send_data->data, send_data->data_len);
            EventBits_t bit = xEventGroupWaitBits(s_zh_espnow_send_cb_status, DATA_SEND_SUCCESS | DATA_SEND_FAIL, pdTRUE, pdFALSE, 50 / portTICK_PERIOD_MS);
            if (bit & DATA_SEND_SUCCESS)
            {
                on_send->status = ESP_NOW_SEND_SUCCESS;
                esp_event_post(ZH_ESPNOW, ZH_ESPNOW_ON_SEND_EVENT, on_send, sizeof(zh_espnow_event_on_send_t), portMAX_DELAY);
            }
            else if (bit & DATA_SEND_FAIL)
            {
                if (attempted_transmission < s_zh_espnow_init_config.max_attempts)
                {
                    ++attempted_transmission;
                    goto RESEND_ESPNOW_MESSAGE;
                }
                on_send->status = ESP_NOW_SEND_FAIL;
                esp_event_post(ZH_ESPNOW, ZH_ESPNOW_ON_SEND_EVENT, on_send, sizeof(zh_espnow_event_on_send_t), portMAX_DELAY);
            }
            free(send_data->data);
            esp_now_del_peer(send_data->mac_addr);
            free(peer);
            free(on_send);
            break;
        case ZH_ESPNOW_RECV:
            recv_data = &espnow_queue.data;
            esp_event_post(ZH_ESPNOW, ZH_ESPNOW_ON_RECV_EVENT, recv_data, recv_data->data_len + 6 + 1, portMAX_DELAY);
            break;
        default:
            break;
        }
    }
    vTaskDelete(NULL);
}