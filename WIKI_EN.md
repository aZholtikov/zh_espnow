# zh_espnow - ESP-NOW Interface Component for ESP-IDF

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Attention](#attention)
- [Installation](#installation)
- [API Reference](#api-reference)
- [Usage Examples](#usage-examples)
- [Technical Specifications](#technical-specifications)
- [Error Codes](#error-codes)
- [Contributing](#contributing)
- [License](#license)

---

## Overview

`zh_espnow` is a component for ESP-IDF (Espressif IoT Development Framework) that provides a thread-safe and convenient interface for working with the ESP-NOW protocol.

The component is designed specifically for ESP32 microcontrollers and provides asynchronous message processing through a dedicated FreeRTOS task. All send and receive operations are performed in the background, allowing the main application to focus on its logic.

The component supports broadcast and unicast data transmission, statistics tracking, and power-saving mode (battery mode).

ESP-NOW supports two versions:

- **v1.0**: ESP-IDF v5.3 and below - maximum packet size 250 bytes
- **v2.0**: ESP-IDF v5.4 and above - maximum packet size 1490 bytes

v2.0 devices can receive packets from both v2.0 and v1.0 devices. v1.0 devices can receive packets from other v1.0 devices, as well as packets from v2.0 devices if the packet length does not exceed 250 bytes.

---

## Features

- **Support for any data types**: Sending and receiving any data structures
- **Broadcast and unicast transmission**: Ability to send messages to all nodes or a specific recipient
- **Asynchronous processing**: Separate FreeRTOS task for sending/receiving messages
- **Statistics**: Tracking of successful and failed sends, driver errors, queue overflows
- **Power-saving mode**: Disabling message reception for energy saving
- **Wi-Fi channel configuration**: Ability to specify channel for ESP-NOW communication
- **Thread safety**: All public functions are thread-safe
- **Error handling**: Comprehensive error checking with detailed logging

---

## Attention

1. For correct operation, the ESP-NOW interface must match the Wi-Fi interface (except in APSTA mode - in this case the ESP-NOW interface can be any).
2. For correct operation in ESP-NOW + STA mode, your WiFi router must be set to the same channel as ESP-NOW.
3. All devices on the network must use the same Wi-Fi channel.
4. For encrypted messages use the application layer.
5. Maximum data size depends on ESP-NOW version:
   - v1.0: up to 250 bytes
   - v2.0: up to 1490 bytes
6. For correct operation in AP+STA mode ensure both interfaces operate on the same channel.

---

## Installation

Navigate to your project's components directory:

```bash
cd ../your_project/components
```

Clone the repository:

```bash
git clone https://github.com/aZholtikov/zh_espnow
```

In your application, include the header:

```c
#include "zh_espnow.h"
```

The component will be automatically built with your project.

---

## API Reference

### ZH_ESPNOW_INIT_CONFIG_DEFAULT()

Macro for initializing component configuration with default values.

```c
zh_espnow_init_config_t config = ZH_ESPNOW_INIT_CONFIG_DEFAULT();
```

### zh_espnow_init_config_t Structure

Configuration structure for initialization:

| Field | Type | Description |
|-------|------|-------------|
| `stack_size` | `uint16_t` | Task stack size in bytes (recommended 2048) |
| `task_priority` | `uint8_t` | Task priority (recommended 5) |
| `queue_size` | `uint8_t` | Queue size (recommended 10) |
| `wifi_channel` | `uint8_t` | Wi-Fi channel (1-13) |
| `attempts` | `uint8_t` | Maximum number of send attempts (recommended 3) |
| `battery_mode` | `bool` | Power-saving mode (if true, receive is disabled) |
| `wifi_interface` | `wifi_interface_t` | Wi-Fi interface (STA or AP) |

### zh_espnow_event_type_t Structure

Event types:

| Value | Description |
|-------|-------------|
| `ZH_ESPNOW_ON_RECV_EVENT` | Message received event |
| `ZH_ESPNOW_ON_SEND_EVENT` | Send completion event |

### zh_espnow_on_send_event_type_t Structure

Send status:

| Value | Description |
|-------|-------------|
| `ZH_ESPNOW_SEND_SUCCESS` | Message successfully sent and acknowledged |
| `ZH_ESPNOW_SEND_FAIL` | Message could not be sent (all attempts failed) |

### zh_espnow_event_on_send_t Structure

Send event data:

| Field | Type | Description |
|-------|------|-------------|
| `mac_addr` | `uint8_t[ESP_NOW_ETH_ALEN]` | MAC address of the target device |
| `status` | `zh_espnow_on_send_event_type_t` | Status of the send operation |

### zh_espnow_event_on_recv_t Structure

Receive event data:

| Field | Type | Description |
|-------|------|-------------|
| `data_len` | `uint16_t` | Length of received data in bytes |
| `mac_addr` | `uint8_t[ESP_NOW_ETH_ALEN]` | MAC address of the sender |
| `data` | `uint8_t[]` | Flexible array for payload data |

### zh_espnow_stats_t Structure

Statistics structure:

| Field | Type | Description |
|-------|------|-------------|
| `sent_success` | `uint32_t` | Number of successfully sent messages |
| `sent_fail` | `uint32_t` | Number of failed sends |
| `received` | `uint32_t` | Number of received messages |
| `espnow_driver_error` | `uint32_t` | Number of ESP-NOW driver errors |
| `event_post_error` | `uint32_t` | Number of event posting failures |
| `queue_overflow_error` | `uint32_t` | Number of queue overflows |
| `min_stack_size` | `uint32_t` | Minimum free stack size of the task |

---

### zh_espnow_init()

Initializes the ESP-NOW component.

**Parameters:**

- `config` - Pointer to configuration structure. Must not be NULL.

**Returns:**

- `ESP_OK` - Success
- `ESP_ERR_INVALID_ARG` - Invalid argument (NULL config or invalid values)
- `ESP_ERR_INVALID_STATE` - Component already initialized
- `ESP_ERR_NO_MEM` - Memory allocation error
- `ESP_FAIL` - Internal initialization error

**Note:** This function initializes the ESP-NOW driver, creates internal queue and task. Wi-Fi must be already initialized and started before calling this function.

---

### zh_espnow_deinit()

Deinitializes the ESP-NOW component.

**Returns:**

- `ESP_OK` - Success
- `ESP_ERR_NOT_FOUND` - Component was not initialized
- `ESP_FAIL` - Deinitialization error

**Note:** This function is NOT thread-safe and must not be called concurrently with other API functions. The caller must ensure that all other accesses to the component have completed.

---

### zh_espnow_send()

Sends a message via ESP-NOW.

**Parameters:**

- `target` - Pointer to 6-byte MAC address. If NULL, broadcast is used.
- `data` - Pointer to data to send. Must not be NULL.
- `data_len` - Length of data in bytes. Must be > 0 and <= maximum packet size.

**Returns:**

- `ESP_OK` - Success
- `ESP_ERR_INVALID_ARG` - Invalid argument (NULL data, zero length or limit exceeded)
- `ESP_ERR_INVALID_STATE` - Component not initialized or queue almost full
- `ESP_ERR_NO_MEM` - Memory allocation error
- `ESP_FAIL` - Queue send error

**Note:** Data is copied into internal buffer. Caller does not need to keep pointer valid after function returns.

---

### zh_espnow_get_version()

Returns ESP-NOW version.

**Returns:**

- Version as 8-bit integer
- 0 on failure

---

### zh_espnow_get_stats()

Returns pointer to current statistics structure.

**Returns:**

- Constant pointer to `zh_espnow_stats_t` structure

**Note:** Pointer is valid while component is initialized.

---

### zh_espnow_reset_stats()

Resets all statistics counters to zero.

---

### zh_espnow_get_mac()

Retrieves MAC address of Wi-Fi interface used by ESP-NOW.

**Parameters:**

- `mac_addr` - Pointer to 6-byte buffer to store MAC address.

**Returns:**

- `ESP_OK` - Success
- `ESP_ERR_INVALID_ARG` - Invalid argument (NULL mac_addr)
- Other errors from esp_wifi_get_mac()

---

## Usage Examples

### Basic Example: Sending and Receiving Messages

```c
#include "nvs_flash.h"
#include "esp_netif.h"
#include "zh_espnow.h"
#include "esp_random.h"

#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]

void zh_espnow_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

uint8_t target[6] = {0xEC, 0x94, 0xCB, 0x87, 0xEC, 0xFC};

typedef struct
{
    float float_value;
    int int_value;
    char char_value[30];
    bool bool_value;
} example_message_t;

void app_main(void)
{
    esp_log_level_set("zh_espnow", ESP_LOG_ERROR);
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_init_config);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    zh_espnow_init_config_t config = ZH_ESPNOW_INIT_CONFIG_DEFAULT();
    config.task_priority = 10;
    config.stack_size = 2048;
    config.queue_size = 5;
    zh_espnow_init(&config);
    esp_event_handler_instance_register(ZH_ESPNOW, ESP_EVENT_ANY_ID, &zh_espnow_event_handler, NULL, NULL);
    example_message_t send_message = {0};
    strcpy(send_message.char_value, "THIS IS A CHAR");
    send_message.float_value = 1.234;
    send_message.bool_value = false;
    printf("ESP-NOW version %d.\n", zh_espnow_get_version());
    uint8_t node_mac[6] = {0};
    zh_espnow_get_mac(node_mac);
    printf("ESP-NOW MAC %02X:%02X:%02X:%02X:%02X:%02X.\n", MAC2STR(node_mac));
    uint8_t counter = 0;
    for (;;)
    {
        ++counter;
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
            printf("Number of espnow driver error: %ld.\n", stats->espnow_driver_error);
            printf("Number of event post error: %ld.\n", stats->event_post_error);
            printf("Number of queue overflow error: %ld.\n", stats->queue_overflow_error);
            printf("Minimum free stack size: %ld.\n", stats->min_stack_size);
        }
    }
}

void zh_espnow_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case ZH_ESPNOW_ON_RECV_EVENT:
        zh_espnow_event_on_recv_t *recv_data = event_data;
        printf("Message from MAC %02X:%02X:%02X:%02X:%02X:%02X is received. Data length %d bytes.\n", MAC2STR(recv_data->mac_addr), recv_data->data_len);
        example_message_t *recv_message = (example_message_t *)recv_data->data;
        printf("Char %s\n", recv_message->char_value);
        printf("Int %d\n", recv_message->int_value);
        printf("Float %f\n", recv_message->float_value);
        printf("Bool %d\n", recv_message->bool_value);
        break;
    case ZH_ESPNOW_ON_SEND_EVENT:
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

---

### Example: Power-Saving Mode (battery mode)

```c
#include "zh_espnow.h"

void app_main(void)
{
    esp_log_level_set("zh_espnow", ESP_LOG_ERROR);
    // Wi-Fi initialization (omitted for brevity)
    // ...

    // Initialize in power-saving mode (receive disabled)
    zh_espnow_init_config_t config = ZH_ESPNOW_INIT_CONFIG_DEFAULT();
    config.battery_mode = true;
    config.attempts = 3;
    zh_espnow_init(&config);

    // Send messages (receive is not working)
    uint8_t message[] = "Hello from battery mode";
    zh_espnow_send(NULL, message, sizeof(message));

    // ...
    zh_espnow_deinit();
}
```

---

### Example: Access Point (AP) Mode

```c
#include "zh_espnow.h"

void app_main(void)
{
    esp_log_level_set("zh_espnow", ESP_LOG_ERROR);
    // Initialize Wi-Fi in AP mode
    esp_wifi_set_mode(WIFI_MODE_AP);
    // ...
    esp_wifi_start();

    // Initialize ESP-NOW with AP interface
    zh_espnow_init_config_t config = ZH_ESPNOW_INIT_CONFIG_DEFAULT();
    config.wifi_interface = WIFI_IF_AP;
    config.wifi_channel = 6;
    zh_espnow_init(&config);

    // ...
    zh_espnow_deinit();
}
```

---

## Technical Specifications

| Parameter | Value |
|-----------|-------|
| **Maximum packet size** | 250 bytes (v1.0) / 1490 bytes (v2.0) |
| **Memory management** | heap_caps_calloc, heap_caps_free |
| **Memory caps** | MALLOC_CAP_8BIT |
| **Thread safety** | Thread-safe (uses FreeRTOS queue and task) |
| **ESP-IDF version** | >= 5.0 |
| **Platform** | ESP32 series |
| **Language** | C (C99) |

---

## Error Codes

| Error Code | Description |
|------------|-------------|
| `ESP_OK` | Operation successful |
| `ESP_ERR_INVALID_ARG` | Invalid argument (NULL pointer or invalid values) |
| `ESP_ERR_INVALID_STATE` | Component already initialized or not initialized |
| `ESP_ERR_NO_MEM` | Memory allocation error (out of memory) |
| `ESP_FAIL` | General error (driver, queue, events) |
| `ESP_ERR_NOT_FOUND` | Component was not initialized |

---

## Contributing

Contributions are welcome! To contribute:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

Please ensure your code follows the existing style and includes appropriate documentation.

---

## License

This project is licensed under the Apache License, Version 2.0 - see the [LICENSE](LICENSE) file for details.

### Apache License, Version 2.0

Copyright (c) 2026 Alexey Zholtikov

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

---

## Additional Notes

- **Performance**: Component uses FreeRTOS queue to decouple ISRs/callbacks from application logic
- **Best Practices**:
  - Always initialize Wi-Fi before component initialization
  - Check send status in event handler
  - Use power-saving mode for battery-powered devices
  - Consider maximum packet size limit (250/1490 bytes)
  - For broadcast messages pass NULL as target

---

*Generated for zh_espnow v3.3.0*