#!/bin/bash
# ============================================================================
# setup_toolchain.sh — AiPiClaw 工具链安装脚本
# 自动检测平台并克隆对应 RISC-V 工具链
# 用法: ./setup_toolchain.sh
# 依赖: git
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TOOLCHAIN_DIR="$SCRIPT_DIR/toolchain"

LINUX_TCH_URL="https://github.com/bouffalolab/toolchain_gcc_t-head_linux.git"
WINDOWS_TCH_URL="https://github.com/bouffalolab/toolchain_gcc_t-head_windows.git"
MACOS_BUILD_REF="https://github.com/p4ddy1/pine_ox64/blob/main/build_toolchain_macos.md"

# ── 颜色 ──────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'
info()  { echo -e "${CYAN}[INFO]${NC}  $1"; }
ok()    { echo -e "${GREEN}[OK]${NC}   $1"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
err()   { echo -e "${RED}[ERR]${NC}  $1"; }

# ── 平台检测 ──────────────────
detect_os() {
    case "$(uname -s)" in
        Linux*)          echo "linux" ;;
        Darwin*)         echo "macos" ;;
        MINGW*|MSYS*)    echo "windows" ;;
        *)               echo "unknown" ;;
    esac
}

# ── 幂等跳过 ──────────────────
if [ -d "$TOOLCHAIN_DIR" ] && [ -n "$(ls -A "$TOOLCHAIN_DIR" 2>/dev/null)" ]; then
    ok "toolchain/ 已存在，跳过。如需重装请先删除 toolchain/ 目录。"
    exit 0
fi

# ── 检测平台 ──────────────────
OS="$(detect_os)"
info "检测到操作系统: ${OS}"

case "$OS" in
    unknown)
        err "未知平台: $(uname -s)，仅支持 Linux / macOS / Windows (MINGW/MSYS)"
        exit 1
        ;;
    linux)
        info "克隆 Linux RISC-V 工具链 (约 2.5 GB，视网速需 5~30 分钟)..."
        git clone --depth 1 "$LINUX_TCH_URL" "$TOOLCHAIN_DIR"
        ok "Linux 工具链安装完成: $TOOLCHAIN_DIR"
        ;;
    windows)
        info "克隆 Windows RISC-V 工具链 (约 1.8 GB)..."
        git clone --depth 1 "$WINDOWS_TCH_URL" "$TOOLCHAIN_DIR"
        ok "Windows 工具链安装完成: $TOOLCHAIN_DIR"
        ;;
    macos)
        warn "macOS 无官方预构建工具链，请参考以下说明自行构建："
        echo ""
        echo "   构建指南: $MACOS_BUILD_REF"
        echo "   构建完成后将工具链放到: $TOOLCHAIN_DIR"
        echo ""
        info "创建空 toolchain/ 目录占位..."
        mkdir -p "$TOOLCHAIN_DIR"
        warn "macOS 工具链未自动安装，请在构建完成后再次运行本脚本验证。"
        ;;
esac

# ── 验证 ──────────────────────
echo ""
GCC="$(find "$TOOLCHAIN_DIR" -name 'riscv*-gcc' -type f 2>/dev/null | head -1 || true)"
if [ -n "$GCC" ] && [ -x "$GCC" ]; then
    ok "工具链就绪: $GCC"
    info "版本: $("$GCC" --version 2>/dev/null | head -1)"
else
    warn "未找到 riscv gcc。如果是 macOS 请安装完成后重试。"
fi

echo ""
echo "=============================================="
echo "  完成！"
echo "=============================================="
echo ""
echo "后续步骤:"
echo "  export PATH=\"\$PATH:$TOOLCHAIN_DIR/bin\""
echo "  export BL_SDK_BASE=\"\$SCRIPT_DIR/os\""
echo "  cd AiPiClaw && make CHIP=bl616 BOARD=bl616dk"
