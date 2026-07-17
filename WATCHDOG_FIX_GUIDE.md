# ESP32看门狗自动复位问题修复指南

## 问题描述
程序卡死时，看门狗没有成功重启单片机，导致系统永久停止响应。

## 根本原因
**sdkconfig中看门狗配置错误**：`CONFIG_ESP_TASK_WDT_PANIC=y`

这个配置导致看门狗超时时只打印错误信息（panic），而**不会自动复位系统**。

## 快速修复（3步完成）

### 方法1：使用自动修复脚本（推荐）⭐

在项目根目录运行PowerShell脚本：

```powershell
.\fix_watchdog_config.ps1
```

然后重新编译和烧录：

```bash
idf.py fullclean
idf.py build
idf.py flash monitor
```

### 方法2：手动修改配置

#### 步骤1：打开配置菜单
```bash
idf.py menuconfig
```

#### 步骤2：导航到看门狗配置
```
Component config → 
  ESP System Settings → 
    Task Watchdog Timer
```

#### 步骤3：修改以下选项
- ❌ **取消勾选** "Invoke panic handler on timeout"
- ✅ **勾选** "Reset CPU on timeout"
- 设置超时时间为 **5秒**

#### 步骤4：保存并退出
按 `S` 保存，按 `Q` 退出

#### 步骤5：重新编译和烧录
```bash
idf.py fullclean
idf.py build
idf.py flash monitor
```

## 验证修复效果

烧录后观察串口输出，应该看到：

```
Initializing Task Watchdog Timer...
Task watchdog monitoring enabled for main task
Watchdog timeout: 5 seconds
Watchdog will reset CPU on timeout (correct configuration)
```

✅ **如果看到以上输出，说明配置正确！**

❌ **如果看到警告信息：**
```
[WARNING] Watchdog is configured to PANIC instead of RESET!
```
说明配置仍未修正，请重新检查。

## 代码层面的改进

本次修复还在代码中添加了以下功能：

### 1. 配置检查警告
程序启动时会检测看门狗配置，如果配置错误会显示警告信息。

### 2. 紧急复位机制
当检测到连续失败超过100次（约1秒）时，会自动触发系统复位：
```c
if (consecutive_failures > MAX_CONSECUTIVE_FAILURES) {
    printf("[CRITICAL] Triggering system reset...\n");
    esp_restart();
}
```

### 3. 资源监控
定期监控栈空间和堆空间，在资源不足时发出警告：
- 栈空间 < 2KB：发出严重警告
- 堆空间 < 10KB：发出严重警告

### 4. 增强的诊断输出
每10秒输出一次系统状态：
```
[DIAG] Loop=1000, Elapsed=10 s, Stack=xxxx words, FreeHeap=xxxxx
[DIAG] Max operation times: I2C=xxx us, TCP=xxx us, SPI=xxx us
[DIAG] TCP connected=1, Relay cycles=xx, Paused=0, Failures=0
```

## 测试建议

修复后建议进行以下测试：

1. **长时间运行测试**：运行至少200个周期（约53分钟）
2. **网络异常测试**：手动断开WiFi，观察重连机制
3. **压力测试**：高频UART数据接收 + 频繁按键操作
4. **卡死恢复测试**：故意制造阻塞场景，验证5秒后是否自动复位

## 常见问题

### Q: 为什么之前没有发现这个问题？
A: 因为ESP-IDF默认配置是PANIC模式，适合开发调试。生产环境必须改为RESET模式。

### Q: 修改配置后需要重新编译吗？
A: 是的，必须执行 `idf.py fullclean && idf.py build`。

### Q: 超时时间设置多少合适？
A: 
- 开发阶段：10-30秒（便于调试）
- 生产环境：5-10秒（快速恢复）

### Q: 如何查看上次复位的原因？
A: 在代码中添加：
```c
esp_reset_reason_t reason = esp_reset_reason();
printf("Last reset reason: %d\n", reason);
```

## 技术细节

### 错误的配置
```
CONFIG_ESP_TASK_WDT_PANIC=y      # ❌ 仅panic，不复位
CONFIG_ESP_TASK_WDT_RESET_CPU=n  # ❌ 未启用CPU复位
```

### 正确的配置
```
CONFIG_ESP_TASK_WDT_PANIC=n      # ✅ 不进入panic
CONFIG_ESP_TASK_WDT_RESET_CPU=y  # ✅ 超时后复位CPU
CONFIG_ESP_TASK_WDT_TIMEOUT_S=5  # ✅ 5秒超时
```

## 相关文件

- `main/main.c` - 主程序（已增强看门狗管理）
- `sdkconfig` - 项目配置文件（需要修改）
- `fix_watchdog_config.ps1` - 自动修复脚本

## 参考资料

- [ESP-IDF Task Watchdog Timer文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/wdts.html)
- [esp_task_wdt API参考](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/wdts.html#task-watchdog)

---

**修复完成后，系统在卡死时会在5秒后自动复位，确保设备能够自我恢复！** 🎉