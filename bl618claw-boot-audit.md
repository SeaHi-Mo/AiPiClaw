# BL618CLaw 启动时序审计报告 (Boot Media Audit)

**项目**: AiPiClaw v79  
**芯片**: BL616/BL618 (AiPi-Eyes-DU / BL616DK)  
**SDK**: Bouffalo SDK (os/ 子模块, 只读)  
**工具链**: riscv64-unknown-elf-gcc  
**审查日期**: 2026-05-15  

---

## 1. 启动链路总览

```
boot2_isp_mode=0
    ↓
__start (ROM跳转) → board_init()
    ├── bflb_flash_init()          # Flash初始化 (80MHz)
    ├── system_clock_init()        # 系统时钟: WIFIPLL→320MHz
    ├── peripheral_clock_init()    # 外设时钟门控
    ├── bflb_irq_initialize()      # IRQ + WiFi IRQ attach
    ├── console_init()             # ★ UART0 (GPIO21/22) / baud=115200
    ├── ram_heap_init()            # PSRAM + TLSF堆管理器
    └── board_init done
    ↓
rfparam_init(0, NULL, 0)          # PHY RF参数 (apply=0:不加载校准表)
bflb_mtd_init()                    # MTD分区子系统
easyflash_init()                   # KV存储 (Flash分区)
tcpip_init(NULL, NULL)             # lwIP TCP/IP协议栈
bflb_wdg_init() + bflb_wdg_start() # 看门狗 (30s, RESET模式)
shell_init_with_task(uart0)        # 串口Shell
    ↓ vTaskStartScheduler()
    ├── mimi_main (prio=5, stack=8K)
    │   └── +3s → axk_wifi_auto_connect() → SoftAP(config portal)
    └── wifi_fw (prio=10, stack=8K)
        └── wifi_task_create() → fhost_init() → wifi_mgmr_init()
```

---

## 2. boot2_isp_mode 审计

| 项目 | 当前值 | 说明 |
|:-----|:-------|:------|
| `flash_prog_cfg.ini` 中 `boot2_isp_mode` | **`=0`** | 禁用 Boot2 ISP 模式 |
| `CONFIG_BOOT2` | 未在 defconfig 中显式配置 | 由 SDK 默认继承 |

**审计结论**: `boot2_isp_mode=0` 是正确的生产模式配置。Boot2 仅负责从 Flash 加载固件到 RAM/ITCM 并跳转 `board_init`。如果设置为 `1`，Boot2 会在启动时等待 ISP 握手 1-2 秒，会导致启动延迟。

**风险**: 无。当前配置正确。

---

## 3. board_init → console_init 路径 (UART0 GPIO21/22 完整性审计)

### 3.1 console_init() 内部流程 (board.c:309-337)

```c
gpio = bflb_device_get_by_name("gpio");
bflb_gpio_uart_init(gpio, GPIO_PIN_21, GPIO_UART_FUNC_UART0_TX);  // GPIO21 = TX
bflb_gpio_uart_init(gpio, GPIO_PIN_22, GPIO_UART_FUNC_UART0_RX);  // GPIO22 = RX

uart0 = bflb_device_get_by_name("uart0");
bflb_uart_init(uart0, &cfg);          // 115200, 8N1, FIFO阈值=7
bflb_uart_set_console(uart0);         // 绑定为console (stdout)
```

`bflb_gpio_uart_init()` 使用 `GPIO_FUNC_UART0_TX/RX` — 这会将 GPIO21/22 配置为 **ALTERNATE 模式** (复用功能)，而不是 GPIO 功能。这意味着后续任何 `bflb_gpio_init()` 调用如果传了相同引脚号且用了 `GPIO_FUNC_GPIO`，**会覆盖这个复用配置**。

### 3.2 UART0 GPIO21/22 覆盖风险检查

| 代码路径 | 是否覆盖 GPIO21/22? | 详情 |
|:---------|:---------------------|:------|
| `axk_hal_gpio_init()` | **不覆盖** ✅ | 只操作 GPIO12/14/15 (RGB LED) |
| `axk_hal_gpio_config(12/14/15)` | 不操作 21/22 ✅ | LED 引脚明确不包含 UART 引脚 |
| `axk_hal_gpio_set_direction()` | 不自动调用 ✅ | 仅当用户通过 shell 或 tool 主动调用 |
| `axk_gpio_control.c` | 检查白名单 ✅ | 白名单只允许 10/11/12/17 |
| `axk_hal_uart_init()` | **不重配** ✅ | 只 memset 设备句柄数组，不调用 bflb_uart_init |
| `axk_hal_uart_config()` | **有风险** ⚠️ | 如果被调用 port=0，会 `bflb_uart_init(uart0, &bflb_cfg)` - 但不会改 GPIO 复用 |
| `board_spi0_gpio_init()` | 不操作 21/22 ✅ | 只操作 12/13/18/19 |
| `board_i2c0_gpio_init()` | 不操作 21/22 ✅ | 只操作 11/14 |

