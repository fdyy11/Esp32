/**
 ******************************************************************************
 * @file        main.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.1 (增加看门狗自动复位功能)
 * @date        2025-01-01
 * @brief       UART实验（增加WiFi TCP客户端功能 + 继电器控制 + 看门狗保护）
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ******************************************************************************
 * @attention
 * 
 * 实验平台:正点原子 ESP32-S3 开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 * 
 * 看门狗配置说明：
 * - 必须确保 CONFIG_ESP_TASK_WDT_PANIC=n
 * - 必须确保 CONFIG_ESP_TASK_WDT_RESET_CPU=y
 * - 否则看门狗超时不会自动复位系统
 * - 运行 idf.py menuconfig 修改配置
 ******************************************************************************
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
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

// 按键状态跟踪变量
static uint8_t last_key0_state = 1;
static uint8_t last_key1_state = 1;

// LCD状态跟踪变量（用于避免不必要的刷新）
static bool lcd_initialized = false;
static bool last_relay_state = false;
static long last_cycle_count = 0;

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
    
    // 检测复位原因
    esp_reset_reason_t reset_reason = esp_reset_reason();
    uint32_t rtc_reset_cause = 0;  // 0=正常启动
    
    switch (reset_reason) {
        case ESP_RST_POWERON:
            printf("[RESET] Power-on reset - Starting fresh\n");
            rtc_reset_cause = 0;
            break;
        case ESP_RST_SW:
            printf("[RESET] Software reset - Starting fresh\n");
            rtc_reset_cause = 0;
            break;
        case ESP_RST_PANIC:
            printf("[RESET] Panic reset - May be watchdog timeout\n");
            rtc_reset_cause = 1;  // 看门狗复位
            break;
        case ESP_RST_INT_WDT:
            printf("[RESET] Interrupt watchdog reset\n");
            rtc_reset_cause = 1;  // 看门狗复位
            break;
        case ESP_RST_TASK_WDT:
            printf("[RESET] Task watchdog reset - Will restore state\n");
            rtc_reset_cause = 1;  // 看门狗复位
            break;
        case ESP_RST_WDT:
            printf("[RESET] Other watchdog reset\n");
            rtc_reset_cause = 1;  // 看门狗复位
            break;
        case ESP_RST_DEEPSLEEP:
            printf("[RESET] Deep sleep reset\n");
            rtc_reset_cause = 0;
            break;
        case ESP_RST_BROWNOUT:
            printf("[RESET] Brownout reset\n");
            rtc_reset_cause = 0;
            break;
        case ESP_RST_SDIO:
            printf("[RESET] SDIO reset\n");
            rtc_reset_cause = 0;
            break;
        default:
            printf("[RESET] Unknown reset reason (%d)\n", reset_reason);
            rtc_reset_cause = 0;
            break;
    }

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
    
    // 尝试从RTC内存恢复状态（仅在看门狗复位时）
    bool restored = relay_restore_from_rtc(rtc_reset_cause);
    
    if (restored) {
        printf("[RTC] Successfully restored relay state from RTC memory\n");
        
        // 更新RTC状态中的复位原因
        relay_rtc_state_t *rtc_state = relay_get_rtc_state_ptr();
        rtc_state->reset_cause = rtc_reset_cause;
        relay_save_to_rtc();
        
        // 在LCD上显示恢复信息
        spilcd_clear(WHITE);
        spilcd_show_string(10, 10, spilcddev.width - 20, 20, 16, "RESTORED", BLACK);
        char restore_str[64];
        snprintf(restore_str, sizeof(restore_str), "Cycle: %lu", relay_get_cycle_count());
        spilcd_show_string(10, 40, spilcddev.width - 20, 20, 16, restore_str, BLACK);
        vTaskDelay(pdMS_TO_TICKS(2000));  // 显示2秒
    } else {
        printf("[RTC] Starting with fresh state\n");
        
        // 清除RTC状态（非看门狗复位）
        relay_clear_rtc_state();
        
        // 在LCD上显示启动信息
        spilcd_clear(WHITE);
        spilcd_show_string(10, 10, spilcddev.width - 20, 20, 16, "System Ready", BLACK);
        spilcd_show_string(10, 40, spilcddev.width - 20, 20, 16, "Fresh Start", BLACK);
    }

    // 启动WiFi和TCP连接（这会阻塞直到连接成功或失败）
    printf("Starting WiFi connection...\n");
    if (!restored) {  // 仅在未恢复状态时显示连接中
        spilcd_show_string(10, 70, spilcddev.width - 20, 20, 16, "Connecting...", BLACK);
        spilcd_show_string(10, 100, spilcddev.width - 20, 20, 16, "WiFi...", BLACK);
    }
    
    ret = wifi_tcp_client_start();
    if (ret != ESP_OK) {
        printf("Failed to start WiFi TCP client\n");
        if (!restored) {
            spilcd_show_string(10, 130, spilcddev.width - 20, 20, 16, "WiFi Failed!", BLACK);
        }
    } else {
        printf("WiFi TCP client started successfully\n");
    }

    // 分配接收缓冲区（使用uart.h中定义的RX_BUF_SIZE）
    rx_buffer = (char *)malloc(RX_BUF_SIZE + 1);
    if (rx_buffer == NULL) {
        printf("Failed to allocate RX buffer\n");
        return;
    }

    // 初始化看门狗（在所有硬件初始化完成后启用）
    printf("Initializing Task Watchdog Timer...\n");
    
    // 将当前任务添加到看门狗监控列表
    esp_err_t wdt_ret = esp_task_wdt_add(NULL);
    if (wdt_ret == ESP_OK) {
        printf("Task watchdog monitoring enabled for main task\n");
        printf("Watchdog timeout: %d seconds\n", CONFIG_ESP_TASK_WDT_TIMEOUT_S);
        
        // 检查看门狗配置是否正确
#ifdef CONFIG_ESP_TASK_WDT_PANIC
        printf("[WARNING] Watchdog is configured to PANIC instead of RESET!\n");
        printf("[WARNING] This means the system will NOT automatically reboot on timeout.\n");
        printf("[INFO] Please run 'idf.py menuconfig' and change:\n");
        printf("[INFO]   Component config -> ESP System Settings -> Task Watchdog Timer\n");
        printf("[INFO]   Disable 'Invoke panic handler on timeout'\n");
        printf("[INFO]   Enable 'Reset CPU on timeout'\n");
#else
        printf("Watchdog will reset CPU on timeout (correct configuration)\n");
#endif
    } else {
        printf("Warning: Failed to add task to watchdog: %s\n", esp_err_to_name(wdt_ret));
    }
    
    // 打印初始栈空间信息，用于诊断
    UBaseType_t stack_watermark = uxTaskGetStackHighWaterMark(NULL);
    printf("[DIAG] Initial stack watermark: %u words (%u bytes)\n", 
           stack_watermark, stack_watermark * sizeof(StackType_t));
    
    // 初始化LCD显示
    spilcd_clear(WHITE);
    spilcd_show_string(10, 10, spilcddev.width - 20, 20, 16, "System Ready", BLACK);
    spilcd_show_string(10, 40, spilcddev.width - 20, 20, 16, "Relay Ctrl Active", BLACK);

    // 继电器状态跟踪变量已在文件顶部声明为全局变量
    bool lcd_need_update = true; // 首次需要更新显示
    
    // 诊断计数器
    uint32_t loop_count = 0;
    uint32_t last_diag_loop = 0;
    
    // 关键操作耗时跟踪
    uint64_t i2c_start_time, tcp_start_time, spi_start_time;
    uint32_t max_i2c_time = 0, max_tcp_time = 0, max_spi_time = 0;
    
    // TCP重连控制
    uint32_t tcp_reconnect_attempts = 0;
    const uint32_t MAX_TCP_RECONNECT_ATTEMPTS = 5;
    uint64_t last_tcp_reconnect_time = 0;
    const uint64_t TCP_RECONNECT_BACKOFF_US = 10000000; // 10秒退避时间
    
    // 系统健康监控
    uint64_t system_start_time = esp_timer_get_time();
    uint32_t consecutive_failures = 0; // 连续失败计数
    const uint32_t MAX_CONSECUTIVE_FAILURES = 100; // 最大连续失败次数（约1秒）

    while(1)
    {
        loop_count++;
        
        // ===== 定期诊断（每1000次循环，约10秒输出一次）=====
        if (loop_count - last_diag_loop >= 1000) {
            UBaseType_t stack_watermark = uxTaskGetStackHighWaterMark(NULL);
            size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
            size_t min_free_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
            uint64_t current_time = esp_timer_get_time();
            uint64_t elapsed_seconds = (current_time - system_start_time) / 1000000;
            
            printf("[DIAG] Loop=%lu, Elapsed=%llu s, Stack=%u words (%u bytes), FreeHeap=%u, MinFreeHeap=%u\n",
                   loop_count, elapsed_seconds, stack_watermark, stack_watermark * sizeof(StackType_t), 
                   free_heap, min_free_heap);
            printf("[DIAG] Max operation times: I2C=%lu us, TCP=%lu us, SPI=%lu us\n",
                   max_i2c_time, max_tcp_time, max_spi_time);
            printf("[DIAG] TCP connected=%d, Relay cycles=%ld, Paused=%d, Failures=%lu\n",
                   tcp_is_connected(), relay_get_cycle_count(), relay_is_paused(), consecutive_failures);
            
            // 检查系统健康状态
            if (stack_watermark < 512) { // 栈空间小于2KB
                printf("[CRITICAL] Stack space critically low! %u words remaining\n", stack_watermark);
            }
            if (free_heap < 10240) { // 堆空间小于10KB
                printf("[CRITICAL] Heap space critically low! %u bytes remaining\n", free_heap);
            }
            if (consecutive_failures > MAX_CONSECUTIVE_FAILURES) {
                printf("[CRITICAL] Too many consecutive failures (%lu). Triggering system reset...\n", consecutive_failures);
                printf("[CRITICAL] System will reboot in 3 seconds...\n");
                vTaskDelay(pdMS_TO_TICKS(3000));
                esp_restart(); // 触发系统复位
            }
            
            last_diag_loop = loop_count;
        }
        
        // ===== 喂看门狗（每次循环开始就喂狗）=====
        esp_task_wdt_reset();
        
        // ===== KEY0按键检测（暂停功能）=====
        i2c_start_time = esp_timer_get_time();
        uint8_t key0_state = xl9555_pin_read(KEY0_IO);
        
        // 喂狗（I2C读取后）
        esp_task_wdt_reset();
        
        // 记录I2C操作耗时
        uint32_t i2c_time = (uint32_t)(esp_timer_get_time() - i2c_start_time);
        if (i2c_time > max_i2c_time) {
            max_i2c_time = i2c_time;
        }

        if (key0_state == 0 && last_key0_state == 1) {
            // KEY0按下（下降沿触发）
            printf("[KEY0] Pause button pressed\n");
            
            if (!relay_is_paused()) {
                // 执行暂停操作
                relay_pause();
                
                // 保存状态到RTC内存
                relay_save_to_rtc();
                
                // 更新LCD显示
                spilcd_clear(WHITE);
                spilcd_show_string(10, 10, spilcddev.width - 20, 20, 16, "PAUSED", BLACK);
                char cycle_str[32];
                snprintf(cycle_str, sizeof(cycle_str), "Cycles: %ld", relay_get_cycle_count());
                spilcd_show_string(10, 40, spilcddev.width - 20, 20, 16, cycle_str, BLACK);
                
                // 通过TCP发送暂停通知（带超时保护和非阻塞检查）
                if (tcp_is_connected()) {
                    const char *pause_msg = "RELAY_PAUSED";
                    tcp_start_time = esp_timer_get_time();
                    int sent = wifi_tcp_send((uint8_t *)pause_msg, strlen(pause_msg));
                    
                    // 记录TCP操作耗时
                    uint32_t tcp_time = (uint32_t)(esp_timer_get_time() - tcp_start_time);
                    if (tcp_time > max_tcp_time) {
                        max_tcp_time = tcp_time;
                    }
                    
                    // 喂狗（TCP发送后）
                    esp_task_wdt_reset();
                    
                    if (sent <= 0) {
                        printf("[TCP] Failed to send pause notification (errno=%d, time=%lu us)\n", errno, tcp_time);
                        // 标记TCP需要重连
                        tcp_reconnect_attempts = 0;
                        last_tcp_reconnect_time = esp_timer_get_time();
                    }
                }

                // LED熄灭
                gpio_set_level(LED0_GPIO_PIN, 0);
                
                lcd_need_update = false; // 暂停状态下不需要再更新
            }
        }
        last_key0_state = key0_state;
        
        // ===== KEY1按键检测（恢复功能）=====
        i2c_start_time = esp_timer_get_time();
        uint8_t key1_state = xl9555_pin_read(KEY1_IO);
        
        // 喂狗（I2C读取后）
        esp_task_wdt_reset();
        
        // 记录I2C操作耗时
        i2c_time = (uint32_t)(esp_timer_get_time() - i2c_start_time);
        if (i2c_time > max_i2c_time) {
            max_i2c_time = i2c_time;
        }

        if (key1_state == 0 && last_key1_state == 1) {
            // KEY1按下（下降沿触发）
            printf("[KEY1] Resume button pressed\n");
            
            if (relay_is_paused()) {
                // 执行恢复操作
                relay_resume();
                
                // 保存状态到RTC内存
                relay_save_to_rtc();
                
                // 标记需要更新LCD显示
                lcd_need_update = true;
                
                // 通过TCP发送恢复通知（带超时保护）
                if (tcp_is_connected()) {
                    const char *resume_msg = "RELAY_RESUMED";
                    tcp_start_time = esp_timer_get_time();
                    int sent = wifi_tcp_send((uint8_t *)resume_msg, strlen(resume_msg));
                    
                    // 记录TCP操作耗时
                    uint32_t tcp_time = (uint32_t)(esp_timer_get_time() - tcp_start_time);
                    if (tcp_time > max_tcp_time) {
                        max_tcp_time = tcp_time;
                    }
                    
                    // 喂狗（TCP发送后）
                    esp_task_wdt_reset();
                    
                    if (sent <= 0) {
                        printf("[TCP] Failed to send resume notification (errno=%d, time=%lu us)\n", errno, tcp_time);
                        tcp_reconnect_attempts = 0;
                        last_tcp_reconnect_time = esp_timer_get_time();
                    }
                }

                // LED熄灭
                gpio_set_level(LED0_GPIO_PIN, 0);
                
                lcd_need_update = false; // 暂停状态下不需要再更新
            }
        }
        last_key1_state = key1_state;
        
        // ===== 继电器定时切换逻辑（每8秒反转一次IO输出）=====
        if (relay_task_handler())
        {
            // 发生了切换事件，向TCP服务端发送完成周期通知
            if (tcp_is_connected()) {
                char tcp_msg[64];
                int msg_len = snprintf(tcp_msg, sizeof(tcp_msg), 
                                       "已完成%ld次收展", 
                                       relay_get_cycle_count());
                
                tcp_start_time = esp_timer_get_time();
                int sent = wifi_tcp_send((uint8_t *)tcp_msg, msg_len);
                
                // 记录TCP操作耗时
                uint32_t tcp_time = (uint32_t)(esp_timer_get_time() - tcp_start_time);
                if (tcp_time > max_tcp_time) {
                    max_tcp_time = tcp_time;
                }
                
                // 喂狗（TCP发送后）
                esp_task_wdt_reset();
                
                if (sent > 0) {
                    printf("[TCP] Sent cycle notification: %s\n", tcp_msg);
                } else {
                    printf("[TCP] Failed to send cycle notification (errno=%d, time=%lu us)\n", errno, tcp_time);
                }
            } else {
                printf("[TCP] Not connected, cannot send cycle notification\n");
            }
            
            // 标记需要更新LCD显示
            lcd_need_update = true;
            
            // 保存状态到RTC内存（每个周期完成后保存）
            relay_save_to_rtc();
        }
        
        // ===== 定期保存状态到RTC内存（每100次循环约1秒保存一次）=====
        if (loop_count % 100 == 0 && !relay_is_paused()) {
            relay_save_to_rtc();
        }
        
        // ===== LCD显示更新（仅在状态变化时更新，避免频繁刷新）=====
        if (lcd_need_update && !relay_is_paused()) {
            bool current_state = relay_get_state();
            long current_cycles = relay_get_cycle_count();
            
            // 检查状态是否真的发生变化
            if (current_state != last_relay_state || current_cycles != last_cycle_count) {
                spi_start_time = esp_timer_get_time();
                
                // 首次初始化或状态变化时，才需要清屏
                if (!lcd_initialized || current_state != last_relay_state) {
                    spilcd_clear(WHITE);
                    lcd_initialized = true;
                } else {
                    // 仅循环数变化时，用白色矩形覆盖旧文本区域（局部刷新）
                    // 覆盖 "Cycles: XXXX" 区域（假设最多10位数字 + 前缀）
                    spilcd_fill(10, 40, 200, 60, WHITE);
                }
                
                // 绘制电机状态（仅在状态变化时）
                if (current_state != last_relay_state) {
                    if (current_state) {
                        spilcd_show_string(10, 10, spilcddev.width - 20, 20, 16, "Motor: FORWARD", BLACK);
                    } else {
                        spilcd_show_string(10, 10, spilcddev.width - 20, 20, 16, "Motor: REVERSE", BLACK);
                    }
                }
                
                // 绘制循环次数（每次更新）
                char cycle_str[32];
                snprintf(cycle_str, sizeof(cycle_str), "Cycles: %ld", current_cycles);
                spilcd_show_string(10, 40, spilcddev.width - 20, 20, 16, cycle_str, BLACK);
                
                // 记录SPI操作耗时
                uint32_t spi_time = (uint32_t)(esp_timer_get_time() - spi_start_time);
                if (spi_time > max_spi_time) {
                    max_spi_time = spi_time;
                }
                
                // 喂狗（LCD刷新后）
                esp_task_wdt_reset();
                
                // 更新状态记录
                last_relay_state = current_state;
                last_cycle_count = current_cycles;
            }
            
            lcd_need_update = false;
        }
        
        // ===== LED指示灯逻辑（与IO16输出同步，暂停时熄灭）=====
        if (!relay_is_paused()) {
            gpio_set_level(LED0_GPIO_PIN, relay_get_state() ? 1 : 0);
        }
        
        // ===== TCP重连逻辑（仅在断开且未到最大重试次数时）=====
        if (!tcp_is_connected() && tcp_reconnect_attempts < MAX_TCP_RECONNECT_ATTEMPTS) {
            uint64_t current_time = esp_timer_get_time();
            if (current_time - last_tcp_reconnect_time >= TCP_RECONNECT_BACKOFF_US) {
                printf("[TCP] Attempting reconnection (%lu/%lu)...\n", 
                       tcp_reconnect_attempts + 1, MAX_TCP_RECONNECT_ATTEMPTS);
                
                // 尝试重新连接TCP
                if (wifi_is_connected()) {
                    esp_err_t ret = tcp_client_reconnect();
                    if (ret == ESP_OK) {
                        printf("[TCP] Reconnected successfully\n");
                        tcp_reconnect_attempts = 0; // 重置重连计数
                    } else {
                        tcp_reconnect_attempts++;
                        printf("[TCP] Reconnection failed, will retry later\n");
                    }
                    last_tcp_reconnect_time = current_time;
                } else {
                    printf("[TCP] WiFi not connected, skipping TCP reconnect\n");
                    last_tcp_reconnect_time = current_time;
                }
            }
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
                
                // 喂狗（UART操作后）
                esp_task_wdt_reset();
                
                // 通过WiFi TCP发送数据到服务器（带超时保护）
                if (tcp_is_connected()) {
                    tcp_start_time = esp_timer_get_time();
                    int sent = wifi_tcp_send((uint8_t *)rx_buffer, bytes_read);
                    
                    // 记录TCP操作耗时
                    uint32_t tcp_time = (uint32_t)(esp_timer_get_time() - tcp_start_time);
                    if (tcp_time > max_tcp_time) {
                        max_tcp_time = tcp_time;
                    }
                    
                    // 喂狗（TCP发送后）
                    esp_task_wdt_reset();
                    
                    if (sent > 0) {
                        printf("[TCP] Sent %d bytes to server\n", sent);
                    } else {
                        printf("[TCP] Failed to send data (errno=%d, time=%lu us)\n", errno, tcp_time);
                        tcp_reconnect_attempts = 0;
                        last_tcp_reconnect_time = esp_timer_get_time();
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

        // ===== 喂看门狗（每次循环结束时喂狗）=====
        esp_task_wdt_reset();
        
        vTaskDelay(pdMS_TO_TICKS(10));    /* 延时10ms */
    }
    
    // 释放缓冲区（理论上不会执行到这里）
    if (rx_buffer) {
        free(rx_buffer);
    }
}