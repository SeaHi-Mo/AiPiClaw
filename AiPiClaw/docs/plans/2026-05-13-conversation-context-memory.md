# 多轮对话上下文记忆 实施计划

> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task.

**Goal:** AiPiClaw 支持多轮对话上下文 — 用户说"打开所有灯"后说"关掉吧"，LLM 能理解"吧"指代"所有灯"。

**Architecture:** 扩展现有 `axk_session_mgr`（已具备 8 槽位、LRU、easyflash 持久化、互斥锁），将其 `context` 字段从 512B 扩展到 16KB，新增 messages 序列化/反序列化。`axk_agent_loop.c` 中 `messages` 数组生命周期从"每轮新建→用完即弃"改为"从 session_mgr 恢复→追加→LLM 调用→截断→保存回 session_mgr"。

**Tech Stack:** C (Bouffalo SDK, FreeRTOS, cJSON, easyflash), BL618 RISC-V

---

## 前置说明

### 根因
`src/agent/axk_agent_loop.c` 第 490 行每轮创建全新 `messages = cJSON_CreateArray()`，第 543 行 `cJSON_Delete(messages)` 销毁。跨轮对话历史全部丢失。

### 需改的文件（共 3 个）
| 文件 | 变更量 | 说明 |
|------|--------|------|
| `src/agent/axk_agent_loop.c` | ~60行改 | messages 生命周期重构 |
| `src/memory/axk_session_mgr.h` | ~20行改 | 接口扩展 |
| `src/memory/axk_session_mgr.c` | ~80行改 | messages 序列化/反序列化 |

不修改的文件：`axk_llm_proxy.c/.h`（消息格式不变）、`axk_message_bus.c/.h`（不关心上下文）、`mimi_config.h`（现有常量已够用）

### 常量复用
- `MIMI_SESSION_MAX_MSGS=20` — 每会话消息条数上限
- `MIMI_CONTEXT_BUF_SIZE=16KB` — JSON 序列化缓冲区上限
- `AXK_MAX_SESSIONS=8` — 并发会话数（已存在）

---

## Task 1: 扩展 session_mgr 接口 — 新增 messages 存取 API

**Objective:** 给 `axk_session_mgr.h/.c` 增加按 session_id 存取 messages cJSON 数组的接口

**Files:**
- Modify: `src/memory/axk_session_mgr.h`
- Modify: `src/memory/axk_session_mgr.c`

### Step 1: 扩展头文件 — 新增 API 声明

在 `axk_session_mgr.h` 中追加以下声明：

```c
/* ── 会话上下文（多轮对话记忆） ───────────────── */

/**
 * @brief 从 session_mgr 恢复会话的 messages 历史
 *
 * @param session_id  会话标识符，格式 "<channel>:<chat_id>"（如 "websocket:client1"）
 * @return cJSON Array，失败或首轮对话返回新建的空数组（调用者负责 cJSON_Delete）
 */
cJSON *axk_session_load_messages(const char *session_id);

/**
 * @brief 将 messages 历史保存到 session_mgr（内部序列化 + easyflash 持久化）
 *
 * @param session_id  会话标识符
 * @param messages    cJSON Array（Anthropic content blocks 格式的 messages 数组）
 * @return 0 成功，-1 失败
 */
int axk_session_save_messages(const char *session_id, cJSON *messages);

/**
 * @brief 清理超过 idle_ticks 未活动的过期会话
 *
 * @param idle_ticks  FreeRTOS tick 值，last_active 早于此值的会话被清理
 */
void axk_session_cleanup_stale(uint32_t idle_ticks);
```

### Step 2: 调整 session_entry_t — context 字段从定长改为 PSRAM 动态分配

在 `axk_session_mgr.c` 中，将：

```c
typedef struct {
    char id[AXK_SESSION_ID_LEN];
    char context[AXK_SESSION_CTX_LEN];  // 当前 512B
    uint32_t last_active;
} session_entry_t;
```

改为：

```c
typedef struct {
    char id[AXK_SESSION_ID_LEN];
    char *context;             // PSRAM 动态分配，大小 MIMI_CONTEXT_BUF_SIZE
    size_t context_len;        // 实际使用长度
    uint32_t last_active;
} session_entry_t;
```

### Step 3: 确保 AXK_SESSION_ID_LEN 足够

`mimi_msg_t` 的 `channel[16]` + `:` + `chat_id[96]` = 最大 113 字节。当前 `AXK_SESSION_ID_LEN` 可能是 64，需改为 128：