**审计结论**: 项目代码中没有路径会调用 `bflb_gpio_init()` 覆盖 GPIO21/22 的 UART 复用配置。Console UART 的 GPIO 配置在整个启动过程中保持完整。

### 3.3 Baudrate 一致性

| 阶段 | 值 | 来源 |
|:-----|:----|:------|
| `console_init` | `CONFIG_CONSOLE_UART_BAUDRATE` = **115200** | defconfig |
| `shell_init_with_task` | 继承 uart0 句柄的当前配置 | 不重新 init |
| `axk_hal_uart_config(port=0)` | 不会自动调用 | 仅当业务代码主动调用 |

**审计结论**: 波特率 115200 在整个启动链中一致。shell 使用 `bflb_device_get_by_name("uart0")` 获取已在 console_init 中配置好的 UART0 句柄，不会重新 init。

---

## 4. MTD + easyflash 初始化时序

### 4.1 启动顺序

```
board_init()
    → bflb_flash_init()           # Flash硬件初始化 (board.c:421)
    → console_init()              # UART console
    → ram_heap_init()              # 堆初始化 (包含PSRAM)
    ↓
main.c:372  board_init() done    ← 返回main
main.c:375  rfparam_init(0, NULL, 0)  # 读 RF 校准参数 (读Flash分区)
main.c:382  bflb_mtd_init()       # MTD分区初始化 ← 必须在easyflash之前
main.c:385  easyflash_init()      # KV存储 ← 必须在所有KV读模块之前
```

### 4.2 顺序正确性

**正确**: `bflb_mtd_init()` 在 `easyflash_init()` 之前调用，MTD 为 easyflash 提供分区映射。easyflash 在 **所有 KV 依赖模块初始化之前** 完成 (session_mgr、llm_proxy 等在 `axk_mimiclaw_modules_init()` 中初始化，在 `easyflash_init()` 之后)。

### 4.3 rfparam_init 位置风险

`rfparam_init(0, NULL, 0)` 在 main.c:375 调用，位于 `board_init()` 之后但 **在 `bflb_mtd_init()` 之前**。rfparam_init 内部会：
1. 通过 `bflb_flash_read()` 直接从 Flash 读取 RF 校准数据（不依赖 MTD）
2. 打印大量 PHY RF 校准信息到串口

**风险**: 
- `rfparam_init(apply_flag=0)` 表示不加载校准参数表，使用默认功率表，WiFi 发射功率偏低。不影响 WiFi 连接功能，但可能影响信号强度和吞吐量。
- 串口大量 RF 校准输出可能在启动早期淹没控制台，但对启动时序无影响。

**建议**: 如遇到 WiFi 信号弱，可将 `rfparam_init(0, NULL, 0)` 改为 `rfparam_init(0, NULL, 1)` 以加载出厂校准参数。

---

## 5. WiFi/EAP 初始化时序

### 5.1 初始化完整路径

```
main.c (vTaskStartScheduler 前):
    1. board_init()              → flash, clock, console, heap
    2. rfparam_init(0,NULL,0)    → PHY RF参数
    3. bflb_mtd_init()            → MTD分区
    4. easyflash_init()           → KV存储
    5. tcpip_init(NULL, NULL)     → lwIP协议栈
    6. bflb_wdg_init+start       → 看门狗30s
    7. shell_init_with_task       → 串口Shell
    8. axk_mimiclaw_modules_init():
       ├── axk_hal_system_init
       ├── axk_hal_gpio_init
       ├── axk_hal_uart_init
       ├── axk_hal_flash_init
       ├── axk_hal_timer_init
       ├── axk_heartbeat_init
       ├── axk_tool_files_init
       ├── axk_memory_store_init
       ├── axk_session_mgr_init
       ├── axk_message_bus_init
       ├── axk_wifi_manager_init      # async_register_event_filter
       ├── axk_serial_cli_init
       ├── ...其他模块...
       ├── axk_ws_server_init+start   # WS :18789
       └── axk_agent_loop_init+start  # Agent任务

vTaskStartScheduler 后:
    wifi_fw task (prio=10):
        wifi_task_create()             # 创建WiFi MAC任务
        fhost_init()                   # WiFi6协议栈
        wifi_mgmr_init(&s_wifi_conf)   # WiFi管理器

    mimi_main (prio=5, stack=8192):
        +3秒 → axk_wifi_auto_connect()
        失败? → axk_wifi_onboard_start()  # SoftAP "AiPiClaw"
              → axk_config_portal_start()  # HTTP :80
```

### 5.2 WiFi 初始化顺序验证 (fhost/WiFi6 要求)

