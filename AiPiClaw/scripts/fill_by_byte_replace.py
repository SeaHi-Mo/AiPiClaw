#!/usr/bin/env python3
"""
fill_by_byte_replace.py — 字节替换方式填充 BL系列产品定制信息确认函 V1.2

原理：解压 xlsx，修改 xl/sharedStrings.xml 中的特定字符串值，
然后重新打包。保留所有 VML、控件、格式。

使用方法：
  1. 编辑 TEMPLATE / OUTPUT_DIR / OUTPUT_NAME
  2. 编辑 REPLACEMENTS 字典（sharedStrings 原始字符串 → 目标字符串）
  3. python3 fill_by_byte_replace.py
"""

import os
import shutil
import tempfile
import zipfile

# ==================== 编辑以下参数 ====================

TEMPLATE = "/home/seahi/workspase/firmware-partnumber-application/templates/固件确认函-底板.xlsx"
OUTPUT_DIR = "/mnt/d/Users/Seahi/Desktop/AiPiClaw"
OUTPUT_NAME = "Ai-Thinker_Ai-WV01-32S_WWXH-Zh_UART-MCP_v1.0产品定制信息确认函 V1.2.xlsx"

# 替换映射：原始字符串 → 目标字符串
# 这些是 sharedStrings.xml 中需要替换的标签和数据单元格引用的值
# 注意：标签文字不能动（比如 "固件MD5" 是标签），这里只替换数据值
REPLACEMENTS = {
    # --- Sheet1 数据值 ---
    # B3: 当前指向 si=4="Boot2 MD5"（标签），需要先确认 H1/B3/B4 等数据单元格指向的确切字符串
    # 实际上空白模板的数据单元格都指向标签字符串，需要改为独立的新值
    # 所以更好的方式是：在 sharedStrings 末尾追加新字符串，更新单元格 v 索引
}

# 不行，字节替换不适合追加字符串。直接用已有的 fill_blank_template.py 方法。
#  但用户要求"字节替换方式"，那就在 sharedStrings.xml 中直接找字符串替换。
# 
# 检查：如果数据单元格就是指向标签字符串（比如 B3 的 si=4="Boot2 MD5"），
# 那替换 "Boot2 MD5" 为 "3cd4d4..." 会把标签和数据一起改掉——不行！
#
# 所以这个模板没有预置独立的数据值单元格。需要用追加字符串 + 更新索引的方式，
# 这本质是 XML 操作而非纯字节替换。

# 重新评估：底板有两种情况
# A) 空白模板：只有标签，数据单元格指向标签索引 —— 必须追加新字符串
# B) 已有数据模板：数据单元格已有独立值（不是标签引用）

# 检查当前模板是哪种
print("需要先检查模板类型...")
