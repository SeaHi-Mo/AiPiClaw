#!/usr/bin/env python3
"""
AiPiClaw firmware annotation script.
Adds Chinese @brief/@param/@return comments to uncommented functions,
and /*< */ field annotations for struct members.
Skips files already done in platform/.
"""

import os
import re
import sys

PROJECT_ROOT = "/home/seahi/workspase/AiPiClaw/AiPiClaw"

# ─── Knowledge base for generating intelligent comments ───

FUNC_COMMENTS = {
    # ── axk_llm_proxy.c static functions ──
    "safe_copy": ("安全字符串拷贝，自动处理 NULL 和溢出", "dst 目标缓冲区", "dst_size 缓冲区大小", "src 源字符串", None),
    "provider_is_openai": ("判断当前 LLM 提供商是否为 OpenAI", None, None, "是 OpenAI 返回 true", None),
    "provider_is_deepseek": ("判断当前 LLM 提供商是否为 DeepSeek", None, None, "是 DeepSeek 返回 true", None),
    "provider_is_minimax": ("判断当前 LLM 提供商是否为 MiniMax", None, None, "是 MiniMax 返回 true", None),
    "provider_uses_openai_format": ("判断当前提供商是否使用 OpenAI 兼容 API 格式", None, None, "使用 OpenAI 格式返回 true", None),
    "llm_api_url": ("根据当前提供商返回对应的 API endpoint URL", None, None, "API URL 字符串指针", None),
    "normalize_model_for_provider": ("根据提供商规范化模型名称，不匹配时使用默认模型", None, None, "模型发生变更返回 true", None),
    "llm_resp_buf_append": ("向 LLM 响应缓冲区追加数据，自动扩容", "rb 响应缓冲区", "data 待追加数据", "len 数据长度", "成功返回 0，OOM 返回 -1"),
    "llm_http_response_cb": ("HTTPS 响应回调函数，累积响应 body", "rsp HTTP 响应结构", "final_data 最终数据标志", "user_data llm_resp_buf_t 缓冲区指针", None),
    "llm_http_call": ("发起 LLM HTTP API 调用", "post_data POST JSON 请求体", "rb 响应缓冲区", "成功返回 0，失败返回 -1"),
    "convert_tools_openai": ("将 Anthropic 格式 tools JSON 转换为 OpenAI function-calling 格式", "tools_json Anthropic 格式 tools JSON", None, "转换后的 cJSON 数组，需调用者释放"),
    "convert_messages_openai": ("将 Anthropic 格式 messages 转换为 OpenAI chat completion 格式", "system_prompt 系统提示词", "messages 原始 cJSON messages 数组", "转换后的 cJSON 消息数组"),
    "axk_llm_response_free": ("释放 LLM 响应结构体中动态分配的内存", "resp LLM 响应结构体", None, None),
    
    # ── axk_agent_loop.c static functions ──
    "str_contains_nocase": ("大小写不敏感的子串查找", "haystack 待搜索字符串", "needle 查找目标", "找到返回 true"),
    "text_has_current_time_anchor": ("检查文本中是否包含实时性关键词（today/今天/最新等）", "text 待检查文本", None, "包含实时性关键词返回 true"),
    "extract_explicit_year": ("从文本中提取显式指定的四位年份", "text 待搜索文本", None, "年份值，未找到返回 0"),
    "get_current_year": ("获取当前 RTC 年份", "year_out 年份输出", None, "RTC 时间有效返回 true"),
    "should_anchor_web_search_to_user": ("判断是否需要将 web_search 查询锚定到用户原始提问", "user_query 用户原始问题", "tool_query LLM 生成的工具查询", "需要锚定返回 true"),
    "rewrite_web_search_input": ("用替换查询重写 web_search 工具输入 JSON", "tool_input 原始工具输入 JSON", "replacement_query 替换查询字符串", "重写后的 JSON 字符串，需调用者释放"),
    "prepare_tool_inputs": ("为工具调用准备输入参数（锚定 web_search 查询）", "resp LLM 响应", "user_query 用户原始问题", "tool_inputs 输出的工具输入数组", None),
    "free_tool_inputs": ("释放 prepare_tool_inputs 分配的输入字符串数组", "resp LLM 响应", "tool_inputs 工具输入数组", None),
    "build_system_prompt": ("构建含当前日期的系统提示词", "buf 输出缓冲区", "size 缓冲区大小", None),
    "build_assistant_content": ("构建助手消息的 content 数组（文本 + 工具调用）", "resp LLM 响应", "tool_inputs 工具输入数组", "cJSON content 数组"),
    "build_tool_results": ("执行工具调用并构建工具结果消息", "resp LLM 响应", "tool_inputs 工具输入数组", "tool_output 工具输出缓冲区", "tool_output_size 缓冲区大小", "cJSON tool_result 数组"),
    "agent_loop_task": ("Agent 主循环 FreeRTOS 任务：等待消息 → 技能匹配 → LLM 推理 → 工具调用 → 响应输出", "arg 任务参数（未使用）", None, None),
    
    # ── axk_telegram_bot.c static functions ──
    "tg_is_integer_string": ("判断字符串是否为整数格式", "s 待检查字符串", None, "是整数返回 true"),
    "tg_trim_spaces": ("去除字符串首尾空白字符（原地修改）", "s 待处理字符串", None, None),
    "tg_normalize_chat_id": ("规范化 Telegram chat_id（处理科学计数法表示的大整数）", "in 原始 chat_id", "out 输出缓冲区", "out_size 缓冲区大小", "成功返回 true"),
    "tg_parse_i64": ("安全解析 64 位整数", "s 整数字符串", "out 输出值", "解析成功返回 true"),
    "tg_debug_get_chat_once": ("执行一次 getChat API 调试调用", "chat_id 聊天 ID", "numeric_chat_id 是否为纯数字 ID", None),
    "tg_debug_get_chat": ("以字符串和数字两种格式尝试 getChat 调试", "chat_id 聊天 ID", "numeric_chat_id 是否为纯数字 ID", None),
    "tg_resp_append": ("向 Telegram HTTP 响应缓冲区追加数据", "rb 响应缓冲区", "data 数据指针", "len 数据长度", "成功返回 0"),
    "tg_http_response_cb": ("Telegram HTTP 响应回调，累积 body 和状态码", "rsp HTTP 响应", "final_data 最终标志", "user_data tg_http_resp_t 指针", None),
    "tg_api_call": ("调用 Telegram Bot API 方法", "method API 方法名", "post_data POST 数据", "out_body 输出响应体", "out_status 输出 HTTP 状态码", "成功返回 0"),
    "tg_response_is_ok": ("判断 Telegram API 响应 ok 字段", "body API 响应 JSON", None, "ok 为 true 返回 true"),
    "tg_parse_chat_id": ("从 cJSON 对象中提取并规范化 chat_id", "chat_id cJSON 对象", "out 输出缓冲区", "out_size 缓冲区大小", "成功返回 true"),
    "tg_format_chat_id_debug": ("格式化 chat_id 用于调试日志输出", "item cJSON 对象", "buf 输出缓冲区", "buf_size 缓冲区大小", None),
    "tg_save_offset_if_needed": ("按条件保存 update_offset 到 Flash KV 存储", "force 强制保存标志", None, None),
    "tg_process_updates": ("处理 getUpdates 返回的 JSON 更新列表，分派消息到 message bus", "json_str getUpdates 响应 JSON", None, None),
    
    # ── axk_feishu_bot.c static functions ──
    "fs_resp_append": ("向飞书 HTTP 响应缓冲区追加数据", "rb 响应缓冲区", "data 数据", "len 长度", "成功返回 0"),
    "fs_http_response_cb": ("飞书 HTTP 响应回调函数", "rsp 响应结构", "final_data 最终标志", "user_data fs_http_resp_t 指针", None),
    "fs_https_post": ("发起飞书 HTTPS POST 请求", "url 请求 URL", "payload POST body", "auth_token 认证令牌", "out_body 输出响应体", "out_status 输出状态码", "成功返回 0"),
    
    # ── axk_tool_web_search.c static functions ──
    "provider_is_bocha": ("判断当前搜索引擎是否为 Bocha", None, None, "是 Bocha 返回 true"),
    "provider_is_brave": ("判断当前搜索引擎是否为 Brave", None, None, "是 Brave 返回 true"),
    "provider_is_supported": ("判断搜索引擎提供商是否支持", "provider 提供商名", None, "支持返回 true"),
    
    # ── axk_tool_get_time.c static functions ──
    "rtc_time_plausible": ("检查 RTC 时间是否在合理范围内（2024-2199）", None, None, "时间合理返回 true"),
    "day_of_year": ("计算指定日期是一年中的第几天", "year 年份", "month 月份", "day 日期", "一年中的第几天（0-based）"),
    
    # ── axk_wifi_manager.c static functions ──
    "get_tick_ms": ("获取当前 FreeRTOS 滴答数（毫秒）", None, None, "毫秒级滴答数"),
    
    # ── axk_tool_files.c functions ──
    "axk_tool_files_init": ("初始化文件系统工具模块（LittleFS + FatFS）", None, None, "成功返回 0"),
    
    # ── axk_gpio_policy.c functions ──
    "axk_gpio_policy_init": ("初始化 GPIO 引脚权限策略（白名单）", None, None, "成功返回 0"),
    "axk_gpio_policy_check": ("检查指定引脚和操作是否在允许范围内", "pin 引脚号", "action 操作名", "通过返回 0，拒绝返回 -1"),
}


