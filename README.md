# ESP32 ESP-IDF component for ESP-NOW interface

There are two branches - for ESP8266 family and for ESP32 family. Please use the appropriate one.

## Using

In an existing project, run the following command to install the component:

```text
cd ../your_project/components
git clone -b esp32 --recursive http://git.zh.com.ru/alexey.zholtikov/zh_espnow.git
```

In the application, add the component:

```c
#include "zh_espnow.h"
```

## Example

Sending and receiving messages:

```c
#include "stdio.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "zh_espnow.h"

#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]

void zh_espnow_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

uint8_t target[6] = {0x34, 0x94, 0x54, 0x24, 0xA3, 0x41};
char *message = "Hello World!";

void app_main(void)
{
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_init_config);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    zh_espnow_init_config_t zh_espnow_init_config = ZH_ESPNOW_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(zh_espnow_init(&zh_espnow_init_config));
    esp_event_handler_instance_register(ZH_ESPNOW, ESP_EVENT_ANY_ID, &zh_espnow_event_handler, NULL, NULL);
    for (;;)
    {
        zh_espnow_send(NULL, (uint8_t *)message, strlen(message));
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        zh_espnow_send(target, (uint8_t *)message, strlen(message));
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void zh_espnow_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    zh_espnow_event_on_recv_t *recv_data = NULL;
    zh_espnow_event_on_send_t *send_data = NULL;
    switch (event_id)
    {
    case ZH_ESPNOW_ON_RECV_EVENT:
        recv_data = event_data;
        char *data = NULL;
        data = (char *)calloc(1, recv_data->data_len + 1);
        memcpy(data, recv_data->data, recv_data->data_len);
        printf("Message from MAC %02X:%02X:%02X:%02X:%02X:%02X is received. Data lenght %d bytes. Message: %s\n", MAC2STR(recv_data->mac_addr), recv_data->data_len, data);
        free(data);
        free(recv_data->data);
        break;
    case ZH_ESPNOW_ON_SEND_EVENT:
        send_data = event_data;
        if (send_data->status == ESP_NOW_SEND_SUCCESS)
        {
            printf("Message to MAC %02X:%02X:%02X:%02X:%02X:%02X sent success.\n", MAC2STR(send_data->mac_addr));
        }
        else
        {
            printf("Message to MAC %02X:%02X:%02X:%02X:%02X:%02X sent fail.\n", MAC2STR(send_data->mac_addr));
        }
    default:
        break;
    }
}
```