```c
#define AXK_SESSION_ID_LEN  128
```

### Step 4: 实现 axk_session_load_messages()

```c
cJSON *axk_session_load_messages(const char *session_id)
{
    session_entry_t *entry;
    cJSON *messages;

    if (!session_id) return cJSON_CreateArray();

    entry = session_find(session_id);  // 现有函数
    if (!entry || !entry->context || entry->context_len == 0) {
        return cJSON_CreateArray();
    }

    messages = cJSON_Parse(entry->context);
    if (!messages || !cJSON_IsArray(messages)) {
        cJSON_Delete(messages);
        return cJSON_CreateArray();
    }

    entry->last_active = xTaskGetTickCount();
    return messages;
}
```

### Step 5: 实现 axk_session_save_messages()

```c
int axk_session_save_messages(const char *session_id, cJSON *messages)
{
    session_entry_t *entry;
    char *json_str;
    size_t json_len;

    if (!session_id || !messages) return -1;

    entry = session_get_or_create(session_id);  // 现有函数
    if (!entry) return -1;

    json_str = cJSON_PrintUnformatted(messages);
    if (!json_str) return -1;

    json_len = strlen(json_str);

    /* 超过缓冲区上限时截断（丢弃最旧的消息） */
    if (json_len >= MIMI_CONTEXT_BUF_SIZE) {
        /* 从 messages 数组中删除最旧的消息直到序列化后不超限 */
        while (cJSON_GetArraySize(messages) > 2) {  // 至少保留 2 条
            cJSON_DeleteItemFromArray(messages, 0);
            free(json_str);
            json_str = cJSON_PrintUnformatted(messages);
            if (!json_str) return -1;
            json_len = strlen(json_str);
            if (json_len < MIMI_CONTEXT_BUF_SIZE) break;
        }
    }

    /* 分配或扩展 PSRAM buffer */
    if (!entry->context) {
        entry->context = (char *)calloc(1, MIMI_CONTEXT_BUF_SIZE);
        if (!entry->context) {
            free(json_str);
            return -1;
        }
    }

    memcpy(entry->context, json_str, json_len + 1);  // +1 for '\0'
    entry->context_len = json_len;
    entry->last_active = xTaskGetTickCount();

    free(json_str);

    /* easyflash 持久化 */
    session_save_to_flash(entry);

    return 0;
}
```

### Step 6: 实现 axk_session_cleanup_stale()

```c
void axk_session_cleanup_stale(uint32_t idle_ticks)
{
    uint32_t now = xTaskGetTickCount();
    int i;

    for (i = 0; i < AXK_MAX_SESSIONS; i++) {
        session_entry_t *entry = &s_sessions[i];
        if (entry->id[0] == '\0') continue;
        if ((now - entry->last_active) > idle_ticks) {
            free(entry->context);
            entry->context = NULL;
            entry->context_len = 0;
            entry->id[0] = '\0';
        }
    }
}
```

### 验证
- 编译通过（`make -j4`）
- 确认 `AXK_SESSION_ID_LEN=128`，`context` 字段改为指针

---

## Task 2: 改造 agent_loop_task — messages 生命周期重构

**Objective:** `axk_agent_loop.c` 中 `messages` 不再每轮新建/销毁，而是从 session_mgr 恢复和保存

**Files:**
- Modify: `src/agent/axk_agent_loop.c`

### Step 1: 添加 include

```c
#include "axk_session_mgr.h"   // 新增
```

### Step 2: 在 while(1) 开头增加周期性 session 清理

在 `while (1)` 的 `axk_serial_cli_poll()` 之后，增加：

```c
/* 每 30 分钟清理一次过期会话 */
{
    static uint32_t s_last_cleanup = 0;
    uint32_t now = xTaskGetTickCount();
    if ((now - s_last_cleanup) > pdMS_TO_TICKS(30 * 60 * 1000)) {
        axk_session_cleanup_stale(pdMS_TO_TICKS(30 * 60 * 1000));
        s_last_cleanup = now;
    }
}
```

### Step 3: 核心改动 — 替换 messages 创建/销毁逻辑

**原代码 (L448-543 区域)**：