def has_doxygen_before(lines, func_line_idx):
    """Check if a function has a doxygen comment block right before it."""
    if func_line_idx == 0:
        return False
    # Check up to 10 lines before for @brief or /** pattern
    start = max(0, func_line_idx - 10)
    prev_lines = lines[start:func_line_idx]
    prev_text = '\n'.join(prev_lines)
    if '@brief' in prev_text or '/**' in prev_text:
        return True
    return False


def find_functions(lines):
    """Find all function definitions with their line numbers."""
    funcs = []
    # Match function definitions: return_type func_name(params)
    # Pattern: starts with word chars or * for pointer return, then func_name(
    func_pattern = re.compile(
        r'^(static\s+)?(?:inline\s+)?'
        r'(?:const\s+)?(?:volatile\s+)?'
        r'(?:unsigned\s+)?(?:signed\s+)?'
        r'[a-zA-Z_][a-zA-Z0-9_*\s]+'  # return type
        r'\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\(',  # function name
        re.MULTILINE
    )
    
    for i, line in enumerate(lines):
        stripped = line.strip()
        # Skip preprocessor, comments, and lines without (
        if stripped.startswith('#') or stripped.startswith('//') or stripped.startswith('/*'):
            continue
        if stripped.startswith('*') or stripped.startswith('/**'):
            continue
        if '(' not in stripped:
            continue
        
        m = func_pattern.match(stripped)
        if m:
            func_name = m.group(1)
            # Skip known non-functions
            if func_name in ('if', 'while', 'for', 'switch', 'return', 'sizeof', 'case', 'default'):
                continue
            funcs.append((i, func_name, stripped))
    
    return funcs


