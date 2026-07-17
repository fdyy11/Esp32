# RTC状态恢复功能测试指南

## 快速测试步骤

### 步骤1：编译和烧录

```bash
cd d:\Project\ESPPro\TestPro1
idf.py build
idf.py flash monitor
```

### 步骤2：观察正常启动

首次启动应该看到：
```
=== System Starting ===
[RESET] Power-on reset - Starting fresh
Initializing WiFi TCP client...
XL9555 initialized successfully
SPI bus initialized successfully
Relay initialized: IO16=RELAY1(FWD), IO18=RELAY2(REV)
[RTC] Checking RTC state... Reset cause: 0
[RTC] RTC data invalid or not initialized
[RTC] Starting with fresh state
System Ready
Fresh Start
```

### 步骤3：等待系统运行

让系统正常运行至少**20秒**（完成1个完整周期），确保RTC内存中有有效数据。

观察输出：
```
[DIAG] Loop=1000, Elapsed=10 s, Stack=xxxx words, FreeHeap=xxxxx
[DIAG] Max operation times: I2C=xxx us, TCP=xxx us, SPI=xxx us
[DIAG] TCP connected=1, Relay cycles=0, Paused=0, Failures=0
I (xxxx) RELAY: Relay toggled (half cycle), IO16=HIGH
I (xxxx) RELAY: Full cycle completed! Cycle count: 1
```

### 步骤4：触发看门狗复位

#### 方法A：故意制造TCP阻塞（推荐）

在串口监控中，断开WiFi路由器或关闭TCP服务器，然后观察：

```
[TCP] Not connected, cannot send data
[TCP] Attempting reconnection (1/5)...
[TCP] Reconnection failed, will retry later
[CRITICAL] Too many consecutive failures (101). Triggering system reset...
[CRITICAL] System will reboot in 3 seconds...
```

等待3秒后系统会自动复位。

#### 方法B：修改代码临时禁用喂狗

在`main.c`的主循环中注释掉所有`esp_task_wdt_reset()`调用，等待5秒后看门狗会触发复位。

### 步骤5：验证状态恢复

系统复位后，应该看到：

```
=== System Starting ===
[RESET] Task watchdog reset - Will restore state
Initializing WiFi TCP client...
XL9555 initialized successfully
SPI bus initialized successfully
Relay initialized: IO16=RELAY1(FWD), IO18=RELAY2(REV)
[RTC] Checking RTC state... Reset cause: 1
[RTC] === RESTORED FROM RTC MEMORY ===
[RTC]   Relay1 State: 1 (FORWARD)      ← 恢复了之前的状态
[RTC]   Timer Count: 3500 ms           ← 恢复了定时器计数
[RTC]   Cycle Count: 1                 ← 恢复了周期数
[RTC]   Paused: 0
[RTC] ===============================
[RTC] Successfully restored relay state from RTC memory
Watchdog will reset CPU on timeout (correct configuration)
RESTORED                                ← LCD显示
Cycle: 1                                ← LCD显示
```

**关键验证点：**
- ✅ `Reset cause: 1` 表示检测到看门狗复位
- ✅ `RESTORED FROM RTC MEMORY` 表示成功恢复
- ✅ `Relay1 State`、`Timer Count`、`Cycle Count` 与复位前一致
- ✅ LCD显示"RESTORED"而不是"System Ready"

### 步骤6：继续运行验证

恢复后系统应该从之前的位置继续运行：

- 如果恢复时`Timer Count = 3500ms`，那么再过4.5秒（8000-3500）继电器会切换
- 周期数从恢复的值继续累加
- 如果之前是暂停状态，恢复后仍然保持暂停

观察输出：
```
I (xxxx) RELAY: Relay toggled (half cycle), IO16=HIGH  ← 约4.5秒后
I (xxxx) RELAY: Full cycle completed! Cycle count: 2   ← 再过8秒
```

### 步骤7：测试按键复位不恢复

按下开发板上的**复位按钮（RST）**或**使能按钮（EN）**：

应该看到：
```
=== System Starting ===
[RESET] Software reset - Starting fresh    ← 不是看门狗复位
[RTC] Checking RTC state... Reset cause: 0
[RTC] Not watchdog reset (cause=0), starting fresh
[RTC] Starting with fresh state
[RTC] RTC state cleared                     ← 清除了RTC状态
System Ready                                ← LCD显示
Fresh Start                                 ← LCD显示
```

**关键验证点：**
- ✅ `Reset cause: 0` 表示非看门狗复位
- ✅ `starting fresh` 表示从头开始
- ✅ `RTC state cleared` 表示清除了状态
- ✅ LCD显示"System Ready"而不是"RESTORED"

### 步骤8：测试断电复位不恢复

1. 断开USB电源
2. 等待5秒（确保完全断电）
3. 重新连接USB电源

应该看到：
```
=== System Starting ===
[RESET] Power-on reset - Starting fresh    ← 上电复位
[RTC] Checking RTC state... Reset cause: 0
[RTC] RTC data invalid or not initialized  ← RTC数据丢失
[RTC] Starting with fresh state
System Ready
Fresh Start
```

