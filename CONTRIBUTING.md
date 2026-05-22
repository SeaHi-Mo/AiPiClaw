# 贡献指南 | Contributing Guide

感谢你对 AiPiClaw 的关注！本项目是 **BL618 上的 AI Agent 固件**，由安信可科技（AI-Thinker）社区维护。欢迎提交 Issue、PR 或加入 QQ 群交流。

---

## 一、开发环境搭建

### 推荐环境

- **操作系统**: WSL2 (Ubuntu 22.04/24.04) 或原生 Linux
- **工具链**: RISC-V GCC (T-Head 版本)
- **SDK**: Bouffalo SDK (子模块)

### 快速开始

```bash
git clone --recursive git@github.com:SeaHi-Mo/AiPiClaw.git
cd AiPiClaw
./setup_toolchain.sh                      # 安装工具链
export BL_SDK_BASE=$(pwd)/os
export PATH=$(pwd)/../toolchain/bin:$PATH
make menuconfig CHIP=bl616 BOARD=bl616dk   # 首次需生成配置
make CHIP=bl616 BOARD=bl616dk              # 编译
```

详细说明请参考 [README.md](./README.md) 的环境搭建章节。

## 二、代码规范

本项目遵循 **Bouffalo SDK 代码风格**，关键约定如下：

### 命名规范

| 类别 | 规范 | 示例 |
|------|------|------|
| 变量/函数 | snake_case | `wifi_connect()`、`agent_loop()` |
| 宏/常量 | UPPER_SNAKE_CASE | `MAX_BUF_SIZE`、`CONFIG_LLM_MODEL` |
| 类型定义 | snake_case + `_t` 后缀 | `agent_ctx_t`、`wifi_config_t` |
| 文件命名 | snake_case | `wifi_onboard.c`、`llm_agent.c` |

### 格式要求

- **缩进**: 4 空格（不使用 Tab）
- **行宽**: 不超过 100 字符
- **大括号**: K&R 风格（左大括号不换行）
- **头文件**: 使用 `#pragma once` 而非传统头文件卫士
- **注释**: 功能说明用中文，API 注释用 Doxygen 风格（英文）

### 示例

```c
/**
 * @brief 连接 WiFi 热点
 * @param ssid      SSID 字符串
 * @param password  密码字符串
 * @return 0 成功，负值失败
 */
int wifi_connect(const char *ssid, const char *password)
{
    if (!ssid || !password) {
        return -1;
    }
    // ... 实现逻辑
}
```

### 禁止事项

- ❌ 不要使用全局变量（除非必要，如中断上下文）
- ❌ 不要使用 `malloc`/`free` 以外的动态内存分配
- ❌ 不要在中断回调中执行阻塞操作
- ❌ 不要硬编码 GPIO 编号（使用别名宏）

## 三、分支命名规范

| 分支类型 | 格式 | 示例 |
|----------|------|------|
| 功能开发 | `feature/<描述>` | `feature/ota-update` |
| 缺陷修复 | `bugfix/<描述>` | `bugfix/wifi-reconnect-crash` |
| 重构 | `refactor/<描述>` | `refactor/message-bus` |
| 文档 | `docs/<描述>` | `docs/api-reference` |
| 发布 | `release/<版本>` | `release/v1.0.0` |

> ⚠️ 注意: `release/` 分支由维护者管理，请不要直接向 `release/` 分支提交非模板化的 PR。

## 四、提交信息格式

提交信息使用 **Conventional Commits** 规范：

```
<type>(<scope>): <简短描述>

<详细说明（可选）>
```

### Type 类型

| Type | 含义 |
|------|------|
| `feat` | 新功能 |
| `fix` | 缺陷修复 |
| `refactor` | 代码重构 |
| `docs` | 文档变更 |
| `chore` | 构建、工具、配置等杂项 |
| `test` | 测试相关 |
| `perf` | 性能优化 |
| `style` | 格式化（非逻辑变更） |

### 示例

```
feat(llm): 支持 DeepSeek 流式推理

- 新增 stream_request 接口
- 支持 SSE 分帧解析
- 添加超时重连机制
```

```
fix(wifi): 修复重连时的内存泄漏

WiFi 重连时 JSON 解析器未正确释放，累计 4 次后 OOM。
用静态缓冲区替代动态分配，规避碎片化问题。
```

## 五、PR 流程

### 提交流程

1. **Fork 仓库** 并切换到目的分支
2. 从分支起点创建**功能分支**（见上方分支命名规范）
3. 完成编码后，确保编译通过：
   ```bash
   make clean && make CHIP=bl616 BOARD=bl616dk
   ```
4. 确保没有新增编译警告
5. **压缩提交**（Squash）为少量有意义的提交
6. 提 PR 到 `release/*` 或 `main`（功能分支合入对应开发分支）
7. 等待 Code Review

### Squash Merge

本项目使用 **Squash Merge** 策略。所有 PR 在合入前会被压缩为一个提交，并按照 Conventional Commits 格式改写提交信息。

### Review 检查项

- [ ] 编译无误，无新增 warning
- [ ] 代码风格符合本贡献指南
- [ ] 提交信息格式正确
- [ ] 已添加必要注释（尤其是新 API）
- [ ] 不引入新的全局变量
- [ ] 不破坏现有功能（尽量附测试结果）

## 六、Issue 规范

- **Bug 报告**: 使用 [Bug Report 模板](./.github/ISSUE_TEMPLATE/bug_report.md)，填写完整环境信息
- **功能请求**: 使用 [Feature Request 模板](./.github/ISSUE_TEMPLATE/feature_request.md)，说明使用场景和预期效果
- **提问**: 建议先搜索已有 Issue 和 QQ 群聊天记录

## 七、社区

如有疑问或需要帮助，欢迎加入 QQ 交流群：

- **QQ 群**: [983111990](https://qm.qq.com/q/983111990)

进群请备注 **AiPiClaw**。

---

再次感谢你的贡献！🎉
