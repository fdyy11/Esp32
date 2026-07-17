/**
 ******************************************************************************
 * @file        wifi_client.c
 * @brief       WiFi TCP客户端模块实现文件
 * @details     实现WiFi STA模式连接和TCP客户端功能
 ******************************************************************************
 */

#include "wifi_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>

static const char *TAG = "WIFI_CLIENT";

/* WiFi事件组 */
static EventGroupHandle_t s_wifi_event_group;

/* WiFi事件位定义 */
#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_FAIL_BIT           BIT1
#define TCP_CONNECTED_BIT       BIT2

/* TCP Socket */
static int s_tcp_socket = -1;

/* 事件回调函数 */
static wifi_event_callback_t s_event_callback = NULL;

/* WiFi连接状态 */
static bool s_wifi_connected = false;
static bool s_tcp_connected = false;

/* WiFi重连控制 */
static uint32_t s_reconnect_count = 0;
static const uint32_t MAX_RECONNECT_ATTEMPTS = 10;  // 最大重连次数
static TickType_t s_last_disconnect_time = 0;
static const TickType_t RECONNECT_BACKOFF_MS = pdMS_TO_TICKS(5000);  // 重连退避时间5秒

/**
 * @brief       WiFi事件处理函数
 */
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi STA started, connecting to AP...");
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected from AP");
        s_wifi_connected = false;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        
        if (s_event_callback) {
            s_event_callback(WIFI_EVENT_DISCONNECTED, NULL);
        }
        
        // 如果TCP已连接，关闭socket
        if (s_tcp_socket >= 0) {
            close(s_tcp_socket);
            s_tcp_socket = -1;
            s_tcp_connected = false;
            xEventGroupClearBits(s_wifi_event_group, TCP_CONNECTED_BIT);
            
            if (s_event_callback) {
                s_event_callback(TCP_EVENT_DISCONNECTED, NULL);
            }
        }
        
        // 限制重连次数和频率，避免资源耗尽
        TickType_t current_time = xTaskGetTickCount();
        if (s_reconnect_count < MAX_RECONNECT_ATTEMPTS && 
            (current_time - s_last_disconnect_time) > RECONNECT_BACKOFF_MS) {
            
            s_reconnect_count++;
            s_last_disconnect_time = current_time;
            
            // 尝试重新连接（仅在WiFi已初始化时）
            wifi_mode_t mode;
            esp_wifi_get_mode(&mode);
            if (mode == WIFI_MODE_STA) {
                esp_wifi_connect();
                ESP_LOGI(TAG, "Retrying to connect... (%lu/%lu)", s_reconnect_count, MAX_RECONNECT_ATTEMPTS);
            }
        } else {
            ESP_LOGW(TAG, "WiFi reconnect limit reached or backoff period active. Stopping reconnect attempts.");
            ESP_LOGW(TAG, "System will continue without WiFi. Use KEY buttons for local control.");
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        
        // 重置重连计数器
        s_reconnect_count = 0;
        
        if (s_event_callback) {
            s_event_callback(WIFI_EVENT_GOT_IP, &event->ip_info.ip);
        }
    }
}

/**
 * @brief       初始化WiFi
 */
static esp_err_t wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialization completed");
    ESP_LOGI(TAG, "Connecting to SSID: %s", WIFI_SSID);

    return ESP_OK;
}

/**
 * @brief       连接到TCP服务器
 */
static esp_err_t tcp_client_connect(void)
{
    struct sockaddr_in server_addr;
    int err;

    // 如果已经连接，先关闭
    if (s_tcp_socket >= 0) {
        close(s_tcp_socket);
        s_tcp_socket = -1;
    }

    // 创建socket
    s_tcp_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_tcp_socket < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        return ESP_FAIL;
    }

    // 设置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(TCP_SERVER_PORT);
    inet_pton(AF_INET, TCP_SERVER_IP, &server_addr.sin_addr.s_addr);

    ESP_LOGI(TAG, "Connecting to TCP server %s:%d...", TCP_SERVER_IP, TCP_SERVER_PORT);

    // 连接到服务器
    err = connect(s_tcp_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
        close(s_tcp_socket);
        s_tcp_socket = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Successfully connected to TCP server");
    s_tcp_connected = true;
    xEventGroupSetBits(s_wifi_event_group, TCP_CONNECTED_BIT);
    
    if (s_event_callback) {
        s_event_callback(TCP_EVENT_CONNECTED, NULL);
    }

    return ESP_OK;
}

