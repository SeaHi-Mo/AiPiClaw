@echo off
chcp 65001 >nul
REM flash.bat - MimiClaw BL618 Windows 串口烧录脚本
REM 作者: 安信可科技有限公司
REM 使用: flash.bat [COM3] [2000000]

set SDK_BASE=%BL_SDK_BASE:C:\Users\Seahi\workspase\BL618Claw\bouffalo_sdk%
if "%SDK_BASE%"=="" set SDK_BASE=C:\Users\Seahi\workspase\BL618Claw\bouffalo_sdk

set FLASH_TOOL=%SDK_BASE%\tools\bflb_tools\bouffalo_flash_cube\BLFlashCommand.exe
set FIRMWARE=build\build_out\mimiclaw_bl618_port_bl616.bin

set PORT=%1
if "%PORT%"=="" set PORT=COM3

set BAUD=%2
if "%BAUD%"=="" set BAUD=2000000

set CHIP=bl616

echo ========================================
echo   MimiClaw BL618 烧录脚本 (Windows)
echo ========================================
echo   固件: %FIRMWARE%
echo   端口: %PORT%
echo   波特率: %BAUD%
echo   芯片: %CHIP%
echo ========================================

if not exist "%FIRMWARE%" (
    echo 错误: 找不到固件文件 %FIRMWARE%
    echo 请先执行 make 编译通过
    exit /b 1
)

if not exist "%FLASH_TOOL%" (
    echo 错误: 找不到烧录工具 %FLASH_TOOL%
    exit /b 1
)

echo.
echo 正在烧录固件，请确保开发板已进入烧录模式 (BOOT + RST)...
echo.

"%FLASH_TOOL%" --chipname %CHIP% --port %PORT% --baudrate %BAUD% write_flash_files 0x0000 "%FIRMWARE%"

echo.
echo 烧录完成! 请按下 RST 复位按键重启开发板。
echo ========================================
pause
