/**
 ******************************************************************************
 * @file        relay.c
 * @brief       继电器控制模块实现文件
 * @details     控制两路继电器(IO16/IO18)，实现电机正反转控制
 *              两路IO输出互补（相反），每8秒自动切换一次
 *              支持RTC内存保存状态，看门狗复位后恢复运行位置
 ******************************************************************************
 */

#include "relay.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "RELAY";

/* RTC慢速内存区域，用于保存继电器状态（断电后数据丢失，但复位后保留） */
// 使用RTC_DATA_ATTR宏将变量放在RTC慢速内存中
RTC_DATA_ATTR static relay_rtc_state_t s_rtc_relay_state = {0};

/* �继电器1当前状态 */
static bool s_relay1_state = false;

/* 定时器计数器（毫秒） */
static uint32_t s_timer_count_ms = 0;

/* 完成的周期计数 */
static uint32_t s_cycle_count = 0;

/* 暂停状态标志 */
static bool s_paused = false;

/* 暂停时保存的定时器计数值 */
static uint32_t s_saved_timer_count_ms = 0;

/* 可配置的正转、反转和停止周期（毫秒） */
static uint32_t s_forward_period_ms = RELAY_FORWARD_PERIOD_MS;
static uint32_t s_reverse_period_ms = RELAY_REVERSE_PERIOD_MS;
static uint32_t s_stop_period_ms = RELAY_STOP_PERIOD_MS;

/* 当前状态阶段 */
static relay_phase_t s_current_phase = RELAY_PHASE_FORWARD;

/**
 * @brief       计算校验和
 * @param       state: 状态结构指针
 * @retval      校验和值
 */
static uint32_t calculate_checksum(const relay_rtc_state_t *state)
{
    uint32_t checksum = 0;
    checksum ^= state->magic;
    checksum ^= (state->relay1_state ? 1 : 0);
    checksum ^= (state->relay2_state ? 1 : 0);
    checksum ^= state->timer_count_ms;
    checksum ^= state->cycle_count;
    checksum ^= (state->paused ? 1 : 0);
    checksum ^= state->reset_cause;
    checksum ^= state->state_phase;
    return checksum;
}

/**
 * @brief       验证RTC数据的完整性
 * @retval      true:数据有效, false:数据无效
 */
