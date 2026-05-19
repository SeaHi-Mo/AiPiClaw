#!/usr/bin/env python3
"""
fill_blank_template.py — 填充 BL系列产品定制信息确认函 V1.2 空白模板

使用方法：
  1. 编辑脚本底部 main() 中的 TEMPLATE / OUTPUT_DIR / OUTPUT_NAME
  2. 编辑 SHEET1_DATA / SHEET3_DATA 字典填入实际值
  3. python3 fill_blank_template.py

依赖：Python 3 标准库（无外部依赖）

单元格映射（V1.2 取消Logo版底板）：

Sheet1:
  H1 = 固件流水号（G1 是标签"固件流水号："，不动标签）
  B3 = 固件MD5    B4 = Boot2 MD5
  D3 = eFuse要求  D4 = 运行模式  F4 = 固件版本号  G4 = IOMAP要求
  B6 = AT端口     D6 = AT端口波特率  F6 = 校验位
  B7 = 数据位     D7 = 停止位       F7 = HEX
  B8 = LOG端口    D8 = LOG端口波特率
  B10 = 启动信息  B11 = 特殊指令    B12 = 指令回复

Sheet3:
  A2 = 产品型号  E2 = Flash容量
  A3 = 天线配置  A4 = 三元组提供  A5 = 烧录三元组
  A6 = 软件物料处理  E6 = 硬件物料处理
  A7 = 号码写入  A8 = 包装方式  A9 = 备注
"""

import os
import shutil
import tempfile
import zipfile
import xml.etree.ElementTree as ET
import re

NS_S = '{http://schemas.openxmlformats.org/spreadsheetml/2006/main}'

# 注册默认 namespace 避免出现 ns0: 前缀
ET.register_namespace('', 'http://schemas.openxmlformats.org/spreadsheetml/2006/main')
ET.register_namespace('r', 'http://schemas.openxmlformats.org/officeDocument/2006/relationships')


def _find_existing_si(root, text):
    """在 sharedStrings 根元素中查找已有字符串的索引"""
    for i, si in enumerate(list(root)):
        t = si.find(f'{NS_S}t')
        if t is not None and t.text and t.text.strip() == text:
            return i
    return None


def _append_string(root, text):
    """追加字符串到 sharedStrings，返回新索引"""
    si = ET.SubElement(root, f'{NS_S}si')
    t = ET.SubElement(si, f'{NS_S}t')
    t.set('{http://www.w3.org/XML/1998/namespace}space', 'preserve')
    t.text = text if text else ''
    return len(list(root)) - 1


def _update_cell_value(sheet_xml, cell_ref, si):
    """更新 sheet XML 中指定单元格的 v 值为 sharedStrings 索引 si"""
    root = ET.fromstring(sheet_xml)
    sheet_data = root.find(f'{NS_S}sheetData')
    if sheet_data is None:
        raise ValueError("sheetData not found — XML structure mismatch")

    row_num = int(re.search(r'(\d+)', cell_ref).group(1))
    for row in sheet_data.findall(f'{NS_S}row'):
        if row.get('r') != str(row_num):
            continue
        for c in row.findall(f'{NS_S}c'):
            if c.get('r') != cell_ref:
                continue
            v = c.find(f'{NS_S}v')
            if v is not None:
                v.text = str(si)
            else:
                v = ET.SubElement(c, f'{NS_S}v')
                v.text = str(si)
            c.set('t', 's')
            return ET.tostring(root, encoding='unicode')

    # 单元格不存在，创建
    for row in sheet_data.findall(f'{NS_S}row'):
        if row.get('r') == str(row_num):
            c = ET.SubElement(row, f'{NS_S}c')
            c.set('r', cell_ref)
            c.set('t', 's')
            c.set('s', '0')
            v = ET.SubElement(c, f'{NS_S}v')
            v.text = str(si)
            break

    return ET.tostring(root, encoding='unicode')