def generate_comment(func_name, lines, func_line_idx):
    """Generate a Chinese doxygen comment for a function."""
    # Check knowledge base first
    if func_name in FUNC_COMMENTS:
        info = FUNC_COMMENTS[func_name]
        comment_lines = ['/**']
        # @brief
        if info[0]:
            comment_lines.append(f' * @brief {info[0]}')
        
        # @param
        params = info[1:-1]  # Everything between brief and return
        param_names = extract_param_names(lines, func_line_idx)
        param_idx = 0
        for p in params:
            if p is None and param_idx < len(param_names):
                comment_lines.append(f' * @param[in] {param_names[param_idx]} 参数')
                param_idx += 1
            elif p:
                if param_idx < len(param_names):
                    comment_lines.append(f' * @param[in] {param_names[param_idx]} {p}')
                else:
                    comment_lines.append(f' * @param {p}')
                param_idx += 1
        
        # @return
        ret = info[-1]
        if ret:
            comment_lines.append(f' * @return {ret}')
        
        comment_lines.append(' */')
        return '\n'.join(comment_lines)
    
    # Fallback: generate generic comment
    comment_lines = ['/**']
    comment_lines.append(f' * @brief {func_name} 函数')
    param_names = extract_param_names(lines, func_line_idx)
    for p in param_names:
        if p != 'void':
            comment_lines.append(f' * @param[in] {p} 参数')
    comment_lines.append(' * @return 执行结果')
    comment_lines.append(' */')
    return '\n'.join(comment_lines)