| 步骤 | SDK 要求顺序 | 实际顺序 | 一致? |
|:-----|:------------|:---------|:------|
| 1 | `board_init()` | board_init() | ✅ |
| 2 | `tcpip_init()` | tcpip_init() | ✅ |
| 3 | `wifi_task_create()` | wifi_fw task → wifi_task_create() | ✅ |
| 4 | `fhost_init()` | ←之后立即调用 | ✅ |
| 5 | `rfparam_init()` | 在 vTaskStartScheduler 前已调用 | ✅ |
| 6 | `wifi_mgmr_init()` | ←之后立即调用 | ✅ |

**审计结论**: 顺序完全正确。`rfparam_init` 必须在 `wifi_mgmr_init` 之前调用 — 当前代码满足。

### 5.3 wifi_fw 任务栈深度评估

| 项 | 值 |
|:---|:----|
| 任务栈 | 8192 字节 |
| 最深处 | `fhost_init()` → macsw 初始化 → `mm_timer_init` → `hal_machw_init` |
| 嵌套深度 | 约 40-50 层函数调用 |
| 栈余量 | 在 8192 下有限（WPS/EAPOL 路径可达 4000+ 字节） |

**风险评估**: 当前 8192 字节对于 `wifi_task_create()` 内部创建的 wifi_fw 任务**可能不足**。此前在 DHCP WPS 路径中，`wps_enrollee_process_msg` 单帧 1360 字节。虽然 OOB WPS 已用 `-UCONFIG_WPS2 -UCONFIG_WPS_PIN` 禁用，但 EAPOL 处理路径仍有深度嵌套。

**建议**: 在没有完整栈水位测量前，维持 8192。如再次出现栈溢出相关 crash，增加到 12288。

### 5.4 EAP (WPA supplicant) 初始化时序

EAP 初始化在 `wifi_mgmr_init()` 内部触发。fhost 初始化的详细序列：
```
wifi_task_create() → 创建 wifi_fw RTOS 任务
fhost_init()       → macsw_platform_init() → intc_init() → 注册WiFi IRQ
                   → 创建 ke_task 架构
wifi_mgmr_init(&s_wifi_conf):
    → wpa_supplicant_init()
        → wpa_supplicant_init_eapol()     # EAPOL状态机
        → eap_peer_methods_validate()     # EAP方法注册 (包括wsc_method=EAP_WSC)
        → wpas_wps_init()                  # WPS初始化
    → wifi_mgmr_sta_autoconnect_enable()
```

**关键发现**: 虽然 defconfig 中没有显式 `-UCONFIG_WPS`，但 commit `adda843` 明确添加了 `-UCONFIG_WPS2 -UCONFIG_WPS_PIN -UUSE_WPS_TASK` 来禁用 WSC/WPS。这意味着 `wpas_wps_init()` 仍然被调用但内部 WPS 功能被关闭。EAPOL 路径不会进入 WPS 解析器，避免 WPS 栈溢出。

---

## 6. 关键风险汇总

| # | 风险点 | 级别 | 影响 | 缓解/修复 |
|:-:|:-------|:-----|:-----|:----------|
| 1 | `rfparam_init(apply=0)` 不加载校准表 | 🟡 中 | WiFi 发射功率偏低 | 改为 `rfparam_init(0,NULL,1)` |
| 2 | wifi_fw 任务栈 8192 在 EAPOL 路径可能不足 | 🟡 中 | 栈溢出风险 | 维持 8192，异常时增至 12288 |
| 3 | UART0 GPIO21/22 被外部 HAL 调用覆盖 | 🟢 低 | 理论上不会 | 已验证项目中无覆盖路径 |
| 4 | `boot2_isp_mode=0` 阻止 ISP 启动等待 | 🟢 低 | 正常启动 | 当前正确 |
| 5 | 看门狗 30s 在 modules_init 期间运行 | 🟡 中 | 如果模块 init >30s 触发复位 | mimi_main 才喂狗，init 在主线程非任务中 |
| 6 | `axk_hal_gpio_init` 重配 GPIO12 覆盖 board_spi0 | ✅ 已记录 | GPIO12 做 RGB 红灯 | 代码中有注释说明此行为 |

---

## 7. 建议改进项

### P1 — 需要关注
1. `rfparam_init(0, NULL, 1)` 启用校准加载：增加一行代码即可提升 WiFi 性能。但注意 `apply_flag=1` 不抑制 rfparam 的 printf 输出。

### P2 — 建议优化
2. console_init 波特率从 115200 改为 2000000 以匹配烧录速度（如果 CH340 支持）：
   修改 defconfig: `CONFIG_CONSOLE_UART_BAUDRATE = 2000000`
   注意：shell CLI 交互也会使用 2Mbps，终端需匹配。

3. 考虑在 main.c 中添加模块初始化耗时监控：
   ```c
   uint32_t t0 = bflb_mtimer_get_time_ms();
   ret = axk_xxx_init();
   printf("[main] xxx_init took %d ms\r\n", bflb_mtimer_get_time_ms() - t0);
   ```

### P3 — 低优先级
4. board_init 过程中 console_init 前的 printf（如 flash init fail 消息）会因为console未就绪而无法输出，这是 SDK 设计行为，无法修改 os/。
