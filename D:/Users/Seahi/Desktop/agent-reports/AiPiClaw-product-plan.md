# AiPiClaw 产品规划方案 (v1.0)

> 撰写日期: 2026-05-15 | 当前版本: v1.0.0-alpha (commit 2b2a351)
> 项目: mimiclaw (ESP32-S3 AI Agent) → BL618 移植版

---

## 一、当前状态速览

| 模块 | 状态 | 说明 |
|------|:----:|------|
| 基线启动 | ✅ | V1.0.0 BSS布局兼容 |
| WiFi连接 | ✅ | DHCP获取IP 192.168.16.20 |
| LLM全链路 | ✅ | MiniMax M2.5 Anthropic兼容接口 |
| 多轮对话 | ✅ | session_mgr + easyflash持久化 |
| Web UI | ✅ | :18789 WebSocket |
| 工具注册 | ✅ | 14个工具已激活, 2个Phase1 Disabled |
| DHCP稳定性 | 🔴 | 0x000d6d66 7轮修复,待长稳验证 |
| board_config | 🟡 | 硬编码fallback, 待恢复JSON解析 |
| OTA升级 | 🟡 | 骨架已有, 功能未实现 |
| 串口CLI中文 | 🟡 | mimi命令中文输入不工作 |

---

## 二、剩余需求拆解与优先级

### 优先级定义
| 级别 | 含义 | 进入下一Phase的必要条件 |
|:----:|------|------------------------|
| P0 🔴 | 致命阻塞 — 影响核心功能稳定性 | 必须修复才能进入下一Phase |
| P1 🟡 | 高优先级 — 核心功能缺失 | 应在本Phase完成 |
| P2 🔵 | 中优先级 — 体验/质量提升 | 可延后一个Phase |
| P3 🟢 | 低优先级 — 锦上添花 | 可延后 |

---

### Phase 1: 稳定化 (P0 → v1.0.1) — 目标 1 周

| ID | 需求 | 优先级 | 当前状态 | 产出 |
|----|------|:------:|----------|------|
| **REQ-001** | DHCP crash根因验证+长稳测试 | 🔴 P0 | 7轮修复,栈变量改static后未复现 | 48h无crash长稳报告 |
| **REQ-002** | WiFi断连重连机制 | 🔴 P0 | 未实现,当前断连=死机 | 自动重连+指数退避 |
| **REQ-003** | boot_media时序验证 | 🔴 P0 | 上次修复了ISP mode,需回测 | 3次冷启动全部正常 |
| **REQ-004** | 内存泄漏检测 | 🟡 P1 | 无检测机制 | FreeRTOS heap watermark监控 |
| **REQ-005** | LLM API timeout/重试 | 🟡 P1 | 无超时处理 | 10s超时+最多3次重试 |

**Phase 1 验收标准:**
- DHCP crash 48h 0次复现
- WiFi断连后10s内自动恢复
- cold boot 5/5 成功率
- heap可用 ≥ 30% 持续运行24h

---

### Phase 2: board_config恢复 (P1 → v1.0.2) — 目标 1 周

| ID | 需求 | 优先级 | 当前状态 | 产出 |
|----|------|:------:|----------|------|
| **REQ-010** | board.json解析器恢复 | 🟡 P1 | parser已有, 但Phase1 DISABLED | 恢复axk_board_config_init() |
| **REQ-011** | board_info工具激活 | 🟡 P1 | 代码已有, DISABLED | LLM可查询板卡信息 |
| **REQ-012** | gpio_list工具激活 | 🟡 P1 | 代码已有, DISABLED | LLM可列出所有GPIO别名 |
| **REQ-013** | GPIO policy硬编码→动态 | 🟡 P1 | gpio_policy/gpio_alias用硬编码fallback | 统一走board_config查表 |
| **REQ-014** | board.json模板文档 | 🔵 P2 | 无 | 硬件工程师自助配置指南 |

**Phase 2 验收标准:**
- 5块不同板子各烧录专属board.json, LLM查询board_info返回正确
- gpio_list返回所有别名列表
- gpio_write_named通过board_config查表, 非硬编码

