/**
 ****************************************************************************************************
 * @file        timg.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2025-01-01
 * @brief       TIMG驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 ESP32-S3 开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#include "timg.h"
#include "led.h"


/* 选择定时器回调方式 */
#define SELECT_CALLBACK     1

#if SELECT_CALLBACK == 0
/**
 * @brief       定时器组回调函数（不常用）
 * @param       args: 传入参数
 * @retval      默认返回1
 */
void not_timer_group_isr_callback(void *args) 
{
    timg_config_t *user_data = (timg_config_t *) args;
    /* 获取定时器分组0中的哪个定时器产生中断*/
    uint32_t timer_intr = timer_group_get_intr_status_in_isr(user_data->timer_group); 
    /* 获取中断状态 */
    if (timer_intr & TIMER_INTR_T0) 
    {
        /* 清除中断状态 */
        timer_group_clr_intr_status_in_isr(user_data->timer_group,user_data->timer_idx);

        /* 用户实现代码 */ 
        /* 存储当前计数值变量清零 */
        user_data->timer_count_value = 0;
        /* 获取当前计数值 */
        user_data->timer_count_value = timer_group_get_counter_value_in_isr(user_data->timer_group, user_data->timer_idx);

        /* 如果没有开启重装载，那么想要周期警报，必须设置警报值为t0 += t0 */
        if (!user_data->auto_reload)
        {
            user_data->alarm_value += user_data->timing_time;
            /* 设置警报值（ISR类型函数） */
            timer_group_set_alarm_value_in_isr(user_data->timer_group, user_data->timer_idx, user_data->alarm_value);
        }

        /* 重新使能定时器中断 */ 
        timer_group_enable_alarm_in_isr(user_data->timer_group, user_data->timer_idx);
    }
    else if (timer_intr & TIMER_INTR_T1)
    {
        /* 定时器1中断 */
    }
    else if (timer_intr & TIMER_INTR_WDT)
    {
        /* 看门狗定时器中断 */
    }
    else
    {

    }
}
#else
/**
 * @brief       定时器组回调函数（常用）
 * @param       args: 传入参数
 * @retval      默认返回1
 */
static bool IRAM_ATTR timer_group_isr_callback(void *args)
{
    timg_config_t *user_data = (timg_config_t *) args;
    /* 存储当前计数值变量清零 */
    user_data->timer_count_value = 0;
    /* 获取当前计数值 */
    user_data->timer_count_value = timer_group_get_counter_value_in_isr(user_data->timer_group, user_data->timer_idx);
    LED0_TOGGLE();
    /* 如果没有开启重装载，那么想要周期警报，必须设置警报值为t0 += t0 */
    if (!user_data->auto_reload)
    {
        user_data->alarm_value += user_data->timing_time;
        /* 设置警报值（ISR类型函数） */
        timer_group_set_alarm_value_in_isr(user_data->timer_group, user_data->timer_idx, user_data->alarm_value);
    }

    return 1;
}
#endif

/**
 * @brief       初始化定时器组
 * @param       timg_config: 定时器配置结构体
 * @retval      无
 */
void timg_init(timg_config_t *timg_config)
{
    uint32_t clk_src_hz = 0;
    timer_config_t timer_config = {0};
    /* 获取时钟频率数值 */
    ESP_ERROR_CHECK(esp_clk_tree_src_get_freq_hz((soc_module_clk_t)timg_config->clk_src, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &clk_src_hz));
    
    timer_config.alarm_en = TIMER_ALARM_EN;                   /* 警报使能 */
    timer_config.auto_reload = timg_config->auto_reload;      /* 自动重装载值 */
    timer_config.clk_src = timg_config->clk_src;              /* 设置时钟源 */
    timer_config.counter_dir = TIMER_COUNT_UP;                /* 向上计数 */
    timer_config.counter_en = TIMER_PAUSE;                    /* 停止定时器 */
    timer_config.divider = clk_src_hz / 1000000;              /* 预分频(1us) */
    
    /* 选择那个定时器组，组内那个定时器，并配置定时器 */
    ESP_ERROR_CHECK(timer_init(timg_config->timer_group, timg_config->timer_idx, &timer_config));
    /* 设置当前计数值 */
    ESP_ERROR_CHECK(timer_set_counter_value(timg_config->timer_group, timg_config->timer_idx, 0));
    /* 设置警报值 */
    ESP_ERROR_CHECK(timer_set_alarm_value(timg_config->timer_group, timg_config->timer_idx, timg_config->alarm_value));
    /* 使能定时器中断 */
    ESP_ERROR_CHECK(timer_enable_intr(timg_config->timer_group, timg_config->timer_idx));
    /* 添加定时器回调函数 */
#if SELECT_CALLBACK == 0
    ESP_ERROR_CHECK(timer_isr_register(timg_config->timer_group, timg_config->timer_idx, not_timer_group_isr_callback, timg_config, 0,NULL));
#else
    ESP_ERROR_CHECK(timer_isr_callback_add(timg_config->timer_group, timg_config->timer_idx, timer_group_isr_callback, timg_config, 0));
#endif
    /* 开启定时器 */
    ESP_ERROR_CHECK(timer_start(timg_config->timer_group, timg_config->timer_idx));
}
