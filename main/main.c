/**
 ******************************************************************************
 * @file        main.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2025-01-01
 * @brief       UART实验（增加WiFi TCP客户端功能 + 继电器控制）
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ******************************************************************************
 * @attention
 * 
 * 实验平台:正点原子 ESP32-S3 开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 ******************************************************************************
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>
#include "ledc.h"
#include "uart.h"
#include "spilcd.h"
#include "xl9555.h"
#include "my_spi.h"
#include "wifi_client.h"
#include "relay.h"
#include "led.h"

// 用于存储最后接收到的UART数据（使用较小的缓冲区用于显示）
#define DISPLAY_BUF_SIZE 128
static char last_rx_data[DISPLAY_BUF_SIZE + 1] = {0};
static bool has_new_data = false;

/**
 * @brief       WiFi事件回调函数
 * @param       event: WiFi事件类型
 * @param       data: 事件数据
 * @retval      无
 */
static void wifi_event_callback(wifi_client_event_t event, void *data)
{
    switch (event) {
        case WIFI_EVENT_DISCONNECTED:
            printf("[WiFi] Disconnected from AP\n");
            break;
        case WIFI_EVENT_CONNECTED:
            printf("[WiFi] Connected to AP\n");
            break;
        case WIFI_EVENT_GOT_IP:
            printf("[WiFi] Got IP address\n");
            break;
        case TCP_EVENT_CONNECTED:
            printf("[TCP] Connected to server\n");
            // 在LCD上显示连接状态
            spilcd_clear(WHITE);
            spilcd_show_string(10, 10, spilcddev.width - 20, 20, 16, "WiFi+TCP OK", BLACK);
            spilcd_show_string(10, 40, spilcddev.width - 20, 20, 16, "Ready...", BLACK);
            break;
        case TCP_EVENT_DISCONNECTED:
            printf("[TCP] Disconnected from server\n");
            break;
        case TCP_EVENT_DATA_RECEIVED:
            printf("[TCP] Data received\n");
            break;
        default:
            break;
    }
}

/**
 * @brief       程序入口
 * @param       无
 * @retval      无
 */
