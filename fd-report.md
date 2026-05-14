     1|# Phase 1 Crash Analysis Report
     2|
     3|**Time**: 2026-05-14
     4|**Crash**: Load access fault, mcause=0x38000005
     5|
     6|---
     7|
     8|## 1. Crash Location
     9|
    10|| Register | Value | Meaning |
    11||----------|-------|---------|
    12|| mepc | 0xa00dce74 | Faulting instruction address |
    13|| mcause | 0x38000005 | Exception 5 = Load access fault |
    14|| mtval | 0xa7e7af8f | Faulting virtual address (the pointer that was dereferenced) |
    15|
    16|### Source (via nm -n + objdump)
    17|
    18|```
    19|a00dce2c T eap_peer_method_register   (libwpa_supplicant.a, eap_methods.c.o)
    20|```
    21|
    22|Disassembly at crash point:
    23|
    24|```asm
    25|a00dce5a:  lui   a5, 0x62fe0           ; a5 = 0x62fe0000
    26|a00dce5e:  lw    s1, -1512(a5)         ; s1 = *(0x62fdfa18) = eap_methods global
    27|a00dce62:  li    a4, 0
    28|a00dce64:  addi  s2, a5, -1512         ; s2 = &eap_methods
    29|a00dce68:  bnez  s1, a00dce74          ; if s1 != NULL, enter linked-list traversal
    30|a00dce6a:  beqz  a4, a00dcea2          ; else insert at head (normal path)
    31|...
    32|a00dce74:  lw    a4, 0(s1)             ; ← CRASH HERE: load s1->vendor
    33|```
    34|
    35|**Crash instruction**: `lw a4, 0(s1)` — dereferencing the `eap_methods` linked list head pointer.
    36|
    37|---
    38|
    39|## 2. Memory Layout Analysis
    40|
    41|### eap_methods BSS location
    42|
    43|```
    44|Address       Symbol                  Object file
    45|0x62fdf9a0    wsc_registrar.0 (0x74)  eap_user_db.c.o     ← 116 bytes, ends at 0x62fdfa13
    46|0x62fdfa14    eap_methods    (0x4)    eap_server_methods.c.o  ← server methods head
    47|0x62fdfa18    eap_methods    (0x4)    eap_methods.c.o      ← PEER methods head ← CRASH
    48|0x62fdfa1c    flash_added_delay
    49|```
    50|
    51|### BSS Zeroing Verification
    52|
    53|`start_load()` uses `__mem_setz_sections` to zero BSS:
    54|
    55|```
    56|__mem_setz_sections (from linker.ld:239-241):
    57|  {0x62fc9740, 0x62fdfa34}  → RAM BSS zeroing range
    58|  {0x23010000, 0x23034b20}  → WiFi BSS zeroing range  
    59|  {0xffffffff, 0xffffffff}  → terminator
    60|```
    61|
    62|✅ `eap_methods` at 0x62fdfa18 is WITHIN the zeroing range [0x62fc9740, 0x62fdfa34).
    63|✅ `start_load()` has NO conditional skip — it ALWAYS runs BSS zeroing.
    64|
    65|---
    66|
    67|## 3. Garbage Value Analysis
    68|
    69|mtval = **0xa7e7af8f** stored in `eap_methods` at 0x62fdfa18.
    70|
    71|This value appears in the ELF at only ONE location:
    72|
    73|```
    74|a00fd5c0: e4 bf a1 e5  8f af e7 a7  91 e6 8a 80  20 28 41 49
    75|```
    76|
    77|This is inside `__floatdidf` (libgcc), a floating-point conversion constant table.  
    78|**Conclusion**: 0xa7e7af8f is a garbage bit pattern, NOT a valid pointer.
    79|
    80|---
    81|
    82|## 4. Call Chain to Crash
    83|
    84|```
    85|main()
    86|  → WiFi init
    87|    → WPS enrollment start
    88|      → eap_peer_wsc_register()          [a00dcd04]
    89|        → eap_peer_method_register()     [a00dce2c]
    90|          → traverse eap_methods list     [a00dce74] CRASH
    91|```
    92|
    93|---
    94|
    95|## 5. Root Cause
    96|
    97|**Memory corruption: `wsc_registrar.0` buffer overflow writes into adjacent `eap_methods` pointer.**
    98|
    99|### Evidence
   100|1. `wsc_registrar.0` (0x62fdf9a0-0x62fdfa13) is immediately before `eap_methods` (0x62fdfa18)
   101|2. Only 4 bytes of gap (0x62fdfa14-0x62fdfa17) occupied by `eap_server_methods`
   102|3. Crash happens during WPS enrollment (WiFi connection attempt), right when WSC registrar state machine is active
   103|4. The corrupted value 0xa7e7af8f is not code-addressable — rules out stale pointer from previous boot
   104|
   105|### Mechanism
   106|During WPS registrar operation, the 116-byte `wsc_registrar.0` struct's internal operations (likely eap_user_db credential parsing) write past its boundary, corrupting one or both `eap_methods` pointers. When `eap_peer_wsc_register` subsequently calls `eap_peer_method_register`, it traverses the corrupted linked list and dereferences garbage.
   107|
   108|### Alternative (less likely)
   109|If BSS was NOT zeroed (warm boot corner case), `eap_methods` would retain a stale pointer from a previous run's heap allocation. However:
   110|- `start_load()` always runs and always zeros BSS
   111|- The value 0xa7e7af8f doesn't match any heap address range on BL618
   112|
   113|---
   114|
   115|## 6. Recommendations
   116|
   117|### P0: Add BSS sentinel between wsc_registrar and eap_methods
   118|Add `__attribute__((used))` guard variables to detect overflow:
   119|
   120|```c
   121|// In eap_user_db.c or wherever wsc_registrar is defined:
   122|static uint32_t __wsc_registrar_guard __attribute__((section(".bss.eap_guard"))) = 0xDEADBEEF;
   123|```
   124|
   125|### P0: Defensive check in eap_peer_method_register
   126|Add range validation before traversing linked list:
   127|
   128|```c
   129|// In eap_methods.c:eap_peer_method_register, before the while loop:
   130|if ((uintptr_t)eap_methods < 0x62fc0000 || (uintptr_t)eap_methods > 0x62fe0000) {
   131|    // Corrupted pointer — reset and continue
   132|    eap_methods = NULL;
   133|}
   134|```
   135|
   136|### P1: Investigate wsc_registrar.0 size
   137|Check if the 116-byte allocation is sufficient for all WSC registrar state transitions, especially credential parsing with long SSID/passphrase.
   138|
   139|### P1: Add BSS poison value on boot
   140|After BSS zeroing, write known pattern (0x00000000 is the zero, but check after init that critical pointers are NULL).
   141|
   142|---
   143|
   144|## 7. Verification Steps
   145|
   146|1. Flash with debug build, monitor serial for "Phase 1" boot sequence
   147|2. If crash reproduces, add BSS dump at 0x62fdf9a0-0x62fdfa20 before WiFi init
   148|3. Add watchpoint on 0x62fdfa18 via gdb (if CKLink available) to catch the write source
   149|

