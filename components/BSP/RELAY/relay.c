/**
 ******************************************************************************
 * @file        relay.c
 * @brief       继电器控制模块实现文件
 * @details     控制两路继电器(IO16/IO18)，实现电机正反转控制
 *              两路IO输出互补（相反），每8秒自动切换一次
 ******************************************************************************
 */

#include "relay.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "RELAY";

/* 继电器1当前状态 */
static bool s_relay1_state = false;

/* 定时器计数器（毫秒） */
static uint32_t s_timer_count_ms = 0;

/* 完成的周期计数 */
static uint32_t s_cycle_count = 0;

/* 暂停状态标志 */
static bool s_paused = false;

/* 暂停时保存的定时器计数值 */
static uint32_t s_saved_timer_count_ms = 0;

/**
 * @brief       初始化继电器
 * @retval      ESP_OK:初始化成功
 */
esp_err_t relay_init(void)
{
    gpio_config_t io_conf = {0};

    /* 配置继电器1 IO口 (IO16) */
    io_conf.intr_type    = GPIO_INTR_DISABLE;         /* 禁用中断 */
    io_conf.mode         = GPIO_MODE_OUTPUT;          /* 输出模式 */
    io_conf.pull_up_en   = GPIO_PULLUP_DISABLE;       /* 禁用上拉 */
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;     /* 禁用下拉 */
    io_conf.pin_bit_mask = (1ULL << RELAY1_IO);       /* 引脚位掩码 */
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    /* 配置继电器2 IO口 (IO18) */
    io_conf.pin_bit_mask = (1ULL << RELAY2_IO);
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    /* 初始状态：继电器1关闭，继电器2开启（互补） */
    relay_set_state(false);

    ESP_LOGI(TAG, "Relay initialized: IO16=%d, IO18=%d", RELAY1_IO, RELAY2_IO);
    return ESP_OK;
}

/**
 * @brief       设置继电器状态
 * @param       relay1_on: true=继电器1开启(电机正转), false=继电器2开启(电机反转)
 * @note        两路IO输出始终相反（互补）
 */
void relay_set_state(bool relay1_on)
{
    s_relay1_state = relay1_on;

    /* IO16和IO18输出相反 */
    gpio_set_level(RELAY1_IO, relay1_on ? 1 : 0);
    gpio_set_level(RELAY2_IO, relay1_on ? 0 : 1);

    ESP_LOGI(TAG, "Relay state changed: IO16=%d, IO18=%d (Motor %s)",
             relay1_on ? 1 : 0,
             relay1_on ? 0 : 1,
             relay1_on ? "FORWARD" : "REVERSE");
}

/**
 * @brief       获取当前继电器1状态
 * @retval      true:继电器1开启(电机正转), false:继电器2开启(电机反转)
 */
bool relay_get_state(void)
{
    return s_relay1_state;
}

/**
 * @brief       切换继电器状态（反转电机方向）
 */
void relay_toggle(void)
{
    relay_set_state(!s_relay1_state);
}

/**
 * @brief       获取完成的周期数
 * @retval      完成的收展周期数
 */
uint32_t relay_get_cycle_count(void)
{
    return s_cycle_count;
}

/**
 * @brief       继电器任务处理函数（需要在主循环中周期调用）
 * @details     每8秒自动切换一次IO输出
 *              每完成一个完整周期（IO16: LOW->HIGH->LOW，共16秒）计数加1
 * @param       无
 * @retval      true:完成了一个完整周期(16s), false:无完整周期
 * @note        调用间隔建议为10ms，内部使用计数器实现8秒定时
 */
bool relay_task_handler(void)
{
    bool cycle_completed = false;

    /* 如果处于暂停状态，不执行任何操作 */
    if (s_paused) {
        return false;
    }

    s_timer_count_ms += 10;  /* 每次调用增加10ms（需保证调用间隔为10ms） */

    if (s_timer_count_ms >= RELAY_TOGGLE_PERIOD_MS)
    {
        s_timer_count_ms = 0;
        relay_toggle();

        /* 只有当IO16回到LOW状态时，才算完成一个完整周期（LOW->HIGH->LOW = 16s） */
        if (!s_relay1_state)
        {
            s_cycle_count++;
            cycle_completed = true;
            ESP_LOGI(TAG, "Full cycle completed! Cycle count: %ld", s_cycle_count);
        }
        else
        {
            ESP_LOGI(TAG, "Relay toggled (half cycle), IO16=HIGH");
        }
    }

    return cycle_completed;
}

/**
 * @brief       暂停继电器控制
 * @details     保存当前定时器计数值，停止继电器输出
 */
void relay_pause(void)
{
    s_paused = true;
    s_saved_timer_count_ms = s_timer_count_ms;
    
    /* 停止所有继电器输出 */
    relay_stop_all();
    
    ESP_LOGI(TAG, "Relay paused at timer_count=%lu ms", s_saved_timer_count_ms);
}

/**
 * @brief       恢复继电器控制
 * @details     从暂停时的位置继续计时
 */
void relay_resume(void)
{
    s_paused = false;
    s_timer_count_ms = s_saved_timer_count_ms;
    
    /* 恢复继电器到之前的状态 */
    relay_set_state(s_relay1_state);
    
    ESP_LOGI(TAG, "Relay resumed from timer_count=%lu ms", s_timer_count_ms);
}

/**
 * @brief       检查是否处于暂停状态
 * @retval      true:已暂停, false:未暂停
 */
bool relay_is_paused(void)
{
    return s_paused;
}

/**
 * @brief       停止所有继电器输出
 * @details     将IO16和IO18都设置为0，停止电机运行
 */
void relay_stop_all(void)
{
    gpio_set_level(RELAY1_IO, 0);
    gpio_set_level(RELAY2_IO, 0);
    ESP_LOGI(TAG, "All relays stopped (IO16=0, IO18=0)");
}
