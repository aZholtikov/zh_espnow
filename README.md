# ESP32 ESP-IDF component for ESP-NOW interface

## Wiki

[EN](WIKI_EN.md) | [RU](WIKI_RU.md)

## Tested on

1. [ESP32 ESP-IDF v6.0.0](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/index.html)

## SAST Tools

[PVS-Studio](https://pvs-studio.com/pvs-studio/?utm_source=website&utm_medium=github&utm_campaign=open_source) - static analyzer for C, C++, C#, and Java code.

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
5. ESP-NOW supports two versions: v1.0 (ESP-IDF v5.3 and below) and v2.0 (ESP-IDF v5.4 and highter). The maximum packet length supported by v2.0 devices is 1490 bytes while the maximum packet length supported by v1.0 devices is 250 bytes. The v2.0 devices are capable of receiving packets from both v2.0 and v1.0 devices. In contrast v1.0 devices can only receive packets from other v1.0 devices. However v1.0 devices can receive v2.0 packets if the packet length is less than or equal to 250 bytes. For packets exceeding this length the v1.0 devices will either truncate the data to the first 250 bytes or discard the packet entirely.

## Using

In an existing project, run the following command to install the component:

```text
cd ../your_project/components
git clone https://github.com/aZholtikov/zh_espnow
```

In the application, add the component:

```c
#include "zh_espnow.h"
```

## Examples

See Wiki [EN](WIKI_EN.md#usage-examples) | [RU](WIKI_RU.md#примеры-использования)
