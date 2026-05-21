#!/bin/bash
# flash.sh - MimiClaw BL618 串口烧录脚本
# 作者: 安信可科技有限公司
# 使用: ./flash.sh [/dev/ttyUSB0] [2000000]

set -e

SDK_BASE="${BL_SDK_BASE:-/home/seahi/workspase/AiPiClaw/os}"
FLASH_TOOL="${SDK_BASE}/tools/bflb_tools/bouffalo_flash_cube/BLFlashCommand-ubuntu"
FIRMWARE="build/build_out/AiPiClaw_bl616.bin"

PORT="${1:-/dev/ttyUSB0}"
BAUD="${2:-2000000}"
CHIP="bl616"

echo "========================================"
echo "  MimiClaw BL618 烧录脚本"
echo "========================================"
echo "  固件: ${FIRMWARE}"
echo "  端口: ${PORT}"
echo "  波特率: ${BAUD}"
echo "  芯片: ${CHIP}"
echo "========================================"

if [ ! -f "${FIRMWARE}" ]; then
    echo "错误: 找不到固件文件 ${FIRMWARE}"
    echo "请先执行 make 编译通过"
    exit 1
fi

if [ ! -f "${FLASH_TOOL}" ]; then
    echo "错误: 找不到烧录工具 ${FLASH_TOOL}"
    exit 1
fi

echo ""
echo "正在烧录固件，请确保开发板已进入烧录模式 (BOOT + RST)..."
echo ""

${FLASH_TOOL} \
    --chipname ${CHIP} \
    --port ${PORT} \
    --baudrate ${BAUD} \
    write_flash_files \
    0x0000 "${FIRMWARE}"

echo ""
echo "烧录完成! 请按下 RST 复位按键重启开发板。"
echo "========================================"
