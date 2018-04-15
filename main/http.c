#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_event_loop.h"
#include "esp_log.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "sdkconfig.h"

#include "http.h"


#ifndef ESP_IDF_VER
#define ESP_IDF_VER "v1.0"
#endif /* ESP_IDF_VER */

#define CONNECTED_BIT BIT0
#define IP_RESOLVE_REQUESTED_BIT BIT1
#define IP_RESOLVE_COMPLETED_BIT BIT2

static const char *TAG = "HTTP Client";
static EventGroupHandle_t http_events;

//POST 
static const char *kHttpBaseHeader = "HTTP/1.0\r\n"
    "Host: "CONFIG_LOGGER_HOST":"CONFIG_LOGGER_PORT"\r\n"
    "User-Agent: esp-idf/"ESP_IDF_VER" esp32\r\n";

static struct sockaddr resolved_address = {0};

void http_link_up(void)
{
    xEventGroupSetBits(http_events, CONNECTED_BIT);
}

void http_link_down(void)
{
    xEventGroupClearBits(http_events, CONNECTED_BIT);
}

static void resolve_ip_task(void *pvParameters)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    int err;

    while(1) {
        // Wait for the callback to set the CONNECTED_BIT in the event group.
        xEventGroupWaitBits(http_events, CONNECTED_BIT | IP_RESOLVE_REQUESTED_BIT, false, true, portMAX_DELAY);

        err = getaddrinfo(CONFIG_LOGGER_HOST, CONFIG_LOGGER_PORT, &hints, &res);

        if(err != 0 || res == NULL) {
            ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        memcpy(&resolved_address, res->ai_addr, sizeof(struct sockaddr));
        freeaddrinfo(res);

        // Print the resolved IP.
        // Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code
        if (resolved_address.sa_family == AF_INET)
        {
            struct in_addr *addr = &((struct sockaddr_in *)&resolved_address)->sin_addr;
            ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));
        }
        else if (resolved_address.sa_family == AF_INET6)
        {
            struct in6_addr *addr = &((struct sockaddr_in6 *)&resolved_address)->sin6_addr;
            ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet6_ntoa(*addr));
        }

        xEventGroupClearBits(http_events, IP_RESOLVE_REQUESTED_BIT);
        xEventGroupSetBits(http_events, IP_RESOLVE_COMPLETED_BIT);
        // End of task
    }
}

void http_post_data(const char* url, const char* data)
{
    if (resolved_address.sa_family == AF_UNSPEC)
    {
        ESP_LOGE(TAG, "IP Address not resovled yet.");
        xEventGroupSetBits(http_events, IP_RESOLVE_REQUESTED_BIT);
        return;
    }

    int s = socket(resolved_address.sa_family, SOCK_STREAM, 0);
    if(s < 0) {
        ESP_LOGE(TAG, "Failed to allocate socket.");
        return;
    }

    if(connect(s, &resolved_address, resolved_address.sa_len) != 0) {
        ESP_LOGE(TAG, "Socket connect failed. errno=%d", errno);
        close(s);
        return;
    }

    char send_buffer[256];

    snprintf(send_buffer, 256, "POST %s %sContent-Length: %d\r\n", url, kHttpBaseHeader, strlen(data));

    if (write(s, send_buffer, strlen(send_buffer)) < 0) {
        ESP_LOGE(TAG, "Socket write header failed");
        close(s);
        return;
    }

    write(s, "\r\n", 2);

    if (write(s, data, strlen(data)) < 0) {
        ESP_LOGE(TAG, "Socket write body failed");
        close(s);
        return;
    }

    struct timeval receiving_timeout;
    receiving_timeout.tv_sec = 5;
    receiving_timeout.tv_usec = 0;
    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
            sizeof(receiving_timeout)) < 0) {
        ESP_LOGE(TAG, "Failed to set socket receiving timeout");
        close(s);
        return;
    }
    
    // Read HTTP response
    char recv_buf[64];
    int r;
    do {
        bzero(recv_buf, sizeof(recv_buf));
        r = read(s, recv_buf, sizeof(recv_buf)-1);
    } while(r > 0);

    close(s);
}

void http_init(void)
{
    http_events = xEventGroupCreate();
    // Start resolving the IP address as soon as possible
    xEventGroupSetBits(http_events, IP_RESOLVE_REQUESTED_BIT);

    xTaskCreate(&resolve_ip_task, "resolve_ip_task", 4096, NULL, 5, NULL);

}
