/**
 ******************************************************************************
 * @file        relay.h
 * @brief       继电器控制模块头文件
 * @details     控制两路继电器(IO16/IO18)，实现电机正反转控制
 *              两路IO输出互补（相反），每8秒自动切换一次
 ******************************************************************************
 */

#ifndef __RELAY_H
#define __RELAY_H

#include "driver/gpio.h"
#include "esp_err.h"
#include <stdbool.h>

/* 继电器控制IO口定义 */
#define RELAY1_IO          GPIO_NUM_16      /* 继电器1 - 电机正转 */
#define RELAY2_IO          GPIO_NUM_18      /* 继电器2 - 电机反转 */

/* 继电器切换周期（单位：毫秒） */
#define RELAY_TOGGLE_PERIOD_MS   8000       /* 8秒切换一次 */

/* 函数声明 */
esp_err_t relay_init(void);                             /* 初始化继电器 */
void relay_set_state(bool relay1_on);                   /* 设置继电器状态 */
bool relay_get_state(void);                             /* 获取当前继电器1状态 */
void relay_toggle(void);                                /* 切换继电器状态 */
bool relay_task_handler(void);                          /* 继电器任务处理(需周期调用) */
uint32_t relay_get_cycle_count(void);                   /* 获取完成的周期数 */

#endif
