# AiPiClaw 缺陷报告 (Defect Report)

**报告日期**: 2026-05-21  
**审查范围**: `CMakeLists.txt`, `FreeRTOSConfig.h`, `lwipopts_user.h`  
**审查者**: Claude Code (Hermes AI Code Review)  
**tool calling 已启用**: 11 turns, 195K input tokens, 32K output tokens

---

## 严重性汇总

| 严重性 | 数量 | 缺陷编号 |
|--------|------|---------|
| **P0 严重** | 5 | 2.1, 2.2, 3.1, 4.1 |
| **P1 高** | 4 | 2.3, 3.2, 3.3, 4.2 |
| **P2 中等** | 9 | 1.2, 2.4, 2.5, 3.4, 3.5, 3.6, 3.7, 4.3, 4.4 |
| **P3 低** | 7 | 1.3, 1.4, 2.6, 2.7, 2.8, 3.8, 3.9, 3.10 |

**总计**: 24 个缺陷

---

## P0 — 立即修复

### 2.1 configCPU_CLOCK_HZ = 1 MHz (应为 320 MHz)
**文件**: `FreeRTOSConfig.h:60`
```c
#define configCPU_CLOCK_HZ  ((uint32_t)(1 * 1000 * 1000))  // 错误!
```
BL618 实际运行 320 MHz。后果：所有 FreeRTOS 时间 API (vTaskDelay, 定时器, 超时) 完全失效。
**修复**: `#define configCPU_CLOCK_HZ ((uint32_t)(320 * 1000 * 1000))`

### 2.2 configTOTAL_HEAP_SIZE 与 lwIP 内存分配冲突
**文件**: `FreeRTOSConfig.h:64` + `lwipopts_user.h:102`
- FreeRTOS 堆 = 180 KB
- lwIP 堆 = 48 KB (独立)
- 总 = 228 KB，两个独立堆可能导致交叉分配

### 3.1 TCP_WND vs PBUF_POOL_SIZE 不匹配
**文件**: `lwipopts_user.h:80-84, 90-93`
- TCP_WND = 35040 字节 (2 × 12 × 1460)
- PBUF_POOL_SIZE = 16 (只能容纳 16 × 1460 = 23360 字节)
- 已通过 `LWIP_DISABLE_TCP_SANITY_CHECKS=1` 绕过检查——掩盖问题

### 4.1 堆内存分配策略不一致
**涉及**: `FreeRTOSConfig.h:64` + `lwipopts_user.h:102`
FreeRTOS 和 lwIP 各维护独立堆，可能出现跨堆分配

---

## P1 — 短期修复

### 2.3 configMINIMAL_STACK_SIZE 浪费
**文件**: `FreeRTOSConfig.h:63` — 注释说 128 字过高但未修正

### 3.2 MEMP_NUM_TCP_SEG = 32 可能过大
**文件**: `lwipopts_user.h:88` — 32 个 TCP segment 在 48KB 堆中占比较多

### 3.3 sys_thread_sem_* 未实现
**文件**: `lwipopts_user.h:149-159` — 三个函数仅声明无实现，链接失败

### 4.2 errno 处理不一致
**涉及**: FreeRTOS POSIX errno vs lwIP __errno() 机制冲突

---

## P2 — 中等优先级 (9 项)

| 编号 | 文件 | 缺陷 |
|------|------|------|
| 1.2 | CMakeLists.txt | 头文件/源文件 include 顺序混乱 |
| 2.4 | FreeRTOSConfig.h:90 | configTIMER_TASK_STACK_DEPTH=1024 可能不足 |
| 2.5 | FreeRTOSConfig.h:71 | configQUEUE_REGISTRY_SIZE=8 过小 |
| 3.4 | lwipopts_user.h:102 | LWIP_HEAP_SIZE 48KB 双HTTPS+WS临界 |
| 3.5 | lwipopts_user.h:67 | PBUF_LINK_ENCAPSULATION_HLEN=388 过大 |
| 3.6 | lwipopts_user.h:96 | TCP_SNDLOWAT 宏嵌套不可读 |
| 3.7 | lwipopts_user.h:146 | LWIP_RAND 使用 random() 无种子 |
| 4.3 | 跨文件 | TCPIP_THREAD_PRIO=28 可能饿死应用任务 |
| 4.4 | 跨文件 | 栈大小单位混淆 (FreeRTOS字 vs lwIP字节) |

---

## P3 — 代码质量 (7 项)

| 编号 | 文件 | 缺陷 |
|------|------|------|
| 1.3 | CMakeLists.txt:20 | 缺少 compat/ 头文件路径 |
| 1.4 | CMakeLists.txt | 注释全中文，缺英文 |
| 2.6 | FreeRTOSConfig.h:78 | 端口优化未验证 RISC-V 支持 |
| 2.7 | FreeRTOSConfig.h:114 | vAssertCalled 只声明未定义 |
| 2.8 | FreeRTOSConfig.h | 缺 configUSE_NEWLIB_REENTRANT |
| 3.8 | lwipopts_user.h:36 | LWIP_DEBUG 生产构建开启 |
| 3.9 | lwipopts_user.h:35/136 | LWIP_NETIF_API 重复定义 |
| 3.10 | lwipopts_user.h:50 | TCPIP_THREAD_STACKSIZE 1024 字节 = 256 字太小 |

---

## Claude Code 漏了什么

1. **没有审 src/ 下的 .c 文件** — 被限制只读配置文件，漏了运行时缺陷
2. **2.1 configCPU_CLOCK_HZ** — Claude Code 判断为 "1 MHz 错误"，但实际 BL618 的 CPU_CLOCK_HZ 在 SDK 中可能由启动代码动态设置，该值可能被覆盖，不一定是真 bug  
3. **内存中没有的已知运行时缺陷全部漏掉**:
   - LLM_WORKER_STACK 4096→8192 (栈溢出)
   - ef_save_env 阻塞调用
   - 优先级 6→10
   - PBUF_POOL 16→32 只是 lwipopts 配置，实际运行时可能已调整
   - mbedTLS ECDHE ~2.5KB
   - 端到端延迟 950-4500ms
4. **没有分析 main.c** 中的任务优先级 (mimi_main=5, wifi_fw=10)、栈分配 (8192×2)、main loop 的 10ms 轮询周期

**结论**: Claude Code 仅扫描了配置文件静态缺陷，缺少运行时/动态分析能力。