void app_main(void)
{
    esp_err_t ret;
    uint16_t len = 0;
    char *rx_buffer = NULL;
    
    // LEDC配置结构体（仅用于初始化后停止，LED改为指示灯模式）
    ledc_config_t ledc_config = {
        .clk_cfg = LEDC_AUTO_CLK,                 /* 自动选择时钟源 */
        .timer_num = LEDC_PWM_TIMER,              /* 使用定时器0 */
        .freq_hz = 1000,                          /* PWM频率1kHz */
        .duty_resolution = LEDC_TIMER_13_BIT,     /* 13位分辨率(0-8191) */
        .channel = LEDC_PWM_CH0_CHANNEL,          /* 通道0 */
        .duty = 0,                                /* 初始占空比0% */
        .gpio_num = LEDC_PWM_CH0_GPIO             /* GPIO1 */
    };

    printf("=== System Starting ===\n");

    // 初始化WiFi TCP客户端（包含NVS初始化）
    printf("Initializing WiFi TCP client...\n");
    wifi_tcp_client_init(wifi_event_callback);
    printf("WiFi TCP client initialized\n");

    xl9555_init();                  /* 初始化XL9555(IO扩展芯片) */
    printf("XL9555 initialized successfully\n");
    
    my_spi_init();                  /* 初始化SPI总线（SPILCD需要） */
    printf("SPI bus initialized successfully\n");
    
    ledc_init(&ledc_config);        /* 初始化LEDC(PWM) */
    
    // 停止PWM输出，将LED改为指示灯模式（由GPIO直接控制）
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_PWM_CH0_CHANNEL, 0);
    
    /* 重新配置GPIO1为标准GPIO输出（LEDC停止后需要重新配置才能使用gpio_set_level） */
    gpio_reset_pin(LED0_GPIO_PIN);
    gpio_set_direction(LED0_GPIO_PIN, GPIO_MODE_OUTPUT);
    printf("LED PWM stopped, switching to indicator mode\n");
    
    uart0_init(115200);             /* 初始化串口0 */
    spilcd_init();                  /* 初始化SPILCD */
    printf("SPILCD initialized successfully\n");
    printf("LCD size: %dx%d\n", spilcddev.width, spilcddev.height);

    // 初始化继电器控制模块
    relay_init();                   /* 初始化继电器(IO16/IO18) */
    printf("Relay initialized: IO16=RELAY1(FWD), IO18=RELAY2(REV)\n");

    // 启动WiFi和TCP连接（这会阻塞直到连接成功或失败）
    printf("Starting WiFi connection...\n");
    spilcd_clear(WHITE);
    spilcd_show_string(10, 10, spilcddev.width - 20, 20, 16, "Connecting...", BLACK);
    spilcd_show_string(10, 40, spilcddev.width - 20, 20, 16, "WiFi...", BLACK);
    
    ret = wifi_tcp_client_start();
    if (ret != ESP_OK) {
        printf("Failed to start WiFi TCP client\n");
        spilcd_show_string(10, 70, spilcddev.width - 20, 20, 16, "WiFi Failed!", BLACK);
    } else {
        printf("WiFi TCP client started successfully\n");
    }

    // 分配接收缓冲区（使用uart.h中定义的RX_BUF_SIZE）
    rx_buffer = (char *)malloc(RX_BUF_SIZE + 1);
    if (rx_buffer == NULL) {
        printf("Failed to allocate RX buffer\n");
        return;
    }

    // 初始化LCD显示
    spilcd_clear(WHITE);
    spilcd_show_string(10, 10, spilcddev.width - 20, 20, 16, "System Ready", BLACK);
    spilcd_show_string(10, 40, spilcddev.width - 20, 20, 16, "Relay Ctrl Active", BLACK);

    while(1)
    {
        // ===== 继电器定时切换逻辑（每8秒反转一次IO输出）=====
        if (relay_task_handler())
        {
            // 发生了切换事件，向TCP服务端发送完成周期通知
            if (tcp_is_connected()) {
                char tcp_msg[64];
                int msg_len = snprintf(tcp_msg, sizeof(tcp_msg), 
                                       "已完成%ld次收展", 
                                       relay_get_cycle_count());
                
                int sent = wifi_tcp_send((uint8_t *)tcp_msg, msg_len);
                if (sent > 0) {
                    printf("[TCP] Sent cycle notification: %s\n", tcp_msg);
                } else {
                    printf("[TCP] Failed to send cycle notification\n");
                }
            } else {
                printf("[TCP] Not connected, cannot send cycle notification\n");
            }
            
            // 更新LCD显示当前状态
            spilcd_clear(WHITE);
            if (relay_get_state()) {
                spilcd_show_string(10, 10, spilcddev.width - 20, 20, 16, "Motor: FORWARD", BLACK);
            } else {
                spilcd_show_string(10, 10, spilcddev.width - 20, 20, 16, "Motor: REVERSE", BLACK);
            }
            char cycle_str[32];
            snprintf(cycle_str, sizeof(cycle_str), "Cycles: %ld", relay_get_cycle_count());
            spilcd_show_string(10, 40, spilcddev.width - 20, 20, 16, cycle_str, BLACK);
        }
        
        // ===== LED指示灯逻辑（与IO16输出同步）=====
        // IO16输出高电平时LED亮，低电平时LED灭
        gpio_set_level(LED0_GPIO_PIN, relay_get_state() ? 1 : 0);
        
        // ===== UART数据接收逻辑 =====
        // 获取缓冲区中的数据长度
        uart_get_buffered_data_len(USART_UX, (size_t*) &len);

        if (len > 0)
        {
            // 有数据接收，读取数据
            if (len > RX_BUF_SIZE) {
                len = RX_BUF_SIZE;
            }
            
            int bytes_read = uart_read_bytes(USART_UX, rx_buffer, len, 100 / portTICK_PERIOD_MS);
            
            if (bytes_read > 0)
            {
                rx_buffer[bytes_read] = '\0';
                
                // 回显接收到的数据（只在接收到数据时才回复）
                uart_write_bytes(USART_UX, rx_buffer, bytes_read);
                
                // 通过WiFi TCP发送数据到服务器
                if (tcp_is_connected()) {
                    int sent = wifi_tcp_send((uint8_t *)rx_buffer, bytes_read);
                    if (sent > 0) {
                        printf("[TCP] Sent %d bytes to server\n", sent);
                    } else {
                        printf("[TCP] Failed to send data\n");
                    }
                } else {
                    printf("[TCP] Not connected, cannot send data\n");
                }
                
                // 保存最后接收到的数据（限制为DISPLAY_BUF_SIZE用于LCD显示）
                strncpy(last_rx_data, rx_buffer, DISPLAY_BUF_SIZE);
                last_rx_data[DISPLAY_BUF_SIZE] = '\0';
                has_new_data = true;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));    /* 延时10ms */
    }
    
    // 释放缓冲区（理论上不会执行到这里）
    if (rx_buffer) {
        free(rx_buffer);
    }
}