---

### Phase 3: OTA升级 + 配置热更新 (P1 → v1.1.0) — 目标 2 周

| ID | 需求 | 优先级 | 当前状态 | 产出 |
|----|------|:------:|----------|------|
| **REQ-020** | OTA固件下载 | 🟡 P1 | 骨架已有, 缺HTTP下载 | HTTP Range分段下载+XZ解压 |
| **REQ-021** | OTA固件校验 | 🟡 P1 | SHA256已有 | 下载完成SHA256校验+回滚 |
| **REQ-022** | OTA A/B分区 | 🟡 P1 | 未分区 | bootloader A/B切换 |
| **REQ-023** | Web UI上传固件 | 🔵 P2 | Web UI已有 | 拖拽.bin上传触发OTA |
| **REQ-024** | LLM API Key热更新 | 🔵 P2 | 仅串口CLI | Web UI/LLM对话修改API Key |

**Phase 3 验收标准:**
- 通过HTTP下载.bin固件, SHA256校验通过后重启切换
- 校验失败自动回滚到旧版本
- Web UI上传固件 ≤ 60秒完成

---

### Phase 4: 产品化增强 (P2 → v1.2.0) — 目标 2 周

| ID | 需求 | 优先级 | 当前状态 | 产出 |
|----|------|:------:|----------|------|
| **REQ-030** | 串口CLI中文支持 | 🔵 P2 | mimi命令中文输入异常 | UTF-8全链路支持 |
| **REQ-031** | Web UI自动连接 | 🔵 P2 | 需手动输IP | window.location.hostname自动发现 |
| **REQ-032** | MQTT消息通道 | 🔵 P2 | 未实现 | MQTT订阅+发布, LLM可推消息 |
| **REQ-033** | 消息总线v2:反压+死信 | 🔵 P2 | TODO标记在代码中 | queue full阻塞+死信诊断 |
| **REQ-034** | 技能系统(Skills) | 🔵 P2 | axk_skill_loader骨架 | 运行时加载.md skill文件 |
| **REQ-035** | WiFi配网Web Portal | 🔵 P2 | 仅硬编码SSID | 手机连AP→浏览器配网 |

**Phase 4 验收标准:**
- 串口CLI输入中文, LLM正确理解并回复
- Web UI打开即连, 无需配置IP
- WiFi配网流程: 手机连设备热点 → 浏览器选SSID输密码 → 设备连接

---

### Phase 5: 生产就绪 (P3 → v1.5.0) — 目标 3 周

| ID | 需求 | 优先级 | 当前状态 | 产出 |
|----|------|:------:|----------|------|
| **REQ-040** | 出厂测试流程 | 🟢 P3 | 无 | 一键烧录+GPIO自检+WiFi自检 |
| **REQ-041** | 设备唯一ID绑定 | 🟢 P3 | 无 | MAC/Flash UID生成设备ID |
| **REQ-042** | 云端管理平台对接 | 🟢 P3 | 无 | MQTT注册+心跳+远程命令 |
| **REQ-043** | 低功耗模式 | 🟢 P3 | 无 | deep sleep + GPIO唤醒 |
| **REQ-044** | 安全加固 | 🟢 P3 | API Key明文存Flash | Flash加密+AES Key存储 |

---

## 三、里程碑定义

```
Phase 1: 稳定化    v1.0.1  ─── 1周 ─── 目标: DHCP 48h无crash
Phase 2: 配置恢复  v1.0.2  ─── 1周 ─── 目标: board_config完整链路
Phase 3: OTA       v1.1.0  ─── 2周 ─── 目标: 远程升级可用
Phase 4: 产品化    v1.2.0  ─── 2周 ─── 目标: 终端用户可自助配网
Phase 5: 生产就绪  v1.5.0  ─── 3周 ─── 目标: 可批量生产
                                   ─────
                                   共 9 周
```

### 里程碑到达条件