def fill_template(template_path, output_path, sheet1_data, sheet3_data):
    """
    填充 V1.2 空白确认函模板。

    参数:
        template_path: 底板 xlsx 路径
        output_path:   输出 xlsx 路径
        sheet1_data:   dict, key=单元格引用(e.g. 'H1'), value=字符串值
        sheet3_data:   dict, key=单元格引用, value=字符串值
    """
    tmp = tempfile.mkdtemp()
    try:
        with zipfile.ZipFile(template_path, 'r') as z:
            z.extractall(tmp)

        ss_path = os.path.join(tmp, 'xl', 'sharedStrings.xml')
        s1_path = os.path.join(tmp, 'xl', 'worksheets', 'sheet1.xml')
        s3_path = os.path.join(tmp, 'xl', 'worksheets', 'sheet3.xml')

        # 读取 sharedStrings
        with open(ss_path, 'r', encoding='utf-8') as f:
            ss_xml = f.read()
        ss_root = ET.fromstring(ss_xml)

        # 收集所有需要追加的值
        all_vals = {}
        for ref, val in {**sheet1_data, **sheet3_data}.items():
            if val:
                all_vals[val] = None

        # 检查是否已存在
        val_to_si = {}
        for v in all_vals:
            idx = _find_existing_si(ss_root, v)
            if idx is not None:
                val_to_si[v] = idx
            else:
                val_to_si[v] = _append_string(ss_root, v)

        # 写回 sharedStrings
        with open(ss_path, 'wb') as f:
            f.write(ET.tostring(ss_root, encoding='utf-8',
                                xml_declaration=True))

        # 更新 Sheet1
        with open(s1_path, 'r', encoding='utf-8') as f:
            s1_xml = f.read()
        for ref, val in sheet1_data.items():
            if val:
                si = val_to_si[val]
                s1_xml = _update_cell_value(s1_xml, ref, si)
        with open(s1_path, 'w', encoding='utf-8') as f:
            f.write(s1_xml)

        # 更新 Sheet3
        with open(s3_path, 'r', encoding='utf-8') as f:
            s3_xml = f.read()
        for ref, val in sheet3_data.items():
            if val:
                si = val_to_si[val]
                s3_xml = _update_cell_value(s3_xml, ref, si)
        with open(s3_path, 'w', encoding='utf-8') as f:
            f.write(s3_xml)

        # 重新打包
        if os.path.exists(output_path):
            os.remove(output_path)
        with zipfile.ZipFile(output_path, 'w', zipfile.ZIP_DEFLATED) as zout:
            for dirpath, _, fnames in os.walk(tmp):
                for fn in fnames:
                    fp = os.path.join(dirpath, fn)
                    zout.write(fp, os.path.relpath(fp, tmp))

        print(f"✓ {output_path}")
        return True

    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == '__main__':
    # ==================== 编辑以下参数 ====================

    TEMPLATE = "/home/seahi/workspase/firmware-partnumber-application/templates/固件确认函-底板.xlsx"
    OUTPUT_DIR = "/mnt/d/Users/Seahi/Desktop/AiPiClaw"
    OUTPUT_NAME = "Ai-Thinker_Ai-WV01-32S_WWXH-Zh_UART-MCP_v1.0产品定制信息确认函 V1.2.xlsx"

    SHEET1_DATA = {
        'H1':  'D20260518-001',                      # 固件流水号（G1是标签，不动）
        'B3':  '3cd4d4b2bddb22d9bd9a3531717e6aae',   # 固件MD5
        'B4':  '879f477eb947e3e3bd4494c15083c081',   # Boot2 MD5
    'C4':  '标准Efuse',
    'E4':  'DOUT',
    'F4':  '1.3',
    'C5':  '否',
        'G4':  '否',
        'B6':  '无',
        'C6':  '无',                                  # AT端口空白值（无AT口）
        'D6':  '无',
        # E6/C7/E7/G7 留空（无AT口，这些行都是AT相关）
        'F6':  'None',
        'B7':  '8',
        'D7':  '1',
        'F7':  '否',
        'B8':  'UART0',
        'D8':  '115200',
        'B10': 'firmware_version:1.0 sdk_version:bouffalo_sdk_v2.0 compile_time:May 18 2026 13:00:00',  # 一行无换行
    }

    SHEET3_DATA = {
        # Sheet3 模板已有预填值，不动
        # 注意：B3/B4/B5/B7/B8/F2 是 dropdown 控件单元格，不能填文字
    }

    # ======================================================

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    output_path = os.path.join(OUTPUT_DIR, OUTPUT_NAME)
    fill_template(TEMPLATE, output_path, SHEET1_DATA, SHEET3_DATA)
