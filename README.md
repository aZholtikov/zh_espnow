# ESP32 ESP-IDF and ESP8266 RTOS SDK component for ESP-NOW interface

## Tested on

1. [ESP8266 RTOS_SDK v3.4](https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/index.html#)
2. [ESP32 ESP-IDF v5.4](https://docs.espressif.com/projects/esp-idf/en/release-v5.4/esp32/index.html)

## Features

1. The maximum size of the transmitted data is up to 250 / 1490 bytes. Please see attention for details.
2. Support of any data types.
3. All nodes are not visible to the network scanner.
4. Not required a pre-pairings for data transfer.
5. Broadcast or unicast data transmissions.
6. Possibility uses WiFi AP or STA modes at the same time with ESP-NOW. Please see attention for details.

## Attention

1. For correct operation ESP-NOW interface must be the same as the WiFi interface (except in the case of APSTA mode - the ESP-NOW interface can be anything).
2. For correct operation in ESP-NOW + STA mode, your WiFi router must be set to the same channel as ESP-NOW.
3. All devices on the network must have the same WiFi channel.
4. For use encrypted messages use the application layer.
5. ESP-NOW supports two versions: v1.0 (RTOS_SDK and ESP-IDF v5.3 and below) and v2.0 (ESP-IDF v5.4 and highter). The maximum packet length supported by v2.0 devices is 1490 bytes while the maximum packet length supported by v1.0 devices is 250 bytes. The v2.0 devices are capable of receiving packets from both v2.0 and v1.0 devices. In contrast v1.0 devices can only receive packets from other v1.0 devices. However v1.0 devices can receive v2.0 packets if the packet length is less than or equal to 250 bytes. For packets exceeding this length the v1.0 devices will either truncate the data to the first 250 bytes or discard the packet entirely.

## Using

In an existing project, run the following command to install the component:

```text
cd ../your_project/components
git clone http://git.zh.com.ru/alexey.zholtikov/zh_espnow
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
#ifdef CONFIG_IDF_TARGET_ESP8266
#include "esp_system.h"
#else
#include "esp_random.h"
#endif

#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]

void zh_espnow_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

uint8_t target[6] = {0xEC, 0x94, 0xCB, 0x87, 0xEC, 0xFC};

typedef struct
{
    char char_value[30];
    int int_value;
    float float_value;
    bool bool_value;
} example_message_t;

void app_main(void)
{
    esp_log_level_set("zh_espnow", ESP_LOG_NONE); // For ESP8266 first enable "Component config -> Log output -> Enable log set level" via menuconfig.
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_init_config);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    zh_espnow_init_config_t espnow_init_config = ZH_ESPNOW_INIT_CONFIG_DEFAULT();
    zh_espnow_init(&espnow_init_config);
#ifdef CONFIG_IDF_TARGET_ESP8266
    esp_event_handler_register(ZH_ESPNOW, ESP_EVENT_ANY_ID, &zh_espnow_event_handler, NULL);
#else
    esp_event_handler_instance_register(ZH_ESPNOW, ESP_EVENT_ANY_ID, &zh_espnow_event_handler, NULL, NULL);
#endif
    example_message_t send_message = {0};
    strcpy(send_message.char_value, "THIS IS A CHAR");
    send_message.float_value = 1.234;
    send_message.bool_value = false;
    printf("ESP-NOW version %d.\n", zh_espnow_get_version());
    printf("ESP-NOW channel %d. \n", zh_espnow_get_channel());
    uint8_t node_mac[6] = {0};
    zh_espnow_get_mac(node_mac);
    printf("ESP-NOW MAC %02X:%02X:%02X:%02X:%02X:%02X.\n", MAC2STR(node_mac));
    uint8_t counter = 0;
    for (;;)
    {
        counter++;
        send_message.int_value = esp_random();
        zh_espnow_send(NULL, (uint8_t *)&send_message, sizeof(send_message));
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        zh_espnow_send(target, (uint8_t *)&send_message, sizeof(send_message));
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        if (counter == 10)
        {
            counter = 0;
            const zh_espnow_stats_t *stats = zh_espnow_get_stats();
            printf("Number of successfully sent messages: %ld.\n", stats->sent_success);
            printf("Number of failed sent messages: %ld.\n", stats->sent_fail);
            printf("Number of received messages: %ld.\n", stats->received);
        }
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
        heap_caps_free(recv_data->data); // Do not delete to avoid memory leaks!
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