| 里程碑 | 硬性条件 | 软性条件 |
|--------|----------|----------|
| **M1: 稳定化** | DHCP零crash 48h + 断连自动恢复 | 内存水位可观测 |
| **M2: 配置恢复** | 5块板board.json差异化配置通过 | 硬件工程师可自助改配置 |
| **M3: OTA** | 远程升级成功率 ≥ 95% | 升级失败自动回滚 |
| **M4: 产品化** | 非技术人员10分钟内完成配网 | 中文对话体验流畅 |
| **M5: 生产就绪** | 出厂测试覆盖100% GPIO | 安全审计通过 |

---

## 四、子代理任务分派

### Phase 1: 稳定化 (当前Phase)

| 代理 | 角色 | 任务 | 产物 |
|------|------|------|------|
| **qa-engineer** | 测试 | DHCP crash复现测试(48h soak), 内存水位监控 | 长稳测试报告 |
| **firmware-dev** | 开发 | REQ-002 WiFi断连重连, REQ-005 LLM超时重试 | PR + 烧录验证 |
| **bl618claw** | 平台专家 | REQ-003 boot_media时序审查 | 平台审查报告 |
| **code-reviewer** | 审查 | Phase 1所有commit审查(莫石海16项规范) | 审查报告 |
| **architect** | 架构 | 内存泄漏分析(REQ-004), 重连架构评审 | 架构建议 |

**Phase 1 开工顺序:** qa先跑长稳(并行) → fd开发重连+超时 → bl618claw审查boot → code-reviewer做增量审查

### Phase 2: board_config恢复

| 代理 | 任务 |
|------|------|
| **firmware-dev** | REQ-010~013 实现 (恢复parser + 解除DISABLED) |
| **architect** | board_config数据结构审查 (兼容性检查) |
| **code-reviewer** | 代码审查 |
| **qa-engineer** | 5块板差异化烧录测试 |
| **vitecoder** | REQ-014 board.json模板文档 |

### Phase 3: OTA + 配置热更新

| 代理 | 任务 |
|------|------|
| **firmware-dev** | REQ-020~022 OTA核心 (下载+校验+A/B) |
| **bl618claw** | BL618 Flash分区表设计 + bootloader审查 |
| **architect** | OTA整体架构设计 |
| **vitecoder** | REQ-023 Web UI上传功能 |

### Phase 4: 产品化增强

| 代理 | 任务 |
|------|------|
| **firmware-dev** | REQ-030 串口中文, REQ-032 MQTT, REQ-033 消息总线v2 |
| **architect** | REQ-034 技能系统设计 |
| **bl618claw** | REQ-035 WiFi配网Portal (BL618 AP模式) |
| **skillcoder** | REQ-034 Skills运行时实现 |

### Phase 5: 生产就绪

| 代理 | 任务 |
|------|------|
| **all** | 按REQ-040~044分派, 此Phase遥远待细化 |

---

## 五、风险管理

| 风险 | 概率 | 影响 | 缓解措施 |
|------|:----:|:----:|----------|
| DHCP crash根本原因未找到 | 中 | 🔴致命 | Phase 1优先做长稳, ≥3轮无果立刻拉architect+bl618claw三方会诊 |
| board_config恢复破坏BSS布局 | 低 | 🔴致命 | 恢复后重新做V1.0.0基线对比, 每步commit后烧录验证 |
| OTA A/B分区Flash空间不足 | 中 | 🟡严重 | Phase 3启动前先做Flash分区容量评估 |
| CH341烧录不稳定 | 高 | 🟡严重 | 已回退到115200波特率, 准备好备用烧录器 |

---

## 六、沟通与交付节奏

- **每日**: fd commit & push, PM审查git log
- **每2天**: fd + qa同步进度 (子代理间直接沟通, 不过PM中转)
- **每个Phase结束**: 三方会诊 (architect + bl618claw + code-reviewer)
- **遇到2轮修复无效**: 立刻拉三方会诊, 不等用户催
- **烧录新固件后**: qa自动把API Key发送给LLM测试
- **阻塞项更新**: 桌面 `mimiclaw-test-log.md` 实时更新

---

*本方案由AI PM基于代码库分析和项目历史制定, 待莫石海确认后执行。*
