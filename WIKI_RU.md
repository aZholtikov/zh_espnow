# zh_espnow - Компонент ESP-NOW интерфейса для ESP-IDF

## Содержание

- [Обзор](#обзор)
- [Возможности](#возможности)
- [Внимание](#внимание)
- [Установка](#установка)
- [Справочник API](#справочник-api)
- [Примеры использования](#примеры-использования)
- [Технические характеристики](#технические-характеристики)
- [Коды ошибок](#коды-ошибок)
- [Вклад в проект](#вклад-в-проект)
- [Лицензия](#лицензия)

---

## Обзор

`zh_espnow` — это компонент для ESP-IDF (Espressif IoT Development Framework), предоставляющий потокобезопасный и удобный интерфейс для работы с ESP-NOW протоколом.

Компонент разработан специально для микроконтроллеров ESP32 и предоставляет асинхронную обработку сообщений с помощью отдельного FreeRTOS задачи. Все операции отправки и получения сообщений выполняются в фоновом режиме, что позволяет основному приложению сосредоточиться на логике работы.

Компонент поддерживает широковещательную (broadcast) и одиночную (unicast) передачу данных, отслеживание статистики и режим энергосбережения (battery mode).

ESP-NOW поддерживает две версии:

- **v1.0**: ESP-IDF v5.3 и ниже — максимальный размер пакета 250 байт
- **v2.0**: ESP-IDF v5.4 и выше — максимальный размер пакета 1490 байт

Устройства v2.0 могут принимать пакеты как от v2.0, так и от v1.0 устройств. Устройства v1.0 могут принимать пакеты от других v1.0 устройств, а также пакеты от v2.0 устройств, если длина пакета не превышает 250 байт.

---

## Возможности

- **Поддержка любых типов данных**: Отправка и получение любых структур данных
- **Широковещательная и одиночная передача**: Возможность отправки сообщений всем узлам или конкретному адресату
- **Асинхронная обработка**: Отдельная FreeRTOS задача для отправки/приема сообщений
- **Статистика**: Отслеживание успешных и неуспешных отправок, ошибок драйвера, переполнений очереди
- **Режим энергосбережения**: Отключение приема сообщений для экономии энергии
- **Настройка Wi-Fi канала**: Возможность указать канал для ESP-NOW коммуникации
- **Потокобезопасность**: Все публичные функции являются потокобезопасными
- **Обработка ошибок**: Комплексная проверка ошибок с детальным логированием

---

## Внимание

1. Для корректной работы интерфейс ESP-NOW должен совпадать с интерфейсом Wi-Fi (за исключением режима APSTA — в этом случае интерфейс ESP-NOW может быть любым).
2. Для корректной работы в режиме ESP-NOW + STA ваш WiFi роутер должен быть настроен на тот же канал, что и ESP-NOW.
3. Все устройства в сети должны использовать одинаковый Wi-Fi канал.
4. Для шифрования сообщений используйте прикладной уровень (application layer).
5. Максимальный размер передаваемых данных зависит от версии ESP-NOW:
   - v1.0: до 250 байт
   - v2.0: до 1490 байт
6. Для корректной работы в режиме AP+STA убедитесь, что оба интерфейса работают на одном и том же канале.

---

## Установка

Перейдите в каталог компонентов вашего проекта:

```bash
cd ../ваш_проект/components
```

Клонируйте репозиторий:

```bash
git clone https://github.com/aZholtikov/zh_espnow
```

В вашем приложении подключите заголовочный файл:

```c
#include "zh_espnow.h"
```

Компонент будет автоматически собран вместе с вашим проектом.

---

## Справочник API

### ZH_ESPNOW_INIT_CONFIG_DEFAULT()

Макрос для инициализации конфигурации компонента значениями по умолчанию.

```c
zh_espnow_init_config_t config = ZH_ESPNOW_INIT_CONFIG_DEFAULT();
```

### Структура zh_espnow_init_config_t

Структура конфигурации инициализации:

| Поле | Тип | Описание |
|------|-----|----------|
| `stack_size` | `uint16_t` | Размер стека задачи в байтах (рекомендуется 2048) |
| `task_priority` | `uint8_t` | Приоритет задачи (рекомендуется 5) |
| `queue_size` | `uint8_t` | Размер очереди (рекомендуется 10) |
| `wifi_channel` | `uint8_t` | Wi-Fi канал (1-13) |
| `attempts` | `uint8_t` | Максимальное количество попыток отправки (рекомендуется 3) |
| `battery_mode` | `bool` | Режим энергосбережения (если true, прием отключен) |
| `wifi_interface` | `wifi_interface_t` | Wi-Fi интерфейс (STA или AP) |

### Структура zh_espnow_event_type_t

Типы событий:

| Значение | Описание |
|----------|----------|
| `ZH_ESPNOW_ON_RECV_EVENT` | Событие получения сообщения |
| `ZH_ESPNOW_ON_SEND_EVENT` | Событие завершения отправки |

### Структура zh_espnow_on_send_event_type_t

Статус отправки:

| Значение | Описание |
|----------|----------|
| `ZH_ESPNOW_SEND_SUCCESS` | Сообщение успешно отправлено и подтверждено |
| `ZH_ESPNOW_SEND_FAIL` | Сообщение не было отправлено (все попытки неудачны) |

### Структура zh_espnow_event_on_send_t

Данные события отправки:

| Поле | Тип | Описание |
|------|-----|----------|
| `mac_addr` | `uint8_t[ESP_NOW_ETH_ALEN]` | MAC-адрес целевого устройства |
| `status` | `zh_espnow_on_send_event_type_t` | Статус операции отправки |

### Структура zh_espnow_event_on_recv_t

Данные события получения:

| Поле | Тип | Описание |
|------|-----|----------|
| `data_len` | `uint16_t` | Длина полученных данных в байтах |
| `mac_addr` | `uint8_t[ESP_NOW_ETH_ALEN]` | MAC-адрес отправителя |
| `data` | `uint8_t[]` | Гибкий массив для данных payload |

### Структура zh_espnow_stats_t

Структура статистики:

| Поле | Тип | Описание |
|------|-----|----------|
| `sent_success` | `uint32_t` | Количество успешно отправленных сообщений |
| `sent_fail` | `uint32_t` | Количество неуспешных отправок |
| `received` | `uint32_t` | Количество полученных сообщений |
| `espnow_driver_error` | `uint32_t` | Количество ошибок драйвера ESP-NOW |
| `event_post_error` | `uint32_t` | Количество ошибок публикации событий |
| `queue_overflow_error` | `uint32_t` | Количество переполнений очереди |
| `min_stack_size` | `uint32_t` | Минимальный свободный размер стека задачи |

---

### zh_espnow_init()

Инициализирует компонент ESP-NOW.

**Параметры:**

- `config` - Указатель на структуру конфигурации. Не должен быть NULL.

**Возвращает:**

- `ESP_OK` - Успех
- `ESP_ERR_INVALID_ARG` - Неверный аргумент (NULL config или неверные значения)
- `ESP_ERR_INVALID_STATE` - Компонент уже инициализирован
- `ESP_ERR_NO_MEM` - Ошибка выделения памяти
- `ESP_FAIL` - Ошибка внутренней инициализации

**Примечание:** Функция инициализирует драйвер ESP-NOW, создает внутреннюю очередь и задачу. Wi-Fi должен быть уже инициализирован и запущен до вызова этой функции.

---

### zh_espnow_deinit()

Деинициализирует компонент ESP-NOW.

**Возвращает:**

- `ESP_OK` - Успех
- `ESP_ERR_NOT_FOUND` - Компонент не был инициализирован
- `ESP_FAIL` - Ошибка деинициализации

**Примечание:** Эта функция НЕ является потокобезопасной и не должна вызываться одновременно с другими функциями API. Вызывающий должен убедиться, что все другие обращения к компоненту завершены.

---

### zh_espnow_send()

Отправляет сообщение через ESP-NOW.

**Параметры:**

- `target` - Указатель на 6-байтный MAC-адрес. Если NULL, используется broadcast.
- `data` - Указатель на данные для отправки. Не должен быть NULL.
- `data_len` - Длина данных в байтах. Должен быть > 0 и <= максимального размера пакета.

**Возвращает:**

- `ESP_OK` - Успех
- `ESP_ERR_INVALID_ARG` - Неверный аргумент (NULL data, нулевая длина или превышение лимита)
- `ESP_ERR_INVALID_STATE` - Компонент не инициализирован или очередь почти полная
- `ESP_ERR_NO_MEM` - Ошибка выделения памяти
- `ESP_FAIL` - Ошибка отправки в очередь

**Примечание:** Данные копируются во внутренний буфер. Вызывающий не обязан сохранять указатель после возврата из функции.

---

### zh_espnow_get_version()

Возвращает версию ESP-NOW.

**Возвращает:**

- Версия в виде 8-битного целого числа
- 0 в случае ошибки

---

### zh_espnow_get_stats()

Возвращает указатель на текущую структуру статистики.

**Возвращает:**

- Константный указатель на структуру `zh_espnow_stats_t`

**Примечание:** Указатель действителен пока компонент инициализирован.

---

### zh_espnow_reset_stats()

Сбрасывает все счетчики статистики в ноль.

---

### zh_espnow_get_mac()

Получает MAC-адрес Wi-Fi интерфейса, используемого для ESP-NOW.

**Параметры:**

- `mac_addr` - Указатель на 6-байтный буфер для сохранения MAC-адреса.

**Возвращает:**

- `ESP_OK` - Успех
- `ESP_ERR_INVALID_ARG` - Неверный аргумент (NULL mac_addr)
- Другие ошибки от esp_wifi_get_mac()

---

## Примеры использования

### Базовый пример: Отправка и получение сообщений

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

### Пример: Режим энергосбережения (battery mode)

```c
#include "zh_espnow.h"

void app_main(void)
{
    esp_log_level_set("zh_espnow", ESP_LOG_ERROR);
    // Инициализация Wi-Fi (пропущена для краткости)
    // ...

    // Инициализация в режиме энергосбережения (прием отключен)
    zh_espnow_init_config_t config = ZH_ESPNOW_INIT_CONFIG_DEFAULT();
    config.battery_mode = true;
    config.attempts = 3;
    zh_espnow_init(&config);

    // Отправка сообщений (прием не работает)
    uint8_t message[] = "Hello from battery mode";
    zh_espnow_send(NULL, message, sizeof(message));

    // ...
    zh_espnow_deinit();
}
```

---

### Пример: Режим AP (Access Point)

```c
#include "zh_espnow.h"

void app_main(void)
{
    esp_log_level_set("zh_espnow", ESP_LOG_ERROR);
    // Инициализация Wi-Fi в режиме AP
    esp_wifi_set_mode(WIFI_MODE_AP);
    // ...
    esp_wifi_start();

    // Инициализация ESP-NOW с интерфейсом AP
    zh_espnow_init_config_t config = ZH_ESPNOW_INIT_CONFIG_DEFAULT();
    config.wifi_interface = WIFI_IF_AP;
    config.wifi_channel = 6;
    zh_espnow_init(&config);

    // ...
    zh_espnow_deinit();
}
```

---

## Технические характеристики

| Параметр | Значение |
|----------|----------|
| **Максимальный размер пакета** | 250 байт (v1.0) / 1490 байт (v2.0) |
| **Тип управления памятью** | heap_caps_calloc, heap_caps_free |
| **Параметры памяти** | MALLOC_CAP_8BIT |
| **Потокобезопасность** | Является потокобезопасным (использует FreeRTOS queue и task) |
| **Версия ESP-IDF** | >= 5.0 |
| **Платформа** | Семейство ESP32 |
| **Язык** | C (C99) |

---

## Коды ошибок

| Код ошибки | Описание |
|------------|----------|
| `ESP_OK` | Операция выполнена успешно |
| `ESP_ERR_INVALID_ARG` | Неверный аргумент (NULL указатель или неверные значения) |
| `ESP_ERR_INVALID_STATE` | Компонент уже инициализирован или н�� инициализирован |
| `ESP_ERR_NO_MEM` | Ошибка выделения памяти (не хватает памяти) |
| `ESP_FAIL` | Общая ошибка (драйвер, очередь, события) |
| `ESP_ERR_NOT_FOUND` | Компонент не был инициализирован |

---

## Вклад в проект

Вклад приветствуется! Чтобы внести свой вклад:

1. Сделайте форк репозитория
2. Создайте ветку функции (`git checkout -b feature/AmazingFeature`)
3. Закоммитьте ваши изменения (`git commit -m 'Add some AmazingFeature'`)
4. Отправьте в ветку (`git push origin feature/AmazingFeature`)
5. Откройте Pull Request

Пожалуйста, убедитесь, что ваш код следует существующему стилю и включает соответствующую документацию.

---

## Лицензия

Этот проект лицензирован по лицензии Apache, версия 2.0 - см. файл [LICENSE](LICENSE) для подробной информации.

### Apache License, Version 2.0

Авторское право (c) 2026 Алексей Жолтиков

Лицензировано по лицензии Apache License, Version 2.0 (далее — "Лицензия");
вы не можете использовать этот файл, кроме случаев, предусмотренных Лицензией.
Копию Лицензии можно получить по адресу:

    http://www.apache.org/licenses/LICENSE-2.0

Если иное не требуется действующим законодательством или не согласовано в письменном виде,
программное обеспечение, распространяемое по Лицензии, распространяется на условиях "КАК ЕСТЬ",
БЕЗ КАКИХ-ЛИБО ГАРАНТИЙ, явных или подразумеваемых, включая, но не ограничиваясь, гарантии
ТОВАРНОГО СОСТОЯНИЯ, ПРИГОДНОСТИ ДЛЯ КОНКРЕТНОЙ ЦЕЛИ И НЕНАРУШЕНИЯ ПРАВ.
Смотрите Лицензию для получения конкретных прав и ограничений.

---

## Дополнительные заметки

- **Производительность**: Компонент использует FreeRTOS queue для декуплирования ISRs/callbacks от логики приложения
- **Лучшие практики**:
  - Всегда инициализируйте Wi-Fi перед инициализацией компонента
  - Проверяйте статус отправки в обработчике событий
  - Используйте режим энергосбережения для батарейных устройств
  - Учитывайте ограничение максимального размера пакета (250/1490 байт)
  - Для отправки широковещательных сообщений передавайте NULL в качестве адресата

---

*Сгенерировано для zh_espnow v3.3.0*