**关键验证点：**
- ✅ `Power-on reset` 表示上电复位
- ✅ `RTC data invalid` 表示数据已丢失（断电导致）
- ✅ 从头开始运行

## 测试检查清单

使用以下清单确保所有功能正常工作：

### 基础功能测试
- [ ] 系统能够正常启动
- [ ] 继电器每8秒切换一次
- [ ] 每16秒完成一个完整周期
- [ ] LCD显示正确
- [ ] TCP通信正常

### RTC保存测试
- [ ] 运行20秒后查看RTC状态
- [ ] 按下KEY0暂停，检查是否保存
- [ ] 按下KEY1恢复，检查是否保存
- [ ] 完成一个周期后检查是否保存

### 看门狗恢复测试
- [ ] 触发看门狗复位
- [ ] 复位后显示"RESTORED FROM RTC MEMORY"
- [ ] IO16/IO18电平与复位前一致
- [ ] 定时器计数连续（不重置为0）
- [ ] 周期数保持不变
- [ ] 从恢复的位置继续运行

### 其他复位测试
- [ ] 按下复位键后从头开始
- [ ] 断电重启后从头开始
- [ ] 软件复位后从头开始
- [ ] LCD显示"System Ready"而不是"RESTORED"

### 边界情况测试
- [ ] 暂停状态下看门狗复位，恢复后仍暂停
- [ ] 周期中间看门狗复位，恢复后继续计时
- [ ] 多次看门狗复位，每次都能正确恢复
- [ ] RTC数据损坏时能检测并从头开始

## 调试命令

### 查看当前RTC状态

在GDB调试器中：
```gdb
# 查看RTC状态结构
print s_rtc_relay_state

# 查看魔术字
print/x s_rtc_relay_state.magic

# 查看校验和
print/x s_rtc_relay_state.checksum

# 查看继电器状态
print s_rtc_relay_state.relay1_state
print s_rtc_relay_state.timer_count_ms
print s_rtc_relay_state.cycle_count
```

### 强制清除RTC状态

在代码中添加临时测试代码：
```c
// 在app_main开始时添加
printf("Forcing RTC state clear for testing...\n");
relay_clear_rtc_state();
```

### 手动触发保存

在任何位置添加：
```c
relay_save_to_rtc();
printf("Manual RTC save triggered\n");
```

## 常见问题排查

### 问题1：看门狗复位后没有恢复

**可能原因：**
- sdkconfig配置错误（CONFIG_ESP_TASK_WDT_PANIC=y）
- 复位原因检测失败
- RTC数据验证失败

**解决方法：**
```bash
# 检查sdkconfig配置
findstr /C:"CONFIG_ESP_TASK_WDT" sdkconfig

# 应该看到：
# CONFIG_ESP_TASK_WDT_PANIC=n
# CONFIG_ESP_TASK_WDT_RESET_CPU=y
```

### 问题2：显示"RTC data invalid"

**可能原因：**
- 首次运行，RTC未初始化
- 断电后数据丢失
- 校验和不匹配

**解决方法：**
这是正常现象，系统会从头开始。如果需要强制初始化，可以运行一次完整周期。

### 问题3：恢复后状态不正确

**可能原因：**
- 保存时机不对
- 数据被覆盖
- 内存损坏

**解决方法：**
增加保存频率，在关键操作前后都调用`relay_save_to_rtc()`。

### 问题4：无法触发看门狗复位

**可能原因：**
- 喂狗太频繁
- 超时时间设置过长
- 看门狗未正确启用

**解决方法：**
```c
// 临时注释掉主循环中的喂狗代码
// esp_task_wdt_reset();

// 或者减少超时时间
// CONFIG_ESP_TASK_WDT_TIMEOUT_S=3
```

## 性能影响评估

### RTC保存操作耗时

| 操作 | 耗时 | 说明 |
|------|------|------|
| `relay_save_to_rtc()` | < 10μs | 仅内存拷贝和校验和计算 |
| `relay_restore_from_rtc()` | < 50μs | 包含验证和GPIO设置 |
| `relay_clear_rtc_state()` | < 5μs | 仅内存清零 |

### 对主循环的影响

- **每秒保存一次**：影响可忽略（10μs / 10ms = 0.1%）
- **每个周期保存**：影响可忽略（16秒才执行一次）
- **按键时保存**：用户无感知

### 内存占用

- **RTC慢速内存占用**：约32字节（relay_rtc_state_t结构体）
- **常规RAM占用**：无额外占用
- **总容量可用**：约8KB，当前使用<0.5%

## 预期测试结果总结

| 测试场景 | 复位原因 | 是否恢复 | LCD显示 | 行为 |
|---------|---------|---------|---------|------|
| 看门狗超时 | TASK_WDT | ✅ 是 | RESTORED | 从断点继续 |
| 按下RST键 | SW | ❌ 否 | System Ready | 从头开始 |
| 断电重启 | POWERON | ❌ 否 | System Ready | 从头开始 |
| 软件复位 | SW | ❌ 否 | System Ready | 从头开始 |
| 首次启动 | POWERON | ❌ 否 | System Ready | 从头开始 |

---

**按照以上步骤测试，可以全面验证RTC状态恢复功能的正确性！** 🎯