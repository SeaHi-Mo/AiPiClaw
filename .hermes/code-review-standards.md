# AiPiClaw Code Review Standards (16-Point Checklist)

## 4-Color Classification
- 🔴 **Critical** — security, crashes, data corruption, race conditions, null deref
- 🟡 **Functional** — logic bugs, flash persistence gaps, format mismatch, dead code
- 🔵 **Quality** — style violations, missing error handling, naming, comments
- 🟢 **OK** — clean, correct, follows conventions

## 16-Point Checklist

### Safety & Security (🔴)
1. **Null pointer safety** — pointer checked before dereference; `malloc`/`pvPortMalloc` return checked
2. **Buffer overflow** — strcpy/strcat/sprintf with unchecked length; snprintf with user input building JSON
3. **Race condition (TOCTOU)** — shared data in ISR + task without critical section/mutex; WiFi connect/disconnect races
4. **Use-after-free / double-free** — free'd pointer reused; double free on error path

### Logic & Correctness (🟡)
5. **Flash persistence** — ef_set/ef_get paired correctly (blob↔string); config survives reboot
6. **Error handling completeness** — all error paths handled; resource cleanup on early return
7. **Return value consistency** — 0=OK vs -1=err consistent across function; caller checks return
8. **Timer/tick overflow** — tick delta wraps at ~49 days; hardcoded delay assumptions

### Code Quality & Style (🔵)
9. **Naming convention** — `module_action_object` (func), `lower_snake_case` (var), `SCREAMING_SNAKE` (macro), `_t` suffix (struct typedef)
10. **.clang-format compliance** — 4-space indent, K&R braces, 80/120 col limit
11. **Magic numbers / hardcoded** — use `#define` not raw constants; IP/URL/port in Kconfig not source
12. **Commented-out / dead code** — no `//` commented logic; no `#if 0` without justification; no unreachable branches

### Bouffalo/BL618 Specific (🔵)
13. **GPIO OE register** — `0x200008C4 + (pin>>1)*4`, bit = 6 + (pin&1)*16 for BL618; active-high LED
14. **lwIP heap sizing** — MEM_SIZE ≥ 48KB for WS+HTTPS; dns_sethostname→callback pattern not getaddrinfo
15. **FreeRTOS heap_3** — kmalloc/kfree not standard malloc; xPortGetFreeHeapSize stub returns 0 (use kfree_size)
16. **WebSocket + HTTP collision** — `GET / HTTP/1.1` common prefix; exclude `Upgrade: websocket` from HTTP handler

## Review Workflow
1. Fetch latest origin/develop
2. Identify new commits (compare with last-reviewed SHA)
3. For each new commit: `git diff <sha>^!` and review against checklist
4. Classify findings per 4-color scheme
5. Generate report with file:line, color, description, recommendation
6. Accumulate PASS/FAIL ratio per developer