static bool validate_rtc_data(void)
{
    if (s_rtc_relay_state.magic != RELAY_RTC_MAGIC) {
        return false;
    }
    
    uint32_t expected_checksum = calculate_checksum(&s_rtc_relay_state);
    if (s_rtc_relay_state.checksum != expected_checksum) {
        ESP_LOGW(TAG, "RTC data checksum mismatch: expected=0x%08X, actual=0x%08X",
                 expected_checksum, s_rtc_relay_state.checksum);
        return false;
    }
    
    return true;
}

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

    /* 初始状态：停止状态（两个IO都为0） */
    relay_stop_all();
    s_current_phase = RELAY_PHASE_STOP1;  // 从停止阶段开始，然后进入正转

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

    ESP_LOGD(TAG, "Relay state changed: IO16=%d, IO18=%d (Motor %s)",
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
 * @details     实现四阶段状态机：正转→停止→反转→停止→正转...
 *              - 正转阶段(RELAY_PHASE_FORWARD): IO16=HIGH, IO18=LOW, 持续s_forward_period_ms
 *              - 停止阶段1(RELAY_PHASE_STOP1): IO16=LOW, IO18=LOW, 持续s_stop_period_ms
 *              - 反转阶段(RELAY_PHASE_REVERSE): IO16=LOW, IO18=HIGH, 持续s_reverse_period_ms
 *              - 停止阶段2(RELAY_PHASE_STOP2): IO16=LOW, IO18=LOW, 持续s_stop_period_ms
 *              每完成一个完整周期（正转+停止+反转+停止）计数加1
 * @param       无
 * @retval      true:完成了一个完整周期, false:无完整周期
 * @note        调用间隔建议为10ms，内部使用计数器实现定时
 */
bool relay_task_handler(void)
{
    bool cycle_completed = false;

    /* 如果处于暂停状态，不执行任何操作 */
    if (s_paused) {
        return false;
    }

    s_timer_count_ms += 10;  /* 每次调用增加10ms（需保证调用间隔为10ms） */

    /* 根据当前阶段选择不同的切换周期 */
    uint32_t current_period = 0;
    switch (s_current_phase) {
        case RELAY_PHASE_FORWARD:
            current_period = s_forward_period_ms;
            break;
        case RELAY_PHASE_STOP1:
        case RELAY_PHASE_STOP2:
            current_period = s_stop_period_ms;
            break;
        case RELAY_PHASE_REVERSE:
            current_period = s_reverse_period_ms;
            break;
        default:
            current_period = s_forward_period_ms;
            break;
    }

    if (s_timer_count_ms >= current_period)
    {
        s_timer_count_ms = 0;
        
        /* 切换到下一个阶段 */
        switch (s_current_phase) {
            case RELAY_PHASE_FORWARD:
                /* 正转结束 → 进入停止阶段1 */
                s_current_phase = RELAY_PHASE_STOP1;
                relay_stop_all();
                ESP_LOGD(TAG, "Phase changed: FORWARD -> STOP1");
                break;
                
            case RELAY_PHASE_STOP1:
                /* 停止阶段1结束 → 进入反转阶段 */
                s_current_phase = RELAY_PHASE_REVERSE;
                relay_set_state(false);  // IO16=LOW, IO18=HIGH
                ESP_LOGD(TAG, "Phase changed: STOP1 -> REVERSE");
                break;
                
            case RELAY_PHASE_REVERSE:
                /* 反转结束 → 进入停止阶段2 */
                s_current_phase = RELAY_PHASE_STOP2;
                relay_stop_all();
                ESP_LOGD(TAG, "Phase changed: REVERSE -> STOP2");
                break;
                
            case RELAY_PHASE_STOP2:
                /* 停止阶段2结束 → 进入正转阶段，完成一个完整周期 */
                s_current_phase = RELAY_PHASE_FORWARD;
                relay_set_state(true);  // IO16=HIGH, IO18=LOW
                s_cycle_count++;
                cycle_completed = true;
                ESP_LOGI(TAG, "Full cycle completed! Cycle count: %ld", s_cycle_count);
                ESP_LOGD(TAG, "Phase changed: STOP2 -> FORWARD");
                break;
                
            default:
                s_current_phase = RELAY_PHASE_FORWARD;
                relay_set_state(true);
                break;
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

/**
 * @brief       保存继电器状态到RTC内存
 * @details     在每次状态变化或定期调用此函数，以便复位后恢复
 */
void relay_save_to_rtc(void)
{
    s_rtc_relay_state.magic = RELAY_RTC_MAGIC;
    s_rtc_relay_state.relay1_state = s_relay1_state;
    s_rtc_relay_state.relay2_state = !s_relay1_state;  // 互补状态
    s_rtc_relay_state.timer_count_ms = s_timer_count_ms;
    s_rtc_relay_state.cycle_count = s_cycle_count;
    s_rtc_relay_state.paused = s_paused;
    s_rtc_relay_state.state_phase = (uint8_t)s_current_phase;
    // reset_cause由main.c设置
    s_rtc_relay_state.checksum = calculate_checksum(&s_rtc_relay_state);
    
    ESP_LOGD(TAG, "State saved to RTC: relay1=%d, timer=%lu ms, cycles=%lu, phase=%d",
             s_relay1_state, s_timer_count_ms, s_cycle_count, s_current_phase);
}

/**
 * @brief       从RTC内存恢复继电器状态
 * @param       reset_cause: 复位原因（0=正常启动, 1=看门狗复位, 2=按键复位）
 * @retval      true:恢复成功, false:恢复失败（数据无效或非看门狗复位）
 * @note        仅在看门狗复位时恢复状态，其他情况重新开始
 */
bool relay_restore_from_rtc(uint32_t reset_cause)
{
    ESP_LOGI(TAG, "Checking RTC state... Reset cause: %lu", reset_cause);
    
    // 首先验证数据完整性
    if (!validate_rtc_data()) {
        ESP_LOGW(TAG, "RTC data invalid or not initialized");
        return false;
    }
    
    // 仅在看门狗复位时恢复状态
    if (reset_cause != 1) {  // 1表示看门狗复位
        ESP_LOGI(TAG, "Not watchdog reset (cause=%lu), starting fresh", reset_cause);
        return false;
    }
    
    // 恢复状态
    s_relay1_state = s_rtc_relay_state.relay1_state;
    s_timer_count_ms = s_rtc_relay_state.timer_count_ms;
    s_cycle_count = s_rtc_relay_state.cycle_count;
    s_paused = s_rtc_relay_state.paused;
    s_current_phase = (relay_phase_t)s_rtc_relay_state.state_phase;
    
    // 如果之前是暂停状态，保存定时器计数
    if (s_paused) {
        s_saved_timer_count_ms = s_timer_count_ms;
    }
    
    // 应用恢复的状态到GPIO
    switch (s_current_phase) {
        case RELAY_PHASE_FORWARD:
            gpio_set_level(RELAY1_IO, 1);
            gpio_set_level(RELAY2_IO, 0);
            break;
        case RELAY_PHASE_REVERSE:
            gpio_set_level(RELAY1_IO, 0);
            gpio_set_level(RELAY2_IO, 1);
            break;
        case RELAY_PHASE_STOP1:
        case RELAY_PHASE_STOP2:
            gpio_set_level(RELAY1_IO, 0);
            gpio_set_level(RELAY2_IO, 0);
            break;
        default:
            gpio_set_level(RELAY1_IO, 0);
            gpio_set_level(RELAY2_IO, 0);
            s_current_phase = RELAY_PHASE_STOP1;
            break;
    }
    
    ESP_LOGI(TAG, "=== RESTORED FROM RTC MEMORY ===");
    ESP_LOGI(TAG, "  Phase: %d (%s)", 
             s_current_phase,
             s_current_phase == RELAY_PHASE_FORWARD ? "FORWARD" :
             s_current_phase == RELAY_PHASE_STOP1 ? "STOP1" :
             s_current_phase == RELAY_PHASE_REVERSE ? "REVERSE" : "STOP2");
    ESP_LOGI(TAG, "  Timer Count: %lu ms", s_timer_count_ms);
    ESP_LOGI(TAG, "  Cycle Count: %lu", s_cycle_count);
    ESP_LOGI(TAG, "  Paused: %d", s_paused);
    ESP_LOGI(TAG, "===============================");
    
    return true;
}

/**
 * @brief       清除RTC内存中的状态
 * @details     在正常启动或按键复位时调用
 */
void relay_clear_rtc_state(void)
{
    memset(&s_rtc_relay_state, 0, sizeof(relay_rtc_state_t));
    ESP_LOGI(TAG, "RTC state cleared");
}

/**
 * @brief       获取RTC状态指针（用于更新reset_cause）
 * @retval      RTC状态结构指针
 */
relay_rtc_state_t* relay_get_rtc_state_ptr(void)
{
    return &s_rtc_relay_state;
}

/**
 * @brief       设置正转时间
 * @param       period_ms: 正转时间（毫秒）
 * @note        最小值建议100ms，避免频繁切换
 */
void relay_set_forward_period(uint32_t period_ms)
{
    if (period_ms < 100) {
        ESP_LOGW(TAG, "Forward period too short, set to minimum 100ms");
        s_forward_period_ms = 100;
    } else {
        s_forward_period_ms = period_ms;
    }
    ESP_LOGI(TAG, "Forward period set to %lu ms (%.1f s)", 
             s_forward_period_ms, s_forward_period_ms / 1000.0);
}

/**
 * @brief       设置反转时间
 * @param       period_ms: 反转时间（毫秒）
 * @note        最小值建议100ms，避免频繁切换
 */
void relay_set_reverse_period(uint32_t period_ms)
{
    if (period_ms < 100) {
        ESP_LOGW(TAG, "Reverse period too short, set to minimum 100ms");
        s_reverse_period_ms = 100;
    } else {
        s_reverse_period_ms = period_ms;
    }
    ESP_LOGI(TAG, "Reverse period set to %lu ms (%.1f s)", 
             s_reverse_period_ms, s_reverse_period_ms / 1000.0);
}

/**
 * @brief       获取正转时间
 * @retval      正转时间（毫秒）
 */
uint32_t relay_get_forward_period(void)
{
    return s_forward_period_ms;
}

/**
 * @brief       获取反转时间
 * @retval      反转时间（毫秒）
 */
uint32_t relay_get_reverse_period(void)
{
    return s_reverse_period_ms;
}

/**
 * @brief       设置停止时间
 * @param       period_ms: 停止时间（毫秒）
 * @note        最小值建议100ms，避免频繁切换
 */
void relay_set_stop_period(uint32_t period_ms)
{
    if (period_ms < 100) {
        ESP_LOGW(TAG, "Stop period too short, set to minimum 100ms");
        s_stop_period_ms = 100;
    } else {
        s_stop_period_ms = period_ms;
    }
    ESP_LOGI(TAG, "Stop period set to %lu ms (%.1f s)", 
             s_stop_period_ms, s_stop_period_ms / 1000.0);
}

/**
 * @brief       获取停止时间
 * @retval      停止时间（毫秒）
 */
uint32_t relay_get_stop_period(void)
{
    return s_stop_period_ms;
}