def extract_param_names(lines, func_line_idx):
    """Extract parameter names from a function definition."""
    if func_line_idx >= len(lines):
        return []
    
    # Get the full function signature (may span multiple lines)
    sig_lines = []
    for j in range(func_line_idx, min(func_line_idx + 10, len(lines))):
        sig_lines.append(lines[j])
        full = ' '.join(s.strip() for s in sig_lines)
        if ')' in full:
            break
    
    full_sig = ' '.join(s.strip() for s in sig_lines)
    
    # Extract content between outermost parentheses of function definition
    paren_start = full_sig.find('(')
    if paren_start < 0:
        return []
    
    # Find matching close paren
    depth = 0
    paren_end = -1
    for i in range(paren_start, len(full_sig)):
        if full_sig[i] == '(':
            depth += 1
        elif full_sig[i] == ')':
            depth -= 1
            if depth == 0:
                paren_end = i
                break
    
    if paren_end < 0:
        return []
    
    params_str = full_sig[paren_start + 1:paren_end].strip()
    if not params_str or params_str == 'void':
        return []
    
    # Split by comma, but respect nesting
    params = []
    depth = 0
    current = []
    for ch in params_str:
        if ch in '([{':
            depth += 1
        elif ch in ')]}':
            depth -= 1
        if ch == ',' and depth == 0:
            params.append(''.join(current).strip())
            current = []
        else:
            current.append(ch)
    if current:
        params.append(''.join(current).strip())
    
    param_names = []
    for p in params:
        # Find the last word (parameter name) - handle pointer types
        p = p.strip()
        # Remove trailing brackets for arrays
        p = re.sub(r'\[.*\]', '', p)
        # Get last identifier
        parts = p.split()
        if parts:
            last = parts[-1].strip('*').strip()
            if re.match(r'^[a-zA-Z_][a-zA-Z0-9_]*$', last):
                param_names.append(last)
    
    return param_names


