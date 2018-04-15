#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"

#include "driver/gpio.h"

#include "http.h"
#include "uart.h"

#define STRING2(x) #x
#define STRING(x) STRING2(x)

#if !defined( WIFI_SSID ) || !defined( WIFI_PASSWORD )
    #error WIFI_SSID or WIFI_PASSWORD not set in secrets file. See secrets.example
#endif

static const int kGpioOutputStatusRed = 16;
static const int kGpioOutputStatusGreen = 17;
static const int kGpioOutputStatusBlue = 18;

static const int kWifiConnectedBit = BIT0;
static const int kLogPostBufferBit = BIT1;

static const char* TAG = "UartLogger";
static EventGroupHandle_t app_events;
static bool connected = false;

#define log_buffer_size 1024
static char log_buffer[log_buffer_size];
static unsigned int log_buffer_read = 0;
static unsigned int log_buffer_write = 0;

void uart_data_callback(const unsigned char* data, int len)
{
    char* buffer = (char*)malloc(sizeof(int) + (len * 3) + 12);
    char* ptr = buffer;
    ptr += sprintf(buffer, "%d, ", portTICK_PERIOD_MS * xTaskGetTickCount());

    for(int i = 0; i < len; i++)
        ptr += sprintf(ptr, "%02X", data[i]); // sprintf returns characters printed including NULL byte


    const int buffer_len = strlen(buffer);
    for(int i = 0; i <= buffer_len; i++)
    {
        if(log_buffer_write >= (log_buffer_read + log_buffer_size))
        {
            ; // TODO: Wait for a read opeartion to complete
        }
        
        log_buffer[log_buffer_write++ % log_buffer_size] = buffer[i];
    }

    ESP_LOGI(TAG, "%d bytes queued", len);

    free(buffer);

    xEventGroupSetBits(app_events, kLogPostBufferBit);
}

static void http_post_task(void *pvParameters)
{
    do
    {
        if(log_buffer_read >= log_buffer_write)
            xEventGroupWaitBits(app_events, kLogPostBufferBit, true, true, portMAX_DELAY);
        
        char* buffer = (char*)malloc(log_buffer_size);

        unsigned int i;
        for(i = 0; (i + log_buffer_read) < log_buffer_write; i++)
        {
            buffer[i] = log_buffer[(i + log_buffer_read) % log_buffer_size];
            if (buffer[i] == 0) // Reached a NULL byte?
                break;
        }
        
        if (buffer[i] != 0) // premature read, wait for more data
        {
            ESP_LOGE(TAG, "Read too much from log_buffer");
            ESP_LOG_BUFFER_HEXDUMP(TAG, buffer, i, ESP_LOG_ERROR);
            free(buffer);
            xEventGroupWaitBits(app_events, kLogPostBufferBit, true, true, portMAX_DELAY);
            continue;
        }
        log_buffer_read += i + 1;

        ESP_LOGI(TAG, "Sending data via HTTP");
        http_post_data("/?token=574c9800-ac34-4e07-ae2f-391fef828c41", buffer);
        ESP_LOGI(TAG, "Sent data via HTTP");

        free(buffer);
    } while(1);
}

esp_err_t event_handler(void *ctx, system_event_t *event)
{
    int ret;
    switch ( event->event_id )
    {
        case SYSTEM_EVENT_STA_START:
            // Change the default hostname (can only be done when interface has started)
            if ( (ret = tcpip_adapter_set_hostname( TCPIP_ADAPTER_IF_STA, CONFIG_UARTLOGGER_HOSTNAME ) ) != ESP_OK )
                ESP_LOGE( TAG, "tcpip_adapter_set_hostname failed to set Hostname to \"" CONFIG_UARTLOGGER_HOSTNAME "\" with %d (0x%X)", ret, ret );
            break;
        case SYSTEM_EVENT_STA_CONNECTED:
            connected = true;
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            xEventGroupSetBits( app_events, kWifiConnectedBit );
            http_link_up();
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            xEventGroupClearBits( app_events, kWifiConnectedBit );
            http_link_down();            
            connected = false;
            break;
        default:
            break;
    }
    return ESP_OK;
}

void app_main(void)
{
    app_events = xEventGroupCreate();

    memset(log_buffer, 0, sizeof(log_buffer) / sizeof(log_buffer[0]));

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    tcpip_adapter_init();

    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_FLASH) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    // Enable minimum power saving mode
    ESP_ERROR_CHECK( esp_wifi_set_ps(WIFI_PS_MODEM) );

    wifi_config_t sta_config = {
        .sta = {
            .ssid = STRING( WIFI_SSID ),
            .password = STRING( WIFI_PASSWORD ),
            .bssid_set = false
        }
    };
    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &sta_config) );

    ESP_ERROR_CHECK( esp_wifi_start() );
    ESP_ERROR_CHECK( esp_wifi_connect() );

    http_init();
    uart_init(uart_data_callback);

    xTaskCreate(&http_post_task, "http_post_task", 4096, NULL, 5, NULL);

    // Now on to main application stuff
    gpio_set_direction(kGpioOutputStatusBlue, GPIO_MODE_OUTPUT_OD);
    gpio_set_direction(kGpioOutputStatusRed, GPIO_MODE_OUTPUT_OD);
    gpio_set_direction(kGpioOutputStatusGreen, GPIO_MODE_OUTPUT_OD);
    
    gpio_set_level(kGpioOutputStatusGreen, 1);

    int level = 0;
    while (true) {
        gpio_set_level(kGpioOutputStatusBlue, level);
        gpio_set_level(kGpioOutputStatusRed, level);
        level = !level;

        if ( connected )
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        else
            vTaskDelay(250 / portTICK_PERIOD_MS);
    }
}

