# RTC状态恢复功能 - 快速开始指南

## 🚀 5分钟快速上手

### 第1步：确认看门狗配置（必须）

在项目根目录运行PowerShell脚本修复看门狗配置：

```powershell
cd d:\Project\ESPPro\TestPro1
.\fix_watchdog_config.ps1
```

**或者手动检查：**
```bash
findstr /C:"CONFIG_ESP_TASK_WDT" sdkconfig
```

确保看到：
```
CONFIG_ESP_TASK_WDT_PANIC=n           # ✅ 必须是n
CONFIG_ESP_TASK_WDT_RESET_CPU=y       # ✅ 必须是y
CONFIG_ESP_TASK_WDT_TIMEOUT_S=5       # ✅ 建议5秒
```

### 第2步：编译和烧录

```bash
idf.py fullclean
idf.py build
idf.py flash monitor
```

### 第3步：观察启动信息

首次启动应该看到：

```
=== System Starting ===
[RESET] Power-on reset - Starting fresh
...
[RTC] Checking RTC state... Reset cause: 0
[RTC] RTC data invalid or not initialized
[RTC] Starting with fresh state
System Ready
Fresh Start
```

✅ **如果看到以上输出，说明功能正常！**

### 第4步：让系统运行至少20秒

等待继电器完成至少1个完整周期（16秒），确保RTC内存中有有效数据。

观察输出：
```
I (xxxx) RELAY: Full cycle completed! Cycle count: 1
[DIAG] Loop=2000, Elapsed=20 s, ...
```

### 第5步：触发看门狗复位测试

#### 方法A：断开WiFi（推荐）

1. 关闭WiFi路由器或TCP服务器
2. 等待系统检测到连接失败
3. 约100秒后会触发自动复位

观察输出：
```
[TCP] Not connected, cannot send data
[CRITICAL] Too many consecutive failures. Triggering system reset...
[CRITICAL] System will reboot in 3 seconds...

=== System Starting ===
[RESET] Task watchdog reset - Will restore state
...
[RTC] === RESTORED FROM RTC MEMORY ===
[RTC]   Relay1 State: 1 (FORWARD)
[RTC]   Timer Count: 3500 ms
[RTC]   Cycle Count: 1
[RTC] Successfully restored relay state from RTC memory
RESTORED
Cycle: 1
```

✅ **如果看到"RESTORED FROM RTC MEMORY"，说明恢复成功！**

#### 方法B：修改代码临时禁用喂狗

在`main.c`中注释掉主循环中的所有`esp_task_wdt_reset()`调用，等待5秒后会自动复位。

### 第6步：验证恢复效果

恢复后系统应该：
- ✅ IO16/IO18保持之前的电平
- ✅ 定时器从之前的计数继续（不是从0开始）
- ✅ 周期数保持不变
- ✅ LCD显示"RESTORED"而不是"System Ready"

## 📋 功能验证清单

使用以下清单快速验证功能：

### 基础验证
- [ ] 系统能正常启动
- [ ] 继电器每8秒切换一次
- [ ] LCD显示正确

### RTC保存验证
- [ ] 运行20秒后有数据保存
- [ ] KEY0暂停时保存
- [ ] KEY1恢复时保存
- [ ] 周期完成时保存

### 看门狗恢复验证
- [ ] 触发看门狗复位
- [ ] 复位后显示"RESTORED"
- [ ] IO状态与复位前一致
- [ ] 定时器连续（不重置）
- [ ] 周期数保持

### 其他复位验证
- [ ] 按下RST键后从头开始
- [ ] 断电重启后从头开始
- [ ] LCD显示"System Ready"

## 🔍 常见问题快速排查

### Q1: 看门狗复位后没有恢复？

**检查步骤：**
```bash
# 1. 检查sdkconfig配置
findstr /C:"CONFIG_ESP_TASK_WDT_PANIC" sdkconfig
# 应该是：CONFIG_ESP_TASK_WDT_PANIC=n

# 2. 检查是否有警告
# 启动时应该看到：
# "Watchdog will reset CPU on timeout (correct configuration)"
# 而不是：
# "[WARNING] Watchdog is configured to PANIC instead of RESET!"
```

