# ESP8266 RTOS SDK component for ESP-NOW interface

There are two branches - for ESP8266 family and for ESP32 family. Please use the appropriate one.

## Using

In an existing project, run the following command to install the component:

```text
cd ../your_project/components
git clone -b esp8266 --recursive http://git.zh.com.ru/alexey.zholtikov/zh_espnow.git
```

In the application, add the component:

```c
#include "zh_espnow.h"
```

## Example

Sending and receiving messages:

```c
#include "nvs_flash.h"
#include "esp_netif.h"
#include "zh_espnow.h"

#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]

void zh_espnow_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

uint8_t target[6] = {0x34, 0x94, 0x54, 0x24, 0xA3, 0x41};

typedef struct example_message_t
{
    char char_value[30];
    int int_value;
    float float_value;
    bool bool_value;
} example_message_t;

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
    zh_espnow_init(&zh_espnow_init_config);
    esp_event_handler_register(ZH_ESPNOW, ESP_EVENT_ANY_ID, &zh_espnow_event_handler, NULL);
    example_message_t send_message;
    for (;;)
    {
        strcpy(send_message.char_value, "THIS IS A CHAR");
        send_message.int_value = esp_random();
        send_message.float_value = 1.234;
        send_message.bool_value = false;
        zh_espnow_send(NULL, (uint8_t *)&send_message, sizeof(send_message));
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        zh_espnow_send(target, (uint8_t *)&send_message, sizeof(send_message));
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void zh_espnow_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case ZH_ESPNOW_ON_RECV_EVENT:;
        zh_espnow_event_on_recv_t *recv_data = event_data;
        printf("Message from MAC %02X:%02X:%02X:%02X:%02X:%02X is received. Data lenght %d bytes.\n", MAC2STR(recv_data->mac_addr), recv_data->data_len);
        example_message_t *recv_message = (example_message_t *)recv_data->data;
        printf("Char %s\n", recv_message->char_value);
        printf("Int %d\n", recv_message->int_value);
        printf("Float %f\n", recv_message->float_value);
        printf("Bool %d\n", recv_message->bool_value);
        free(recv_data->data); // Do not delete to avoid memory leaks!
        break;
    case ZH_ESPNOW_ON_SEND_EVENT:;
        zh_espnow_event_on_send_t *send_data = event_data;
        if (send_data->status == ZH_ESPNOW_SEND_SUCCESS)
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