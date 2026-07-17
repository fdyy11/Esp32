# ESP32 RTC内存状态恢复功能说明

## 功能概述

本功能使用ESP32的RTC慢速内存（RTC Slow Memory）来保存继电器的运行状态，使得系统在**看门狗复位**后能够恢复到之前的运行位置继续工作，而不是从头开始。

## 核心特性

### 1. 智能复位检测

系统能够区分不同类型的复位：

| 复位类型 | 行为 | 说明 |
|---------|------|------|
| **看门狗复位** | ✅ 恢复状态 | 从RTC内存恢复IO16/IO18电平、定时器计数、周期数 |
| **按键复位** | ❌ 重新开始 | 清除RTC状态，从头启动 |
| **断电复位** | ❌ 重新开始 | RTC数据丢失，从头启动 |
| **软件复位** | ❌ 重新开始 | 清除RTC状态，从头启动 |

### 2. 状态保存内容

RTC内存中保存以下信息：

```c
typedef struct {
    uint32_t magic;               // 魔术字 (0x52454C41 = "RELA")
    bool relay1_state;            // IO16的电平状态
    bool relay2_state;            // IO18的电平状态（互补）
    uint32_t timer_count_ms;      // 当前周期的运行时间（毫秒）
    uint32_t cycle_count;         // 完成的周期数
    bool paused;                  // 暂停状态
    uint32_t reset_cause;         // 复位原因
    uint32_t checksum;            // 校验和
} relay_rtc_state_t;
```

### 3. 自动保存机制

状态在以下时机自动保存到RTC内存：

- ✅ **每个完整周期完成后**（约16秒）
- ✅ **每100次主循环**（约1秒）
- ✅ **按下KEY0暂停时**
- ✅ **按下KEY1恢复时**

## 工作流程

### 场景1：正常运行 → 看门狗超时 → 自动恢复

```
时间线：
T0: 系统启动，relay1=LOW, timer=0ms, cycles=0
T1: 运行5秒，relay1=LOW, timer=5000ms
T2: 看门狗超时，系统复位
T3: 系统重启，检测到看门狗复位
T4: 从RTC恢复：relay1=LOW, timer=5000ms, cycles=0
T5: 继续从5000ms处运行，3秒后切换继电器
```

**串口输出示例：**
```
[RESET] Task watchdog reset - Will restore state
[RTC] Checking RTC state... Reset cause: 1
[RTC] === RESTORED FROM RTC MEMORY ===
[RTC]   Relay1 State: 0 (REVERSE)
[RTC]   Timer Count: 5000 ms
[RTC]   Cycle Count: 0
[RTC]   Paused: 0
[RTC] ===============================
[RTC] Successfully restored relay state from RTC memory
```

### 场景2：正常运行 → 按键复位 → 重新开始

```
时间线：
T0: 系统运行中，relay1=HIGH, timer=3000ms, cycles=5
T1: 用户按下复位键
T2: 系统重启，检测到软件复位
T3: 清除RTC状态，从头启动
T4: relay1=LOW, timer=0ms, cycles=0
```

**串口输出示例：**
```
[RESET] Software reset - Starting fresh
[RTC] Checking RTC state... Reset cause: 0
[RTC] Not watchdog reset (cause=0), starting fresh
[RTC] Starting with fresh state
[RTC] RTC state cleared
```

### 场景3：正常运行 → 断电 → 重新开始

```
时间线：
T0: 系统运行中
T1: 断电（RTC内存数据丢失）
T2: 重新上电
T3: 检测到上电复位，RTC数据无效
T4: 从头启动
```

**串口输出示例：**
```
[RESET] Power-on reset - Starting fresh
[RTC] Checking RTC state... Reset cause: 0
[RTC] RTC data invalid or not initialized
[RTC] Starting with fresh state
```

## 技术实现细节

### 1. RTC慢速内存

ESP32的RTC慢速内存在复位后仍然保持数据（但断电后会丢失）：

```c
// 使用RTC_DATA_ATTR宏将变量放在RTC慢速内存中
RTC_DATA_ATTR static relay_rtc_state_t s_rtc_relay_state = {0};
```

**特点：**
- ✅ 复位后数据保留
- ❌ 断电后数据丢失
- 📦 容量有限（约8KB可用）
- ⚡ 访问速度与常规RAM相同

### 2. 数据完整性保护

使用**魔术字 + 校验和**双重验证：

```c
// 写入时计算校验和
s_rtc_relay_state.magic = RELAY_RTC_MAGIC;  // 0x52454C41
s_rtc_relay_state.checksum = calculate_checksum(&s_rtc_relay_state);

// 读取时验证
if (s_rtc_relay_state.magic != RELAY_RTC_MAGIC) {
    return false;  // 数据未初始化
}
if (s_rtc_relay_state.checksum != expected_checksum) {
    return false;  // 数据损坏
}
```

### 3. 复位原因检测

```c
esp_reset_reason_t reset_reason = esp_reset_reason();

switch (reset_reason) {
    case ESP_RST_TASK_WDT:
    case ESP_RST_INT_WDT:
    case ESP_RST_WDT:
    case ESP_RST_PANIC:
        rtc_reset_cause = 1;  // 看门狗复位，需要恢复
        break;
    default:
        rtc_reset_cause = 0;  // 其他复位，重新开始
        break;
}
```

## API使用说明

### 1. 保存状态到RTC

```c
// 在任何需要保存状态的地方调用
relay_save_to_rtc();
```

### 2. 从RTC恢复状态

