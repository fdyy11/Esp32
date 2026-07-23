/**
 ******************************************************************************
 * @file        relay.h
 * @brief       继电器控制模块头文件
 * @details     控制两路继电器(IO16/IO18)，实现电机正反转控制
 *              两路IO输出互补（相反），每8秒自动切换一次
 *              支持RTC内存保存状态，看门狗复位后恢复运行位置
 ******************************************************************************
 */

#ifndef __RELAY_H
#define __RELAY_H

#include "driver/gpio.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/* 继电器控制IO口定义 */
#define RELAY1_IO          GPIO_NUM_16      /* 继电器1 - 电机正转 */
#define RELAY2_IO          GPIO_NUM_18      /* 继电器2 - 电机反转 */

/* 继电器切换周期（单位：毫秒） */
#define RELAY_FORWARD_PERIOD_MS    15000       /* 正转时间：15秒 */
#define RELAY_REVERSE_PERIOD_MS    15000       /* 反转时间：15秒 */
#define RELAY_STOP_PERIOD_MS       5000        /* 停止时间：2秒 */
// #define RELAY_TOGGLE_PERIOD_MS     8000       /* 兼容旧代码，默认使用正转时间 */

/* RTC内存魔术字，用于验证数据有效性 */
#define RELAY_RTC_MAGIC    0x52454C41      /* "RELA" */

/**
 * @brief       继电器状态阶段枚举
 */
typedef enum {
    RELAY_PHASE_FORWARD = 0,    /* 正转阶段：IO16=HIGH, IO18=LOW */
    RELAY_PHASE_STOP1 = 1,      /* 停止阶段1：IO16=LOW, IO18=LOW (正转→反转) */
    RELAY_PHASE_REVERSE = 2,    /* 反转阶段：IO16=LOW, IO18=HIGH */
    RELAY_PHASE_STOP2 = 3       /* 停止阶段2：IO16=LOW, IO18=LOW (反转→正转) */
} relay_phase_t;

/**
 * @brief       RTC内存中保存的继电器状态结构
 */
typedef struct {
    uint32_t magic;               /* 魔术字，用于验证数据有效性 */
    bool relay1_state;            /* IO16的电平状态 */
    bool relay2_state;            /* IO18的电平状态 */
    uint32_t timer_count_ms;      /* 当前周期的运行时间（毫秒） */
    uint32_t cycle_count;         /* 完成的周期数 */
    bool paused;                  /* 暂停状态 */
    uint32_t reset_cause;         /* 复位原因：0=正常启动, 1=看门狗复位, 2=按键复位 */
    uint32_t checksum;            /* 校验和，用于数据完整性检查 */
    uint8_t state_phase;          /* 状态阶段：0=正转, 1=停止1, 2=反转, 3=停止2 */
} relay_rtc_state_t;

/* 函数声明 */
esp_err_t relay_init(void);                             /* 初始化继电器 */
void relay_set_state(bool relay1_on);                   /* 设置继电器状态 */
bool relay_get_state(void);                             /* 获取当前继电器1状态 */
void relay_toggle(void);                                /* 切换继电器状态 */
bool relay_task_handler(void);                          /* 继电器任务处理(需周期调用) */
uint32_t relay_get_cycle_count(void);                   /* 获取完成的周期数 */
void relay_pause(void);                                 /* 暂停继电器控制 */
void relay_resume(void);                                /* 恢复继电器控制 */
bool relay_is_paused(void);                             /* 检查是否处于暂停状态 */
void relay_stop_all(void);                              /* 停止所有继电器输出 */

/* 动态配置函数 */
void relay_set_forward_period(uint32_t period_ms);      /* 设置正转时间（毫秒） */
void relay_set_reverse_period(uint32_t period_ms);      /* 设置反转时间（毫秒） */
void relay_set_stop_period(uint32_t period_ms);         /* 设置停止时间（毫秒） */
uint32_t relay_get_forward_period(void);                /* 获取正转时间（毫秒） */
uint32_t relay_get_reverse_period(void);                /* 获取反转时间（毫秒） */
uint32_t relay_get_stop_period(void);                   /* 获取停止时间（毫秒） */

/* RTC内存相关函数 */
void relay_save_to_rtc(void);                           /* 保存状态到RTC内存 */
bool relay_restore_from_rtc(uint32_t reset_cause);      /* 从RTC内存恢复状态 */
void relay_clear_rtc_state(void);                       /* 清除RTC内存中的状态 */
relay_rtc_state_t* relay_get_rtc_state_ptr(void);       /* 获取RTC状态指针 */

#endif
