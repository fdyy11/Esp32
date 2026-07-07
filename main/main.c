/**
 ******************************************************************************
 * @file        main.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2025-01-01
 * @brief       UART实验
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

// 用于存储最后接收到的UART数据（使用较小的缓冲区用于显示）
#define DISPLAY_BUF_SIZE 128
static char last_rx_data[DISPLAY_BUF_SIZE + 1] = {0};
static bool has_new_data = false;

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
    
    // 呼吸灯相关变量
    uint32_t breath_count = 0;
    uint16_t duty = 0;
    bool increasing = true;
    
    // LEDC配置结构体
    ledc_config_t ledc_config = {
        .clk_cfg = LEDC_AUTO_CLK,                 /* 自动选择时钟源 */
        .timer_num = LEDC_PWM_TIMER,              /* 使用定时器0 */
        .freq_hz = 1000,                          /* PWM频率1kHz */
        .duty_resolution = LEDC_TIMER_13_BIT,     /* 13位分辨率(0-8191) */
        .channel = LEDC_PWM_CH0_CHANNEL,          /* 通道0 */
        .duty = 0,                                /* 初始占空比0% */
        .gpio_num = LEDC_PWM_CH0_GPIO             /* GPIO1 */
    };

    ret = nvs_flash_init();         /* 初始化NVS */

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    xl9555_init();                  /* 初始化XL9555(IO扩展芯片) */
    printf("XL9555 initialized successfully\n");
    
    my_spi_init();                  /* 初始化SPI总线（SPILCD需要） */
    printf("SPI bus initialized successfully\n");
    
    ledc_init(&ledc_config);        /* 初始化LEDC(PWM) */
    
    // 测试LED是否正常工作 - 先设置为50%亮度
    printf("Testing LED PWM on GPIO1...\n");
    ledc_pwm_set_duty(&ledc_config, 50);
    vTaskDelay(pdMS_TO_TICKS(500));  // 等待500ms观察LED
    
    uart0_init(115200);             /* 初始化串口0 */
    spilcd_init();                  /* 初始化SPILCD */
    printf("SPILCD initialized successfully\n");
    printf("LCD size: %dx%d\n", spilcddev.width, spilcddev.height);

    // 分配接收缓冲区（使用uart.h中定义的RX_BUF_SIZE）
    rx_buffer = (char *)malloc(RX_BUF_SIZE + 1);
    if (rx_buffer == NULL) {
        printf("Failed to allocate RX buffer\n");
        return;
    }

    // 初始显示提示信息（白底黑字）
    spilcd_clear(WHITE);
    spilcd_show_string(10, 10, spilcddev.width - 20, 20, 16, "Last UART Data:", BLACK);
    spilcd_show_string(10, 40, spilcddev.width - 20, 20, 16, "Waiting...", BLACK);

    while(1)
    {
        // ===== 呼吸灯逻辑（一直循环执行）=====
        breath_count++;
        
        // 每10ms更新一次占空比，实现呼吸效果
        if (breath_count >= 10)  // 10 * 10ms = 100ms
        {
            if (increasing)
            {
                duty += 5;  // 每次增加5%
                if (duty >= 100)
                {
                    duty = 100;
                    increasing = false;  // 达到最亮后开始变暗
                }
            }
            else
            {
                if (duty <= 5)
                {
                    duty = 0;
                    increasing = true;   // 达到最暗后开始变亮
                }
                else
                {
                    duty -= 5;  // 每次减少5%
                }
            }
            
            ledc_pwm_set_duty(&ledc_config, duty);
            
            // 每2秒输出一次调试信息
            static uint32_t debug_count = 0;
            debug_count++;
            if (debug_count >= 20)  // 20 * 100ms = 2秒
            {
                printf("Breathing LED: duty=%d%%, direction=%s\n", 
                       duty, increasing ? "UP" : "DOWN");
                debug_count = 0;
            }
            
            breath_count = 0;
        }
        
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
                
                // 保存最后接收到的数据（限制为DISPLAY_BUF_SIZE用于LCD显示）
                strncpy(last_rx_data, rx_buffer, DISPLAY_BUF_SIZE);
                last_rx_data[DISPLAY_BUF_SIZE] = '\0';
                has_new_data = true;
                
                // 在LCD上显示接收到的数据（白底黑字）
                spilcd_clear(WHITE);
                spilcd_show_string(10, 10, spilcddev.width - 20, 20, 16, "Last UART Data:", BLACK);
                spilcd_show_string(10, 40, spilcddev.width - 20, 100, 16, last_rx_data, BLACK);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));    /* 延时10ms */
    }
    
    // 释放缓冲区（理论上不会执行到这里）
    if (rx_buffer) {
        free(rx_buffer);
    }
}