```c
while (1) {
    axk_serial_cli_poll();
    mimi_msg_t msg;
    cJSON *messages = NULL;
    cJSON *user_msg = NULL;
    char *final_text = NULL;
    int iteration = 0;
    int err = axk_message_bus_pop_inbound(&msg, UINT32_MAX);
    if (err != 0) { ... continue; }

    // skill match ...

    messages = cJSON_CreateArray();                    // ← 新建
    user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", msg.content);
    cJSON_AddItemToArray(messages, user_msg);

    while (iteration < MIMI_AGENT_MAX_TOOL_ITER) {
        // ... LLM 调用 + tool use 迭代 ...
        // L526-536: 追加 assistant + tool_result 到 messages
    }

    cJSON_Delete(messages);                            // ← 销毁
    // ... 发送回复 ...
}
```

**新代码**：

```c
while (1) {
    axk_serial_cli_poll();

    /* 周期性清理过期会话 */
    {
        static uint32_t s_last_cleanup = 0;
        uint32_t now = xTaskGetTickCount();
        if ((now - s_last_cleanup) > pdMS_TO_TICKS(30 * 60 * 1000)) {
            axk_session_cleanup_stale(pdMS_TO_TICKS(30 * 60 * 1000));
            s_last_cleanup = now;
        }
    }

    mimi_msg_t msg;
    cJSON *messages = NULL;
    cJSON *user_msg = NULL;
    char *final_text = NULL;
    int iteration = 0;
    int err = axk_message_bus_pop_inbound(&msg, UINT32_MAX);
    if (err != 0) { ... continue; }

    // skill match ...

    /* ── 构建 session_id ── */
    char session_id[AXK_SESSION_ID_LEN];
    snprintf(session_id, sizeof(session_id), "%s:%s", msg.channel, msg.chat_id);

    /* ── 从 session_mgr 恢复历史 messages ── */
    messages = axk_session_load_messages(session_id);

    /* ── 追加当前用户消息 ── */
    user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", msg.content ? msg.content : "");
    cJSON_AddItemToArray(messages, user_msg);

    /* ── LLM 调用 + tool use 迭代（不变） ── */
    while (iteration < MIMI_AGENT_MAX_TOOL_ITER) {
        // ... 现有代码不变 (L496-541) ...
    }

    /* ── 截断消息历史到 MIMI_SESSION_MAX_MSGS 条 ── */
    while (cJSON_GetArraySize(messages) > MIMI_SESSION_MAX_MSGS) {
        cJSON_DeleteItemFromArray(messages, 0);
    }

    /* ── 保存回 session_mgr ── */
    axk_session_save_messages(session_id, messages);

    /* ── 释放 messages ── */
    cJSON_Delete(messages);

    /* ── 发送回复（不变） ── */
    // ... 现有代码不变 (L545-591) ...
}
```

### 验证
- 编译通过
- 逻辑审查：`axk_session_load_messages` 返回的是 `cJSON *Array`，和原有 `cJSON_CreateArray()` 返回值类型完全兼容
- tool use 迭代中的 messages 追加逻辑（L526-536）不受影响

---

## Task 3: 编译验证

**Objective:** 编译整个固件，确保无编译错误和链接错误

**Files:** 无新增文件

### 命令

```bash
cd /home/seahi/workspase/AiPiClaw/AiPiClaw
make -j4 2>&1 | tail -30
```

### 预期
- 0 errors, 0 warnings（如果原有 warning 则不应增加）
- 生成的 `.bin` 文件大小增幅 < 5KB

---

## Task 4: git commit

**Objective:** 提交所有改动

```bash
cd /home/seahi/workspase/AiPiClaw/AiPiClaw
git add src/agent/axk_agent_loop.c src/memory/axk_session_mgr.c src/memory/axk_session_mgr.h
git commit -m "feat: add multi-turn conversation context memory via session_mgr"
git push
```

---

## 风险提示

| 风险 | 等级 | 注意 |
|------|------|------|
| `axk_session_mgr.c` 中 `session_find()` / `session_get_or_create()` 函数名可能与现有代码不同 | 中 | 先读 `axk_session_mgr.c` 确认实际函数名后调整 |
| cJSON 序列化 tool_use/tool_result 嵌套结构 | 低 | `cJSON_PrintUnformatted` 已处理嵌套，不额外处理 |
| easyflash 写入寿命 | 低 | 每次对话写一次（非每条消息） |
| `AXK_SESSION_ID_LEN` 和 `AXK_SESSION_CTX_LEN` 定义位置 | 低 | 可能在 `axk_session_mgr.h` 或 `mimi_config.h`，先确认 |