def annotate_file(filepath):
    """Add Chinese annotations to a C/H file."""
    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
    
    lines = content.split('\n')
    funcs = find_functions(lines)
    
    if not funcs:
        print(f"  No functions found in {filepath}")
        return False
    
    modified = False
    # Process in reverse order to maintain line indices
    for func_line_idx, func_name, func_sig in reversed(funcs):
        if has_doxygen_before(lines, func_line_idx):
            continue
        
        # Skip main, simple callbacks, well-known patterns
        if func_name in ('main',):
            continue
        
        comment = generate_comment(func_name, lines, func_line_idx)
        
        # Find insertion point (after any preceding blank lines)
        insert_idx = func_line_idx
        while insert_idx > 0 and lines[insert_idx - 1].strip() == '':
            insert_idx -= 1
        
        # Insert the comment
        comment_lines = comment.split('\n')
        new_lines = lines[:insert_idx] + comment_lines + [''] + lines[insert_idx:]
        lines = new_lines
        modified = True
        print(f"  ✓ Added @brief to {func_name}()")
    
    if not modified:
        print(f"  All functions already annotated in {filepath}")
        return False
    
    with open(filepath, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    
    return True


def add_struct_field_comments(filepath):
    """Add /*< */ comments to struct fields that lack them."""
    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
    
    lines = content.split('\n')
    modified = False
    
    # Field name → comment mapping
    field_comments = {
        # Common response buffer struct fields
        'data': '响应数据缓冲区',
        'len': '已接收数据长度',
        'cap': '缓冲容量',
        'status_code': 'HTTP 状态码',
        'oom': '内存不足标志',
    }
    
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        # Find struct field declarations: type name;
        # Skip lines that already have /*< or //
        if '/*<' in stripped or ('//' in stripped and not stripped.startswith('//')):
            i += 1
            continue
        
        # Match simple field: whitespace + type + name + semicolon
        m = re.match(r'^(\s+)([a-zA-Z_][a-zA-Z0-9_*\s]+?)([a-zA-Z_][a-zA-Z0-9_]*)\s*;', stripped)
        if m:
            indent = m.group(1)
            field_name = m.group(3)
            if field_name in field_comments:
                # Check if inside a struct
                # Simple heuristic: look back for struct keyword
                in_struct = False
                for j in range(max(0, i - 50), i):
                    if re.search(r'typedef\s+struct|struct\s+\w*\s*\{', lines[j]):
                        in_struct = True
                        break
                
                if in_struct:
                    new_line = line.rstrip() + f'  /**< {field_comments[field_name]} */'
                    lines[i] = new_line
                    modified = True
                    print(f"  ✓ Added struct field comment: {field_name}")
        
        i += 1
    
    if modified:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write('\n'.join(lines))
    
    return modified


def annotate_header(filepath):
    """Add function documentation to a header file's public API."""
    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
    
    lines = content.split('\n')
    modified = False
    
    header_funcs = {
        # axk_agent_loop.h
        'axk_agent_loop_init': ('初始化 Agent 主循环模块', None, None, '成功返回 0'),
        'axk_agent_loop_run': ('Agent 主循环运行（兼容性保留接口）', None, None, None),
        'axk_agent_loop_start': ('启动 Agent 主循环 FreeRTOS 任务', None, None, '成功返回 0，失败返回 -1'),
        
        # axk_context_builder.h
        'axk_context_builder_init': ('初始化上下文构建器', None, None, '成功返回 0'),
        'axk_context_builder_get_system_prompt': ('获取系统提示词字符串', None, None, '提示词字符串指针'),
        'axk_context_builder_build_request': ('构建完整 LLM 请求 JSON', 'user_message 用户消息', 'tools_json 工具 JSON', 'session_context 会话上下文', '请求 JSON 字符串，需调用者释放'),
        
        # axk_tool_web_search.h functions
        'axk_tool_web_search_init': ('初始化网络搜索工具', None, None, '成功返回 0'),
        
        # axk_tool_get_time.h functions
        'axk_tool_get_time_init': ('初始化时间获取工具', None, None, '成功返回 0'),
        
        # axk_tool_gpio.h functions
        'axk_tool_gpio_init': ('初始化 GPIO 控制工具', None, None, '成功返回 0'),
        
        # axk_tool_files.h functions
        'axk_tool_files_init': ('初始化文件管理工具模块', None, None, '成功返回 0'),
        
        # axk_tool_cron.h functions
        'axk_tool_cron_init': ('初始化定时任务工具', None, None, '成功返回 0'),
        'axk_tool_cron_add_execute': ('执行 cron_add 工具调用', 'input_json 输入 JSON', 'output 输出缓冲区', 'output_size 缓冲区大小', '成功返回 0'),
        'axk_tool_cron_list_execute': ('执行 cron_list 工具调用', 'input_json 输入 JSON', 'output 输出缓冲区', 'output_size 缓冲区大小', '成功返回 0'),
        'axk_tool_cron_remove_execute': ('执行 cron_remove 工具调用', 'input_json 输入 JSON', 'output 输出缓冲区', 'output_size 缓冲区大小', '成功返回 0'),
        
        # axk_tool_registry.h functions
        'axk_tool_registry_init': ('初始化工具注册中心', None, None, '成功返回 0'),
        'axk_tool_registry_get_tools_json': ('获取所有已注册工具的 JSON Schema', None, None, '工具 JSON 字符串'),
        'axk_tool_registry_execute': ('根据工具名执行对应工具', 'name 工具名', 'input_json 输入参数 JSON', 'output 输出缓冲区', 'output_size 缓冲区大小', '成功返回 0'),
    }
    
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        
        # Find function declarations
        m = re.match(r'^(int|void|char\s*\*|const\s+char\s*\*|bool|axk_wifi_state_t)\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\(', stripped)
        if m:
            func_name = m.group(2)
            if func_name in header_funcs and not has_doxygen_before(lines, i):
                info = header_funcs[func_name]
                comment_lines = ['/**']
                if info[0]:
                    comment_lines.append(f' * @brief {info[0]}')
                params = info[1:-1]
                param_names = extract_param_names(lines, i)
                for p_idx, p in enumerate(params):
                    if p and p_idx < len(param_names):
                        comment_lines.append(f' * @param[in] {param_names[p_idx]} {p}')
                ret = info[-1]
                if ret:
                    comment_lines.append(f' * @return {ret}')
                comment_lines.append(' */')
                
                # Insert before the function declaration
                insert_idx = i
                while insert_idx > 0 and lines[insert_idx - 1].strip() == '':
                    insert_idx -= 1
                
                new_lines = lines[:insert_idx] + comment_lines + [''] + lines[insert_idx:]
                lines = new_lines
                i += len(comment_lines) + 1
                modified = True
                print(f"  ✓ Added header doc to {func_name}()")
        
        i += 1
    
    if modified:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write('\n'.join(lines))
    
    return modified


# ─── Main processing ───

FILES_TO_PROCESS = [
    # Priority 1: Large C files needing static function annotations
    "src/llm/axk_llm_proxy.c",
    "src/gateway/axk_telegram_bot.c",
    "src/gateway/axk_feishu_bot.c",
    "src/tools/axk_tool_web_search.c",
    "src/tools/axk_tool_files.c",
    "src/agent/axk_agent_loop.c",
    
    # Priority 2: Medium C files
    "src/tools/axk_tool_get_time.c",
    "src/tools/axk_tool_cron.c",
    "src/tools/axk_tool_gpio.c",
    "src/wifi/axk_wifi_manager.c",
    "src/gateway/axk_ws_server.c",
    "src/tools/axk_tool_registry.c",
    "src/tools/axk_gpio_policy.c",
    
    # Priority 3: Headers needing function doc
    "src/agent/axk_agent_loop.h",
    "src/agent/axk_context_builder.h",
    "src/tools/axk_tool_web_search.h",
    "src/tools/axk_tool_get_time.h",
    "src/tools/axk_tool_gpio.h",
    "src/tools/axk_tool_files.h",
    "src/tools/axk_tool_cron.h",
    "src/tools/axk_tool_registry.h",
    "src/tools/axk_gpio_policy.h",
    "src/cli/axk_serial_cli.h",
    "src/gateway/axk_http_proxy.h",
    "src/llm/axk_llm_proxy.h",
    "src/bus/axk_message_bus.h",
    
    # Priority 4: Other remaining files
    "src/onboard/axk_wifi_onboard.c",
    "src/ota/axk_ota_manager.c",
    "src/heartbeat/axk_heartbeat.c",
    "src/gpio/axk_gpio_control.c",
    "src/cron/axk_cron_service.c",
    "src/skills/axk_skill_loader.c",
    "src/memory/axk_memory_store.c",
    "src/memory/axk_storage.c",
    "src/memory/axk_session_mgr.c",
]

SKIP_FILES = [
    # Already done in platform/
    "src/platform/",
    # Header-only, already annotated
    "src/gateway/web_ui.h",
    "src/gateway/axk_http_proxy.c",  # already well annotated
    "src/gateway/axk_transport_ws.c",  # already well annotated
    "src/gateway/axk_ws_server.h",  # already done
    "src/gateway/axk_feishu_bot.h",  # already done
    "src/gateway/axk_telegram_bot.h",  # already annotated
    "src/cli/axk_serial_cli.c",  # already well annotated
    "src/tools/axk_tool_registry.c",  # checked, has annotations
    "src/tools/axk_gpio_policy.c",  # already well annotated
    "src/wifi/axk_wifi_manager.h",  # already well annotated
    "main.c",  # already well annotated
    "src/include/mimi_config.h",
    "src/include/axk_mimiclaw.h",
    "src/include/axk_storage.h",
    "src/include/axk_mimiclaw_port.h",
    "src/include/axk_mimiclaw_ext_rtc.h",
    "src/include/axk_mimiclaw_tool_web_search_port.h",
]

def should_skip(filepath):
    for skip in SKIP_FILES:
        if skip in filepath:
            return True
    return False


def main():
    print("=" * 60)
    print("AiPiClaw Firmware Annotation Tool")
    print("=" * 60)
    
    total_annotated = 0
    total_skipped = 0
    
    for rel_path in FILES_TO_PROCESS:
        full_path = os.path.join(PROJECT_ROOT, rel_path)
        
        if should_skip(rel_path):
            continue
        
        if not os.path.exists(full_path):
            print(f"⚠ File not found: {rel_path}")
            continue
        
        print(f"\n📄 Processing: {rel_path}")
        
        if rel_path.endswith('.h'):
            modified = annotate_header(full_path)
        else:
            modified = annotate_file(full_path)
            if modified or True:  # Always try struct fields
                add_struct_field_comments(full_path)
        
        if modified:
            total_annotated += 1
        else:
            total_skipped += 1
    
    print(f"\n{'=' * 60}")
    print(f"Results: {total_annotated} annotated, {total_skipped} already done")
    print(f"{'=' * 60}")


if __name__ == '__main__':
    main()
