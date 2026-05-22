---
name: Bug Report / 问题报告
about: Report a bug to help us improve / 反馈问题以帮助改进
title: "[Bug] "
labels: bug
assignees: ''
---

<!--
注意: 请使用下面提供的模板填写，中英文均可。
NOTE: Please use the template below. Chinese or English both welcome.
-->

## 环境信息 / Environment

| 项目 / Item  | 值 / Value |
|--------------|------------|
| 芯片 / Chip  | BL618 / BL616 |
| 板型 / Board | AiPi-Eyes-DU / BL618DK |
| SDK 版本 / SDK Version | <!-- e.g., bouffalo_sdk v2.0.0-xxx --> |
| 固件版本 / Firmware Version | <!-- e.g., v1.0.0 --> |
| 构建配置 / Build Config | `make CHIP=bl616 BOARD=bl616dk` (或自定义 defconfig) |
| 操作系统 / OS | Linux / WSL / Windows / macOS |

## 问题描述 / Describe the Bug

<!-- 清晰描述问题是什么 -->
<!-- A clear and concise description of what the bug is. -->

## 复现步骤 / To Reproduce

复现步骤 / Steps to reproduce the behavior:

1. 连接硬件: <!-- e.g., 串口连接，波特率115200 -->
2. 执行命令: <!-- e.g., 发送 '...' 指令 -->
3. 观察到: <!-- e.g., 设备重启 / 无响应 / 输出错误 -->

## 预期行为 / Expected Behavior

<!-- 预期应该发生什么 -->
<!-- What you expected to happen. -->

## 实际行为 / Actual Behavior

<!-- 实际发生了什么 -->
<!-- What actually happened. -->

## 日志输出 / Log Output

<!-- 请附上完整串口日志（至少包含启动日志和问题发生前后的输出） -->
<!-- Please attach full serial log (at least boot log + output before/after the issue). -->

```
在此粘贴日志 / Paste log here
```

## 硬件连接信息 / Hardware Connection

- 串口设备: `/dev/ttyUSB0` (或 Windows COM 口)
- 波特率: 115200 / 2000000
- 外设连接: <!-- 是否有额外 GPIO 外接设备？传感器/舵机等 -->

## 附加说明 / Additional Context

<!-- 其他相关信息，如是否使用了自定义 defconfig、是否修改了源码等 -->
<!-- Any other context, e.g., custom defconfig, modified source code, etc. -->