---

# Phase 2: strcmp(NULL) Crash — 执行顺序修复

**Time**: 2026-05-14 17:27
**Crash**: mepc=a0061d2c (strcmp), mtval=0 (NULL pointer dereference)

## Root Cause

memset+memcpy fix (commit 11d4619) placed the initialization AFTER fhost_init():

```
wifi_task_create();
int ret = fhost_init();        // ← CRASH: fhost 内部读未自始化的 country_code
// ...
memset(&s_wifi_conf, 0, ...);  // ← 永远执行不到!
memcpy(s_wifi_conf.country_code, "CN", ...);
ret = wifi_mgmr_init(&s_wifi_conf);
```

fhost_init() 调用链中, fhost_cntrl_init → (later) fhost_cntrl_ke_msg_country_code_ind
访问 wifiMgmr BSS 中的 country_code 字段. 该字段为未初始化垃圾值,
传入 strcmp 导致 NULL dereference.

## Fix (commit 0b8cb01)

将 s_wifi_conf 初始化移到 fhost_init() 之前:

```
wifi_task_create();
// C5: s_wifi_conf 必须在 fhost_init 前初始化
memset(&s_wifi_conf, 0, sizeof(s_wifi_conf));
memcpy(s_wifi_conf.country_code, "CN", sizeof(s_wifi_conf.country_code));
int ret = fhost_init();        // ← 现在安全了
// ...
ret = wifi_mgmr_init(&s_wifi_conf);
```

## Verification

- Build: make CHIP=bl616 BOARD=bl616dk → OK
- ELF: nm -n 确认 s_wifi_conf 在 BSS 段 0x62fdaXXX 范围
- Flash 待测试