**解决方法：**
```powershell
.\fix_watchdog_config.ps1
idf.py fullclean
idf.py build
idf.py flash monitor
```

### Q2: 显示"RTC data invalid"？

这是**正常现象**，表示：
- 首次运行，RTC未初始化
- 或者断电后数据丢失

系统会从头开始，运行一个周期后就会有有效数据。

### Q3: 如何强制清除RTC状态？

在`app_main`开始时添加：
```c
printf("Forcing RTC clear...\n");
relay_clear_rtc_state();
```

### Q4: 如何查看当前RTC状态？

在代码中添加调试输出：
```c
relay_rtc_state_t *state = relay_get_rtc_state_ptr();
printf("RTC Magic: 0x%08X\n", state->magic);
printf("RTC Checksum: 0x%08X\n", state->checksum);
printf("Relay1: %d, Timer: %lu, Cycles: %lu\n",
       state->relay1_state, state->timer_count_ms, state->cycle_count);
```

## 📊 预期行为对照表

| 场景 | 复位原因 | LCD显示 | IO状态 | 定时器 | 周期数 |
|------|---------|---------|--------|--------|--------|
| 首次启动 | POWERON | System Ready | LOW | 0ms | 0 |
| 正常运行 | - | Motor: FORWARD/REVERSE | 切换 | 递增 | 递增 |
| 看门狗复位 | TASK_WDT | RESTORED | **恢复** | **恢复** | **恢复** |
| 按键复位 | SW | System Ready | LOW | 0ms | 0 |
| 断电重启 | POWERON | System Ready | LOW | 0ms | 0 |

**关键区别：**
- ✅ 看门狗复位：**加粗**的项会恢复
- ❌ 其他复位：全部重置

## 💡 使用技巧

### 技巧1：调整保存频率

如果觉得每秒保存太频繁，可以修改`main.c`：

```c
// 原来：每100次循环（约1秒）
if (loop_count % 100 == 0 && !relay_is_paused()) {
    relay_save_to_rtc();
}

// 改为：每500次循环（约5秒）
if (loop_count % 500 == 0 && !relay_is_paused()) {
    relay_save_to_rtc();
}
```

### 技巧2：增加更多保存点

在关键操作后立即保存：

```c
// TCP发送成功后
if (sent > 0) {
    relay_save_to_rtc();  // 新增
    printf("[TCP] Sent successfully\n");
}
```

### 技巧3：监控RTC状态

在诊断输出中添加RTC信息：

```c
printf("[DIAG] TCP connected=%d, Relay cycles=%ld, Paused=%d, Failures=%lu\n",
       tcp_is_connected(), relay_get_cycle_count(), relay_is_paused(), consecutive_failures);

// 新增：显示RTC状态
relay_rtc_state_t *rtc = relay_get_rtc_state_ptr();
printf("[DIAG] RTC valid=%d, timer=%lu ms\n",
       (rtc->magic == RELAY_RTC_MAGIC), rtc->timer_count_ms);
```

## 🎯 下一步

功能验证通过后，可以：

1. **阅读详细文档**
   - [`RTC_STATE_RECOVERY_GUIDE.md`](RTC_STATE_RECOVERY_GUIDE.md) - 完整功能说明
   - [`RTC_TEST_GUIDE.md`](RTC_TEST_GUIDE.md) - 详细测试指南
   - [`RTC_IMPLEMENTATION_SUMMARY.md`](RTC_IMPLEMENTATION_SUMMARY.md) - 实现总结

2. **进行压力测试**
   - 运行至少200个周期（53分钟）
   - 多次触发看门狗复位
   - 验证数据一致性

3. **根据需求优化**
   - 调整保存频率
   - 增加更多状态保存
   - 添加Flash持久化

## 📞 需要帮助？

如果遇到任何问题：

1. 查看串口输出的错误信息
2. 检查sdkconfig配置是否正确
3. 参考详细文档中的故障排查章节
4. 使用调试命令查看RTC状态

---

**恭喜！你已经成功实现了ESP32的RTC状态恢复功能！** 🎉

系统现在能够在看门狗复位后自动恢复运行位置，大大提升了可靠性和连续性！