```c
// 在系统启动时调用
uint32_t reset_cause = 1;  // 1=看门狗复位, 0=其他
bool restored = relay_restore_from_rtc(reset_cause);

if (restored) {
    printf("成功恢复状态\n");
} else {
    printf("从头开始\n");
}
```

### 3. 清除RTC状态

```c
// 在正常启动或按键复位时调用
relay_clear_rtc_state();
```

### 4. 获取RTC状态指针

```c
// 用于更新reset_cause字段
relay_rtc_state_t *rtc_state = relay_get_rtc_state_ptr();
rtc_state->reset_cause = 1;
relay_save_to_rtc();
```

## 配置要求

### 1. sdkconfig配置

确保以下配置项正确设置：

```
CONFIG_ESP_TASK_WDT_EN=y              # 启用看门狗
CONFIG_ESP_TASK_WDT_INIT=y            # 初始化看门狗
CONFIG_ESP_TASK_WDT_PANIC=n           # 不要仅panic
CONFIG_ESP_TASK_WDT_RESET_CPU=y       # 超时后复位CPU
CONFIG_ESP_TASK_WDT_TIMEOUT_S=5       # 5秒超时
```

### 2. 编译选项

无需特殊编译选项，RTC内存支持是ESP-IDF内置功能。

## 测试方法

### 测试1：验证看门狗恢复功能

1. 烧录程序并启动
2. 等待系统运行几个周期
3. 故意制造卡死（如断开WiFi后频繁TCP发送）
4. 观察5秒后是否自动复位
5. 检查串口输出是否显示"RESTORED FROM RTC MEMORY"
6. 验证继电器状态和周期数是否正确恢复

**预期结果：**
```
[DIAG] Loop=500, TCP connected=0, Relay cycles=2
[CRITICAL] Too many consecutive failures. Triggering system reset...
[RESET] Task watchdog reset - Will restore state
[RTC] === RESTORED FROM RTC MEMORY ===
[RTC]   Relay1 State: 1 (FORWARD)
[RTC]   Timer Count: 3500 ms
[RTC]   Cycle Count: 2
```

### 测试2：验证按键复位不恢复

1. 系统运行中
2. 按下开发板上的复位按钮（或EN按钮）
3. 观察系统是否从头开始

**预期结果：**
```
[RESET] Software reset - Starting fresh
[RTC] Not watchdog reset (cause=0), starting fresh
[RTC] RTC state cleared
```

### 测试3：验证断电后不恢复

1. 系统运行中
2. 断开电源
3. 重新上电
4. 观察系统是否从头开始

**预期结果：**
```
[RESET] Power-on reset - Starting fresh
[RTC] RTC data invalid or not initialized
[RTC] Starting with fresh state
```

## 注意事项

### ⚠️ 重要提醒

1. **RTC内存不是永久存储**
   - 断电后数据丢失
   - 仅适用于复位恢复，不适用于长期存储

2. **保存频率不宜过高**
   - 当前设计：每秒保存一次 + 关键事件保存
   - 过于频繁的写入可能影响性能

3. **校验和验证必须通过**
   - 如果校验和不匹配，数据会被拒绝
   - 确保`calculate_checksum()`函数正确实现

4. **看门狗超时时间要合理**
   - 建议5-10秒
   - 太短：可能误触发
   - 太长：故障恢复慢

5. **状态恢复后的行为**
   - 继电器立即恢复到之前的电平
   - 定时器从之前的计数继续
   - 周期数保持不变
   - 如果是暂停状态，继续保持暂停

### 🔧 调试技巧

1. **查看RTC内存内容**
   ```c
   relay_rtc_state_t *state = relay_get_rtc_state_ptr();
   printf("RTC Magic: 0x%08X\n", state->magic);
   printf("RTC Checksum: 0x%08X\n", state->checksum);
   ```

2. **强制清除RTC状态**
   ```c
   // 在app_main开始时调用
   relay_clear_rtc_state();
   ```

3. **模拟看门狗复位**
   ```c
   // 用于测试恢复功能
   esp_restart();  // 软件复位，不会触发恢复
   ```

## 常见问题

### Q1: 为什么断电后不能恢复？
A: RTC慢速内存在断电后会丢失数据。这是硬件限制，无法避免。如果需要永久存储，应使用Flash或EEPROM。

### Q2: 如何确认是看门狗复位？
A: 使用`esp_reset_reason()`函数，返回值为`ESP_RST_TASK_WDT`、`ESP_RST_INT_WDT`、`ESP_RST_WDT`或`ESP_RST_PANIC`时表示看门狗相关复位。

### Q3: 保存状态会影响性能吗？
A: 影响很小。RTC内存访问速度与常规RAM相同，每次保存只需几微秒。

### Q4: 如果校验和错误怎么办？
A: 系统会认为数据无效，从头开始运行。这通常发生在内存损坏或未初始化的情况下。

### Q5: 可以同时保存多个任务的状态吗？
A: 可以。每个任务使用独立的RTC数据结构即可。注意总大小不要超过RTC慢速内存容量（约8KB）。

## 相关文件

- `components/BSP/RELAY/relay.h` - 继电器模块头文件（包含RTC结构定义）
- `components/BSP/RELAY/relay.c` - 继电器模块实现（包含RTC保存/恢复逻辑）
- `main/main.c` - 主程序（集成复位检测和状态恢复）

## 版本历史

- **V1.0** (2025-01-01): 初始版本，实现基本的RTC状态保存和恢复功能
- **V1.1** (2025-01-01): 添加校验和验证、复位原因检测、智能恢复逻辑

---

**通过RTC内存状态恢复功能，系统在看门狗复位后能够无缝继续之前的工作，大大提高了系统的可靠性和连续性！** 🎉