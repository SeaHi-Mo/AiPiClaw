cmake_minimum_required(VERSION 3.15)

find_package(bouffalo_sdk REQUIRED HINTS $ENV{BL_SDK_BASE})

# 安信可科技定制：MimiClaw BL618移植项目编译配置
# 本文件定义了所有模块的编译源文件和包含路径

# 添加编译宏定义
sdk_add_compile_definitions(-DCONFIG_CLI_CMD_ENABLE)
sdk_add_compile_definitions(-DWEBSOCKET_SSL_CHECK_HOSTNAME)
sdk_add_compile_definitions(-DSHELL_CMD_SIZE=512)

# v78: 禁用 WSC/WPS 避免 wsc_registrar 溢出，STA-only 无需 AP/WPS 代码
sdk_add_compile_options(-UCONFIG_WPS2 -UCONFIG_WPS_PIN -UUSE_WPS_TASK)

# 包含路径配置
sdk_add_include_directories(src/include)
sdk_add_include_directories(src/platform)
sdk_add_include_directories(src/bus)
sdk_add_include_directories(src/gateway)
sdk_add_include_directories(src/cron)
sdk_add_include_directories(compat)
sdk_add_include_directories(compat/freertos)

# 上游模块头文件路径 (仅用于编译上游核心代码)
sdk_add_include_directories(upstream)
sdk_add_include_directories(upstream/cron)
sdk_add_include_directories(upstream/tools)

# 创建平台抽象层库
sdk_generate_library(axk_platform_hal)
sdk_library_add_sources(src/platform/axk_hal_gpio.c)
sdk_library_add_sources(src/platform/axk_hal_uart.c)
sdk_library_add_sources(src/platform/axk_hal_wifi.c)
sdk_library_add_sources(src/platform/axk_hal_flash.c)
sdk_library_add_sources(src/platform/axk_hal_system.c)
sdk_library_add_sources(src/platform/axk_hal_timer.c)
sdk_library_add_sources(src/platform/axk_mimiclaw_port.c)
sdk_library_add_sources(src/platform/axk_mimiclaw_ext_rtc.c)

# 消息总线模块
sdk_library_add_sources(src/bus/axk_message_bus.c)

# 工具注册表模块
sdk_library_add_sources(src/tools/axk_tool_registry.c)

# 心跳模块
sdk_add_include_directories(src/heartbeat)
sdk_library_add_sources(src/heartbeat/axk_heartbeat.c)

# 内存管理模块
sdk_add_include_directories(src/memory)
sdk_library_add_sources(src/memory/axk_memory_store.c)
sdk_library_add_sources(src/memory/axk_session_mgr.c)
sdk_library_add_sources(src/memory/axk_context_summary.c)
sdk_library_add_sources(src/memory/axk_storage.c)

# WiFi管理模块
sdk_add_include_directories(src/wifi)
sdk_library_add_sources(src/wifi/axk_wifi_manager.c)

# 串口命令行模块
sdk_add_include_directories(src/cli)
sdk_library_add_sources(src/cli/axk_serial_cli.c)

# 智能体主循环模块
sdk_add_include_directories(src/agent)
sdk_library_add_sources(src/agent/axk_agent_loop.c)
sdk_library_add_sources(src/agent/axk_context_builder.c)

# 技能加载器模块
sdk_add_include_directories(src/skills)
sdk_library_add_sources(src/skills/axk_skill_loader.c)

# OTA升级模块
sdk_add_include_directories(src/ota)
sdk_library_add_sources(src/ota/axk_ota_manager.c)

# WiFi配网模块
sdk_add_include_directories(src/onboard)
sdk_library_add_sources(src/onboard/axk_wifi_onboard.c)

# GPIO控制模块
sdk_add_include_directories(src/gpio)
sdk_library_add_sources(src/gpio/axk_gpio_control.c)

# 网关模块
sdk_add_include_directories(src/gateway)
sdk_library_add_sources(src/gateway/axk_ws_server.c)
sdk_library_add_sources(src/gateway/axk_http_proxy.c)
sdk_library_add_sources(src/gateway/axk_transport_ws.c)
sdk_library_add_sources(src/gateway/axk_telegram_bot.c)
sdk_library_add_sources(src/gateway/axk_feishu_bot.c)
sdk_library_add_sources(src/gateway/axk_config_portal.c)

# LLM代理模块
sdk_add_include_directories(src/llm)
sdk_library_add_sources(src/llm/axk_llm_proxy.c)

# 工具模块
sdk_add_include_directories(src/tools)
sdk_library_add_sources(src/tools/axk_tool_get_time.c)
sdk_library_add_sources(src/tools/axk_tool_files.c)
sdk_library_add_sources(src/tools/axk_tool_web_search.c)
sdk_library_add_sources(src/tools/axk_tool_gpio.c)
sdk_library_add_sources(src/tools/axk_gpio_policy.c)
sdk_library_add_sources(src/tools/axk_gpio_alias.c)
sdk_library_add_sources(src/tools/axk_tool_gpio_named.c)

# 定时任务服务 (上游代码)
sdk_library_add_sources(upstream/cron/cron_service.c)
sdk_library_add_sources(upstream/tools/tool_cron.c)

# 堆兼容层
sdk_library_add_sources(compat/heap_compat.c)

# 设置主文件
sdk_set_main_file(main.c)

# ── Web UI 嵌入 (每次构建自动从 HTML 生成 C 头文件) ──
set(CONFIG_UI_HTML  "/mnt/d/Users/Seahi/Desktop/agent-reports/config_ui.html")
set(WEB_UI_HTML     "/mnt/d/Users/Seahi/Desktop/agent-reports/web_ui.html")
add_custom_command(
    OUTPUT  ${CMAKE_CURRENT_SOURCE_DIR}/src/gateway/config_ui.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/gateway/web_ui.h
    COMMAND ${CMAKE_COMMAND} -E env
            python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_html.py
            "${CONFIG_UI_HTML}"  ${CMAKE_CURRENT_SOURCE_DIR}/src/gateway/config_ui.h  CONFIG_UI_HTML
            "${WEB_UI_HTML}"     ${CMAKE_CURRENT_SOURCE_DIR}/src/gateway/web_ui.h     WEB_UI_HTML
    DEPENDS ${CONFIG_UI_HTML} ${WEB_UI_HTML}
    COMMENT "Embedding Web UI HTML into C headers"
)
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gateway/config_ui.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gateway/web_ui.h
    PROPERTIES GENERATED TRUE
)

project(AiPiClaw)

# ── 板卡配置解析器 (DISABLED: 源文件导致 BSS 偏移 ~3KB) ──
# sdk_library_add_sources(src/board/board_data.c)
# sdk_library_add_sources(src/board/axk_board_config.c)

# 板卡信息工具 (DISABLED: 同上)
# sdk_library_add_sources(src/tools/axk_tool_board_info.c)
# sdk_library_add_sources(src/tools/axk_tool_gpio_list.c)
