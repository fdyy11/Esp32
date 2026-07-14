/**
 ******************************************************************************
 * @file        wifi_client.h
 * @brief       WiFi TCP客户端模块头文件
 * @details     提供WiFi STA模式连接和TCP客户端功能
 ******************************************************************************
 */

#ifndef __WIFI_CLIENT_H__
#define __WIFI_CLIENT_H__

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* WiFi配置参数 */
#define WIFI_SSID           "CCZU2"      // WiFi名称，请修改为你的WiFi名称
#define WIFI_PASSWORD       "yw521hnmgx"   // WiFi密码，请修改为你的WiFi密码
#define WIFI_MAXIMUM_RETRY  5                    // 最大重连次数

/* TCP服务器配置参数 */
#define TCP_SERVER_IP       "192.168.82.9"     // TCP服务器IP地址，请修改为你的服务器IP
#define TCP_SERVER_PORT     7070                 // TCP服务器端口号

/* WiFi事件定义 */
typedef enum {
    WIFI_EVENT_DISCONNECTED = 0,
    WIFI_EVENT_CONNECTED,
    WIFI_EVENT_GOT_IP,
    TCP_EVENT_CONNECTED,
    TCP_EVENT_DISCONNECTED,
    TCP_EVENT_DATA_RECEIVED
} wifi_client_event_t;

/* 回调函数类型定义 */
typedef void (*wifi_event_callback_t)(wifi_client_event_t event, void *data);

/**
 * @brief       初始化WiFi TCP客户端
 * @param       callback: 事件回调函数（可选，传NULL表示不使用回调）
 * @retval      ESP_OK: 成功, 其他: 失败
 */
esp_err_t wifi_tcp_client_init(wifi_event_callback_t callback);

/**
 * @brief       启动WiFi连接
 * @retval      ESP_OK: 成功, 其他: 失败
 */
esp_err_t wifi_tcp_client_start(void);

/**
 * @brief       停止WiFi连接
 * @retval      ESP_OK: 成功, 其他: 失败
 */
esp_err_t wifi_tcp_client_stop(void);

/**
 * @brief       通过TCP发送数据
 * @param       data: 要发送的数据指针
 * @param       len: 数据长度
 * @retval      实际发送的字节数, -1: 失败
 */
int wifi_tcp_send(const uint8_t *data, size_t len);

/**
 * @brief       检查WiFi是否已连接
 * @retval      true: 已连接, false: 未连接
 */
bool wifi_is_connected(void);

/**
 * @brief       检查TCP是否已连接
 * @retval      true: 已连接, false: 未连接
 */
bool tcp_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* __WIFI_CLIENT_H__ */