esp_err_t wifi_tcp_client_init(wifi_event_callback_t callback)
{
    s_event_callback = callback;
    
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化WiFi
    ESP_ERROR_CHECK(wifi_init_sta());

    ESP_LOGI(TAG, "WiFi TCP client initialized");
    return ESP_OK;
}

esp_err_t wifi_tcp_client_start(void)
{
    // 等待WiFi连接（设置超时时间为15秒，避免永久阻塞）
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(15000));  // 15秒超时

    if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
        return ESP_FAIL;
    }

    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "WiFi connection timeout after 15 seconds");
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "WiFi connected successfully");

    // 尝试连接TCP服务器（带重试机制，最多重试3次）
    int retry_count = 0;
    const int max_retries = 3;
    
    while (retry_count < max_retries) {
        if (tcp_client_connect() == ESP_OK) {
            break;
        }
        
        retry_count++;
        ESP_LOGW(TAG, "TCP connection failed, retry %d/%d...", retry_count, max_retries);
        vTaskDelay(pdMS_TO_TICKS(1000)); // 等待1秒后重试
    }

    if (retry_count >= max_retries) {
        ESP_LOGW(TAG, "Failed to connect to TCP server after %d retries, continuing without TCP", max_retries);
        // 不返回错误，允许系统继续运行（仅WiFi模式）
        return ESP_OK;
    }

    return ESP_OK;
}

esp_err_t wifi_tcp_client_stop(void)
{
    if (s_tcp_socket >= 0) {
        close(s_tcp_socket);
        s_tcp_socket = -1;
        s_tcp_connected = false;
    }
    
    esp_wifi_stop();
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | TCP_CONNECTED_BIT);
    
    ESP_LOGI(TAG, "WiFi TCP client stopped");
    return ESP_OK;
}

int wifi_tcp_send(const uint8_t *data, size_t len)
{
    if (!s_tcp_connected || s_tcp_socket < 0) {
        ESP_LOGW(TAG, "TCP not connected");
        return -1;
    }

    if (data == NULL || len == 0) {
        return 0;
    }

    // 设置发送超时时间（30ms），避免永久阻塞
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 30000;  // 30毫秒（从50ms减少到30ms）
    setsockopt(s_tcp_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    // 使用非阻塞方式发送
    int sent_bytes = send(s_tcp_socket, data, len, MSG_DONTWAIT);
    
    if (sent_bytes < 0) {
        // 区分超时错误和真正的错误
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ESP_LOGW(TAG, "Send timeout (non-fatal), errno=%d", errno);
            return 0;  // 超时不算致命错误
        }
        
        ESP_LOGE(TAG, "Error occurred during sending: errno %d (%s)", errno, strerror(errno));
        s_tcp_connected = false;
        xEventGroupClearBits(s_wifi_event_group, TCP_CONNECTED_BIT);
        
        if (s_event_callback) {
            s_event_callback(TCP_EVENT_DISCONNECTED, NULL);
        }
        return -1;
    }

    ESP_LOGD(TAG, "Sent %d bytes to TCP server", sent_bytes);
    return sent_bytes;
}

bool wifi_is_connected(void)
{
    return s_wifi_connected;
}

bool tcp_is_connected(void)
{
    return s_tcp_connected;
}

/**
 * @brief       尝试重新连接TCP服务器
 */
esp_err_t tcp_client_reconnect(void)
{
    if (s_wifi_connected) {
        ESP_LOGI(TAG, "Attempting to reconnect TCP...");
        
        // 关闭旧的socket（如果存在）
        if (s_tcp_socket >= 0) {
            close(s_tcp_socket);
            s_tcp_socket = -1;
        }
        
        s_tcp_connected = false;
        xEventGroupClearBits(s_wifi_event_group, TCP_CONNECTED_BIT);
        
        // 尝试重新连接
        esp_err_t ret = tcp_client_connect();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "TCP reconnected successfully");
            return ESP_OK;
        } else {
            ESP_LOGW(TAG, "TCP reconnection failed");
            return ESP_FAIL;
        }
    } else {
        ESP_LOGW(TAG, "WiFi not connected, cannot reconnect TCP");
        return ESP_FAIL;
    }
}
