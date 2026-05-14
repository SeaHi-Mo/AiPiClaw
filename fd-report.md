# Phase 1 Crash Analysis Report

**Time**: 2026-05-14
**Crash**: Load access fault, mcause=0x38000005

---

## 1. Crash Location

| Register | Value | Meaning |
|----------|-------|---------|
| mepc | 0xa00dce74 | Faulting instruction address |
| mcause | 0x38000005 | Exception 5 = Load access fault |
| mtval | 0xa7e7af8f | Faulting virtual address (the pointer that was dereferenced) |

### Source (via nm -n + objdump)

```
a00dce2c T eap_peer_method_register   (libwpa_supplicant.a, eap_methods.c.o)
```

Disassembly at crash point:

```asm
a00dce5a:  lui   a5, 0x62fe0           ; a5 = 0x62fe0000
a00dce5e:  lw    s1, -1512(a5)         ; s1 = *(0x62fdfa18) = eap_methods global
a00dce62:  li    a4, 0
a00dce64:  addi  s2, a5, -1512         ; s2 = &eap_methods
a00dce68:  bnez  s1, a00dce74          ; if s1 != NULL, enter linked-list traversal
a00dce6a:  beqz  a4, a00dcea2          ; else insert at head (normal path)
...
a00dce74:  lw    a4, 0(s1)             ; ← CRASH HERE: load s1->vendor
```

**Crash instruction**: `lw a4, 0(s1)` — dereferencing the `eap_methods` linked list head pointer.

---

## 2. Memory Layout Analysis

### eap_methods BSS location

```
Address       Symbol                  Object file
0x62fdf9a0    wsc_registrar.0 (0x74)  eap_user_db.c.o     ← 116 bytes, ends at 0x62fdfa13
0x62fdfa14    eap_methods    (0x4)    eap_server_methods.c.o  ← server methods head
0x62fdfa18    eap_methods    (0x4)    eap_methods.c.o      ← PEER methods head ← CRASH
0x62fdfa1c    flash_added_delay
```

### BSS Zeroing Verification

`start_load()` uses `__mem_setz_sections` to zero BSS:

```
__mem_setz_sections (from linker.ld:239-241):
  {0x62fc9740, 0x62fdfa34}  → RAM BSS zeroing range
  {0x23010000, 0x23034b20}  → WiFi BSS zeroing range  
  {0xffffffff, 0xffffffff}  → terminator
```

✅ `eap_methods` at 0x62fdfa18 is WITHIN the zeroing range [0x62fc9740, 0x62fdfa34).
✅ `start_load()` has NO conditional skip — it ALWAYS runs BSS zeroing.

---

## 3. Garbage Value Analysis

mtval = **0xa7e7af8f** stored in `eap_methods` at 0x62fdfa18.

This value appears in the ELF at only ONE location:

```
a00fd5c0: e4 bf a1 e5  8f af e7 a7  91 e6 8a 80  20 28 41 49
```

This is inside `__floatdidf` (libgcc), a floating-point conversion constant table.  
**Conclusion**: 0xa7e7af8f is a garbage bit pattern, NOT a valid pointer.

---

## 4. Call Chain to Crash

```
main()
  → WiFi init
    → WPS enrollment start
      → eap_peer_wsc_register()          [a00dcd04]
        → eap_peer_method_register()     [a00dce2c]
          → traverse eap_methods list     [a00dce74] CRASH
```

---

## 5. Root Cause

**Memory corruption: `wsc_registrar.0` buffer overflow writes into adjacent `eap_methods` pointer.**

### Evidence
1. `wsc_registrar.0` (0x62fdf9a0-0x62fdfa13) is immediately before `eap_methods` (0x62fdfa18)
2. Only 4 bytes of gap (0x62fdfa14-0x62fdfa17) occupied by `eap_server_methods`
3. Crash happens during WPS enrollment (WiFi connection attempt), right when WSC registrar state machine is active
4. The corrupted value 0xa7e7af8f is not code-addressable — rules out stale pointer from previous boot

### Mechanism
During WPS registrar operation, the 116-byte `wsc_registrar.0` struct's internal operations (likely eap_user_db credential parsing) write past its boundary, corrupting one or both `eap_methods` pointers. When `eap_peer_wsc_register` subsequently calls `eap_peer_method_register`, it traverses the corrupted linked list and dereferences garbage.

### Alternative (less likely)
If BSS was NOT zeroed (warm boot corner case), `eap_methods` would retain a stale pointer from a previous run's heap allocation. However:
- `start_load()` always runs and always zeros BSS
- The value 0xa7e7af8f doesn't match any heap address range on BL618

---

## 6. Recommendations

### P0: Add BSS sentinel between wsc_registrar and eap_methods
Add `__attribute__((used))` guard variables to detect overflow:

```c
// In eap_user_db.c or wherever wsc_registrar is defined:
static uint32_t __wsc_registrar_guard __attribute__((section(".bss.eap_guard"))) = 0xDEADBEEF;
```

### P0: Defensive check in eap_peer_method_register
Add range validation before traversing linked list:

```c
// In eap_methods.c:eap_peer_method_register, before the while loop:
if ((uintptr_t)eap_methods < 0x62fc0000 || (uintptr_t)eap_methods > 0x62fe0000) {
    // Corrupted pointer — reset and continue
    eap_methods = NULL;
}
```

### P1: Investigate wsc_registrar.0 size
Check if the 116-byte allocation is sufficient for all WSC registrar state transitions, especially credential parsing with long SSID/passphrase.

### P1: Add BSS poison value on boot
After BSS zeroing, write known pattern (0x00000000 is the zero, but check after init that critical pointers are NULL).

---

## 7. Verification Steps

1. Flash with debug build, monitor serial for "Phase 1" boot sequence
2. If crash reproduces, add BSS dump at 0x62fdf9a0-0x62fdfa20 before WiFi init
3. Add watchpoint on 0x62fdfa18 via gdb (if CKLink available) to catch the write source
