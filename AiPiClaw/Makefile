SDK_DEMO_PATH ?= $(abspath .)
BL_SDK_BASE ?= $(abspath $(SDK_DEMO_PATH)/../os)

export BL_SDK_BASE

# 安信可科技定制：设置RISC-V工具链路径
TOOLCHAIN_PATH ?= $(abspath $(SDK_DEMO_PATH)/../toolchain/bin)

# 将工具链添加到PATH
export PATH := $(TOOLCHAIN_PATH):$(PATH)

CROSS_COMPILE ?= riscv64-unknown-elf-

include $(BL_SDK_BASE)/project.build
