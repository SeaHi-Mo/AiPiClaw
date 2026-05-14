# AiPiClaw — BL618 AI 助手固件

> **安信可科技 (AI-Thinker)** · 嵌入式 LLM Agent 固件  
> 版本: v1.0.0 · 芯片: BL618 (RISC-V E907 @ 480MHz)  
> 板卡: AiPi-Eyes-DU

---

## 1. 简介

AiPiClaw 是运行在 AiPi-Eyes-DU 开发板上的 AI 助手固件，基于 Bouffalo SDK 和 FreeRTOS。通过 MiniMax M2.5 大模型提供自然语言对话、GPIO 硬件控制、Web UI 交互、串口 CLI 等功能。

## 2. 硬件要求

| 组件 | 规格 |
|------|------|
| 开发板 | AiPi-Eyes-DU |
| 芯片 | Bouffalo BL618 (RISC-V E907 @ 480MHz) |
| SRAM | 512KB |
| PSRAM | 4MB |
| Flash | 4MB |
| WiFi | 2.4GHz b/g/n, 板载 PCB 天线 |
| LED | GPIO12=红, GPIO14=绿, GPIO15=蓝 (高电平有效) |
| 按键 | GPIO10=KEY0, GPIO11=KEY1 |
| 调试串口 | UART0 (GPIO21/22, 115200-8N1) |

## 3. 功能特性

- **LLM 对话** — MiniMax M2.5 Anthropic 兼容 API，多轮上下文记忆
- **GPIO 控制** — 红灯/绿灯/蓝灯，支持中文别名（"打开绿灯"）
- **Web UI** — WebSocket 实时通信，自动连接
- **串口 CLI** — `mimi hello` 交互式命令行
- **WiFi 配网** — STA 模式连接，自动重连
- **定时任务** — cron 服务 (EVERY / AT 模式)
- **OTA 升级** — HTTPS FOTA 固件更新
- **上下文记忆** — 跨轮对话记忆，身份识别
- **文件系统** — LittleFS 文件读写

## 4. 编译

```bash
# 工具链安装
cd AiPiClaw && ./setup_toolchain.sh

# 编译
cd AiPiClaw/AiPiClaw
make -s CHIP=bl616 BOARD=bl616dk
```

## 5. 烧录

按住 BOOT → 按 RST → 松开 BOOT 进入烧录模式：

```bash
make flash CHIP=bl616 COMX=/dev/ttyUSB0
```

## 6. 使用

**串口 CLI** (115200-8N1):
```bash
mimi hello
wifi_set <SSID> <密码>
```

**Web UI**: 浏览器访问 `http://<设备IP>`

**GPIO 控制**: 
```
打开绿灯 / 关闭红灯 / 灯的状态是什么？
```

## 7. 架构

```
CLI / Web / Telegram / Feishu → 消息总线 → Agent Loop → LLM Proxy (MiniMax M2.5)
                                              ↓
                                        Tool Registry (14 工具)
                                        ├── GPIO (write/read/write_named/read_named)
                                        ├── File (read/write/edit/list)
                                        ├── Web Search, Time, Cron
                                        └── Context Memory
```

## 8. 许可证

MIT License. Copyright (c) 2026 安信可科技.
