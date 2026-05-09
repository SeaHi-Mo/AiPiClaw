# AiPiClaw

**BL618 上的 AI Agent 固件** — 智能对话 + 硬件控制

基于 AI-Thinker [mimiclaw](https://github.com/SeaHi-Mo/AiPiClaw) (ESP32-S3) 移植到 Bouffalo BL618 平台。

## 硬件要求

| 项目 | 规格 |
|------|------|
| 开发板 | AiPi-Eyes-DU / 任何 BL616/BL618 开发板 |
| 芯片 | BL616 / BL618 (RISC-V E907 @ 480MHz) |
| 内存 | 512KB SRAM + 4MB PSRAM |
| Flash | 4MB |
| 串口 | UART0 @ 115200 baud (默认) |
| WiFi | 2.4GHz 802.11 b/g/n/ax (WiFi 6) |

## 环境搭建

### 1. 克隆仓库（含子模块）

```bash
git clone --recursive git@github.com:SeaHi-Mo/AiPiClaw.git
cd AiPiClaw
```

如果已克隆但未拉子模块：

```bash
git submodule update --init --recursive
```

### 2. 安装工具链

```bash
./setup_toolchain.sh
```

脚本会自动检测操作系统，克隆对应的 RISC-V 工具链到 `toolchain/` 目录。

- **Linux**: 从 GitHub 克隆 `toolchain_gcc_t-head_linux`
- **Windows** (MINGW/MSYS): 从 GitHub 克隆 `toolchain_gcc_t-head_windows`
- **macOS**: 参考脚本提示的构建指南手动安装

### 3. 设置 SDK

SDK 已作为子模块在 `os/` 目录，无需额外下载。

## 编译

```bash
cd AiPiClaw
export BL_SDK_BASE=$(pwd)/os
export PATH=$(pwd)/../toolchain/bin:$PATH
make CHIP=bl616 BOARD=bl616dk
```

首次编译需先执行 `make menuconfig CHIP=bl616 BOARD=bl616dk` 生成 SDK 配置。项目已提供 `defconfig`，可手动导入。

编译产物位于 `build/build_out/`：
- `AiPiClaw_bl616.bin` — 固件镜像
- `AiPiClaw_bl616.elf` — 调试符号
- `AiPiClaw_bl616.map` — 内存映射

## 烧录

### Linux

```bash
./flash.sh /dev/ttyUSB0 2000000
```

### Windows

```bash
flash.bat
```

或使用 Bouffalo Lab 的 BLFlashCommand：

```bash
BLFlashCommand-ubuntu \
    --interface uart --port /dev/ttyUSB0 --chipname bl616 --baudrate 2000000 \
    --firmware build/build_out/AiPiClaw_bl616.bin write_flash_files
```

### 烧录后

- 按下 RST 键重启进入固件
- 串口输出启动日志，确认模块初始化成功

## 快速使用

### 1. 连接 WiFi

```bash
# 串口输入:
wifi_set <SSID> <PASSWORD>

# 查看连接状态:
wifi_status
```

### 2. 配置 LLM API

```bash
llm_key <your-api-key>
llm_model <model-name>
llm_provider <anthropic|openai|minimax>
```

### 3. 开始对话

```bash
mimi 你好
mimi 现在几点了
mimi 把 GPIO17 拉高
```

### 4. Web 界面

WiFi 连接成功后，浏览器访问：

```
http://<IP>:18789
```

支持 WebSocket 实时聊天。

## 默认配置

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| LLM 模型 | `claude-opus-4-5` | Anthropic (上电可改) |
| LLM 提供商 | `anthropic` | 支持 openai / minimax |
| Web 端口 | 18789 | HTTP + WebSocket |
| WebSocket 客户端 | 4 | 最大并发连接 |
| 消息总线队列 | 16 | 三级优先级 |
| Agent 栈 | 24KB | LLM 调用线程 |
| Shell 栈 | 4KB | CLI 命令处理 |
| 日志级别 | INFO | 通过 defconfig 控制 |
| 时区 | PST8PDT | POSIX 格式 |

## 项目结构

```
AiPiClaw/
├── AiPiClaw/          # 程序文件
│   ├── main.c         # 主入口
│   ├── src/           # 核心源码
│   │   ├── agent/     # 智能体主循环
│   │   ├── llm/       # LLM API 代理
│   │   ├── tools/     # 工具注册表 (13 个工具)
│   │   ├── gateway/   # WebSocket / HTTP / Bot
│   │   ├── platform/  # 硬件抽象层
│   │   ├── bus/       # 消息总线 (三级优先)
│   │   ├── wifi/      # WiFi 管理器
│   │   └── ...
│   ├── CMakeLists.txt # 编译配置
│   └── defconfig      # SDK 配置
├── os/                # Bouffalo SDK (子模块)
└── toolchain/         # RISC-V 工具链
```

## 架构概览

```
串口 CLI ───→ 消息总线 ──→ Agent Loop ──→ LLM API (云端)
Web :18789 ──→          │                → Tool Registry
Telegram ────→           │                → GPIO / WiFi / 搜索
                         └──→ 回复输出 (串口/Web/Bot)
```

## 工具列表

| 工具 | 说明 |
|------|------|
| `web_search` | 网络搜索 (Tavily/Brave) |
| `get_current_time` | 获取当前时间 |
| `file_read` | 读取文件 |
| `file_write` | 写入文件 |
| `file_append` | 追加文件 |
| `file_delete` | 删除文件 |
| `list_dir` | 列出目录 |
| `gpio_write` | 设置 GPIO 输出 |
| `gpio_read` | 读取 GPIO 输入 |
| `gpio_read_all` | 读取所有 GPIO |
| `cron_add` | 添加定时任务 |
| `cron_list` | 列出定时任务 |
| `cron_remove` | 删除定时任务 |

## License

本项目基于 AI-Thinker mimiclaw 移植。SDK (os/) 使用 Bouffalo Lab 的 bouffalo_sdk，遵循其原始许可证。
