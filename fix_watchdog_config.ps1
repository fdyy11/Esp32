# ESP32看门狗配置修复脚本
# 用途：自动修复sdkconfig中的看门狗配置，确保超时时能自动复位系统

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "ESP32 Watchdog Configuration Fix Tool" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$sdkconfigPath = "sdkconfig"

# 检查文件是否存在
if (-Not (Test-Path $sdkconfigPath)) {
    Write-Host "Error: sdkconfig file not found!" -ForegroundColor Red
    Write-Host "Please run this script from the project root directory." -ForegroundColor Yellow
    exit 1
}

Write-Host "Current watchdog configuration:" -ForegroundColor Green
findstr /C:"CONFIG_ESP_TASK_WDT" $sdkconfigPath
Write-Host ""

# 备份原文件
$backupPath = "sdkconfig.backup.$(Get-Date -Format 'yyyyMMdd_HHmmss')"
Copy-Item $sdkconfigPath $backupPath
Write-Host "Backup created: $backupPath" -ForegroundColor Yellow
Write-Host ""

# 读取文件内容
$content = Get-Content $sdkconfigPath

# 修复配置
$modified = $false

# 1. 禁用PANIC模式
if ($content -match 'CONFIG_ESP_TASK_WDT_PANIC=y') {
    $content = $content -replace 'CONFIG_ESP_TASK_WDT_PANIC=y', 'CONFIG_ESP_TASK_WDT_PANIC=n'
    Write-Host "[FIX] Changed CONFIG_ESP_TASK_WDT_PANIC=y to n" -ForegroundColor Green
    $modified = $true
} else {
    Write-Host "[OK] CONFIG_ESP_TASK_WDT_PANIC already set to n" -ForegroundColor Cyan
}

# 2. 启用CPU复位
if ($content -notmatch 'CONFIG_ESP_TASK_WDT_RESET_CPU=y') {
    # 如果存在但设置为n，则改为y
    if ($content -match 'CONFIG_ESP_TASK_WDT_RESET_CPU=n') {
        $content = $content -replace 'CONFIG_ESP_TASK_WDT_RESET_CPU=n', 'CONFIG_ESP_TASK_WDT_RESET_CPU=y'
        Write-Host "[FIX] Changed CONFIG_ESP_TASK_WDT_RESET_CPU=n to y" -ForegroundColor Green
        $modified = $true
    } else {
        # 如果不存在，添加该配置
        $content += "`nCONFIG_ESP_TASK_WDT_RESET_CPU=y"
        Write-Host "[FIX] Added CONFIG_ESP_TASK_WDT_RESET_CPU=y" -ForegroundColor Green
        $modified = $true
    }
} else {
    Write-Host "[OK] CONFIG_ESP_TASK_WDT_RESET_CPU already set to y" -ForegroundColor Cyan
}

# 保存修改后的文件
if ($modified) {
    Set-Content -Path $sdkconfigPath -Value $content -Encoding UTF8
    Write-Host ""
    Write-Host "Configuration saved successfully!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Next steps:" -ForegroundColor Yellow
    Write-Host "1. Run: idf.py fullclean" -ForegroundColor White
    Write-Host "2. Run: idf.py build" -ForegroundColor White
    Write-Host "3. Run: idf.py flash monitor" -ForegroundColor White
    Write-Host ""
    Write-Host "After flashing, you should see:" -ForegroundColor Cyan
    Write-Host "  'Watchdog will reset CPU on timeout (correct configuration)'" -ForegroundColor White
} else {
    Write-Host ""
    Write-Host "No changes needed. Configuration is already correct." -ForegroundColor Green
}

Write-Host ""
Write-Host "Updated watchdog configuration:" -ForegroundColor Green
findstr /C:"CONFIG_ESP_TASK_WDT" $sdkconfigPath
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan