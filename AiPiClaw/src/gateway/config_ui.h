/**
 * @file config_ui.h
 * @brief Config Portal UI HTML - 占位文件
 * @version 1.0
 * @date 2026-05-15
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 *
 * @note 配置门户的SPA页面HTML。
 *       完整实现将由前端团队在Day3/4完成。
 *       当前仅包含占位内容。
 */

#ifndef __CONFIG_UI_H
#define __CONFIG_UI_H

/**
 * @brief 配置门户SPA页面HTML（占位）
 *
 * 前端将替换此文件为完整单页应用。
 * 包含WiFi扫描+连接、LLM配置、应用配置三个步骤。
 */
static const char CONFIG_UI_HTML[] =
    "<!DOCTYPE html>\n"
    "<html lang=\"zh-CN\">\n"
    "<head>\n"
    "<meta charset=\"UTF-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">\n"
    "<title>AiPiClaw 配置门户</title>\n"
    "<style>\n"
    "body{font-family:-apple-system,sans-serif;max-width:480px;margin:0 auto;padding:16px;background:#f5f5f5}\n"
    "h1{text-align:center;color:#333}\n"
    ".step{background:#fff;border-radius:8px;padding:16px;margin:12px 0;box-shadow:0 1px 3px rgba(0,0,0,.1)}\n"
    ".step h2{font-size:16px;margin:0 0 12px 0;color:#555}\n"
    "button{width:100%;padding:12px;background:#007aff;color:#fff;border:none;border-radius:6px;font-size:16px;cursor:pointer}\n"
    "button:hover{background:#0056cc}\n"
    "button:disabled{background:#ccc;cursor:not-allowed}\n"
    "input{width:100%;padding:10px;margin:4px 0 12px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}\n"
    "select{width:100%;padding:10px;margin:4px 0 12px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}\n"
    "label{font-size:14px;color:#333}\n"
    ".status{font-size:12px;color:#666;margin-top:8px}\n"
    ".success{color:#34c759}\n"
    ".error{color:#ff3b30}\n"
    ".scan-btn{background:#5856d6}\n"
    ".scan-btn:hover{background:#3634a3}\n"
    "</style>\n"
    "</head>\n"
    "<body>\n"
    "<h1>AiPiClaw 配置</h1>\n"
    "<div class=\"step\">\n"
    "<h2>第1步: 选择WiFi网络</h2>\n"
    "<button class=\"scan-btn\" onclick=\"scanWifi()\">扫描WiFi</button>\n"
    "<div id=\"scanResult\" class=\"status\"></div>\n"
    "<div id=\"apList\"></div>\n"
    "<input type=\"password\" id=\"wifiPassword\" placeholder=\"WiFi密码\">\n"
    "<button onclick=\"connectWifi()\">连接WiFi</button>\n"
    "<div id=\"wifiStatus\" class=\"status\"></div>\n"
    "</div>\n"
    "<div class=\"step\">\n"
    "<h2>第2步: 配置AI</h2>\n"
    "<label>API提供商</label>\n"
    "<select id=\"llmProvider\">\n"
    "<option value=\"anthropic\">Anthropic Claude</option>\n"
    "<option value=\"openai\">OpenAI</option>\n"
    "<option value=\"deepseek\">DeepSeek</option>\n"
    "<option value=\"minimax\">MiniMax</option>\n"
    "</select>\n"
    "<label>模型名称</label>\n"
    "<input type=\"text\" id=\"llmModel\" placeholder=\"claude-opus-4-5\">\n"
    "<label>API Key</label>\n"
    "<input type=\"password\" id=\"llmApiKey\" placeholder=\"sk-ant-...\">\n"
    "<button onclick=\"saveLLMConfig()\">保存AI配置</button>\n"
    "<div id=\"llmStatus\" class=\"status\"></div>\n"
    "</div>\n"
    "<div class=\"step\">\n"
    "<h2>第3步: 应用并启动</h2>\n"
    "<button onclick=\"applyAndStart()\">应用配置 &amp; 启动</button>\n"
    "<div id=\"applyStatus\" class=\"status\"></div>\n"
    "</div>\n"
    "<script>\n"
    "let selectedSSID='';\n"
    "function scanWifi(){\n"
    "  const btn=event.target;btn.disabled=true;btn.textContent='扫描中...';\n"
    "  document.getElementById('scanResult').textContent='';\n"
    "  fetch('/api/wifi/scan').then(r=>r.json()).then(d=>{\n"
    "    const list=document.getElementById('apList');list.innerHTML='';\n"
    "    d.aps.forEach(ap=>{\n"
    "      const div=document.createElement('div');\n"
    "      div.textContent=ap.ssid+' ('+ap.rssi+'dBm)';div.style.cssText='padding:8px;margin:4px 0;background:#f0f0f0;border-radius:4px;cursor:pointer';\n"
    "      div.onclick=()=>{selectedSSID=ap.ssid;document.getElementById('scanResult').innerHTML='已选: <b>'+ap.ssid+'</b>';\n"
    "        document.querySelectorAll('#apList>div').forEach(e=>e.style.background='#f0f0f0');div.style.background='#cce5ff'};\n"
    "      list.appendChild(div);\n"
    "    });\n"
    "    document.getElementById('scanResult').textContent='找到 '+d.count+' 个网络';\n"
    "    btn.disabled=false;btn.textContent='重新扫描';\n"
    "  }).catch(e=>{document.getElementById('scanResult').textContent='扫描失败: '+e;btn.disabled=false;btn.textContent='扫描WiFi'});\n"
    "}\n"
    "function connectWifi(){\n"
    "  if(!selectedSSID){document.getElementById('wifiStatus').textContent='请先选择WiFi网络';return}\n"
    "  const pwd=document.getElementById('wifiPassword').value;\n"
    "  document.getElementById('wifiStatus').textContent='连接中...';\n"
    "  fetch('/api/wifi/connect',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:selectedSSID,password:pwd})})\n"
    "    .then(r=>r.json()).then(d=>{document.getElementById('wifiStatus').innerHTML='<span class=\"success\">凭据已保存</span>'})\n"
    "    .catch(e=>{document.getElementById('wifiStatus').innerHTML='<span class=\"error\">失败: '+e+'</span>'});\n"
    "}\n"
    "function saveLLMConfig(){\n"
    "  const provider=document.getElementById('llmProvider').value;\n"
    "  const model=document.getElementById('llmModel').value;\n"
    "  const key=document.getElementById('llmApiKey').value;\n"
    "  document.getElementById('llmStatus').textContent='保存中...';\n"
    "  fetch('/api/llm/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({provider,model,api_key:key})})\n"
    "    .then(r=>r.json()).then(d=>{document.getElementById('llmStatus').innerHTML='<span class=\"success\">AI配置已保存</span>'})\n"
    "    .catch(e=>{document.getElementById('llmStatus').innerHTML='<span class=\"error\">失败: '+e+'</span>'});\n"
    "}\n"
    "function applyAndStart(){\n"
    "  document.getElementById('applyStatus').textContent='应用配置中...';\n"
    "  fetch('/api/apply',{method:'POST'}).then(r=>r.json()).then(d=>{\n"
    "    document.getElementById('applyStatus').innerHTML='<span class=\"success\">配置已应用!</span><br>聊天地址: '+d.chat_url;\n"
    "  }).catch(e=>{document.getElementById('applyStatus').innerHTML='<span class=\"error\">失败: '+e+'</span>'});\n"
    "}\n"
    "</script>\n"
    "</body>\n"
    "</html>\n";

#define CONFIG_UI_HTML_LEN (sizeof(CONFIG_UI_HTML) - 1)

#endif /* __CONFIG_UI_H */
