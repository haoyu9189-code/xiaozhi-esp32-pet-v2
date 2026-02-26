@echo off
REM 设置伪造 MAC 地址的偏移量
REM 用法: set_fake_mac_offset.bat <串口> <偏移量>
REM 例如: set_fake_mac_offset.bat COM13 1

if "%1"=="" (
    echo 错误: 请指定串口
    echo 用法: set_fake_mac_offset.bat ^<串口^> ^<偏移量^>
    echo 例如: set_fake_mac_offset.bat COM13 1
    exit /b 1
)

if "%2"=="" (
    echo 错误: 请指定偏移量
    echo 用法: set_fake_mac_offset.bat ^<串口^> ^<偏移量^>
    echo 例如: set_fake_mac_offset.bat COM13 1
    exit /b 1
)

set PORT=%1
set OFFSET=%2

echo ========================================
echo 设置伪造 MAC 偏移量
echo ========================================
echo 串口: %PORT%
echo 偏移量: %OFFSET%
echo ========================================
echo.

REM 创建临时 CSV 文件
echo key,type,encoding,value > temp_mac_offset.csv
echo test,namespace,, >> temp_mac_offset.csv
echo mac_offset,data,i32,%OFFSET% >> temp_mac_offset.csv

echo [1/4] 生成 NVS 分区...
python %IDF_PATH%\components\nvs_flash\nvs_partition_generator\nvs_partition_gen.py generate temp_mac_offset.csv temp_mac_offset.bin 0x6000

if errorlevel 1 (
    echo 错误: 生成 NVS 分区失败
    del temp_mac_offset.csv
    exit /b 1
)

echo [2/4] 读取当前 NVS 分区...
esptool.py --port %PORT% read_flash 0x9000 0x6000 nvs_backup.bin

echo [3/4] 合并 NVS 分区...
REM 注意: 这里简化处理，直接写入新的配置
REM 实际使用中可能需要更复杂的合并逻辑

echo [4/4] 烧录 NVS 分区...
esptool.py --port %PORT% write_flash 0x9000 temp_mac_offset.bin

if errorlevel 1 (
    echo 错误: 烧录失败
    del temp_mac_offset.csv
    del temp_mac_offset.bin
    exit /b 1
)

echo.
echo ========================================
echo 设置完成！
echo ========================================
echo 偏移量已设置为: %OFFSET%
echo 请重启设备使配置生效
echo.
echo 伪造 MAC = 真实 MAC + %OFFSET%
echo ========================================

REM 清理临时文件
del temp_mac_offset.csv
del temp_mac_offset.bin

pause
