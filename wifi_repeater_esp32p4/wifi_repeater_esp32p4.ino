#include <WiFi.h>
#include <ETH.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <lwip/lwip_napt.h>
#include <lwip/netif.h>

#define HTTP_OK 200

static bool eth_connected = false;
static bool napt_enabled = false;
static unsigned long napt_retry_ms = 0;

Preferences prefs;
WebServer server(80);

String cfg_ap_ssid = "ESP32P4_Router";
String cfg_ap_pass = "";
String cfg_wan_mode = "dhcp";
String cfg_wan_ip = "";
String cfg_wan_mask = "";
String cfg_wan_gw = "";
String cfg_wan_dns = "";

static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
    log_i("WiFi AP client connected");
  } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
    log_i("WiFi AP client disconnected");
  }
}

static void enable_napt() {
  if (napt_enabled) return;
  if (!ETH.netif()) { log_w("NAPT: ETH netif null"); return; }
  struct netif *eth_n = (struct netif *)esp_netif_get_netif_impl(ETH.netif());
  if (!eth_n) { log_w("NAPT: lwIP netif null"); return; }
  if (!ip_napt_enable_netif(eth_n, 1)) {
    log_w("NAPT enable failed, will retry");
    napt_retry_ms = millis() + 5000;
    return;
  }
  napt_enabled = true;
  log_i("NAPT enabled on Ethernet interface");
}

static void onEthEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (event == ARDUINO_EVENT_ETH_CONNECTED) {
    log_i("Ethernet Link Up");
  } else if (event == ARDUINO_EVENT_ETH_DISCONNECTED) {
    log_i("Ethernet Link Down");
    eth_connected = false;
  } else if (event == ARDUINO_EVENT_ETH_GOT_IP) {
    log_i("Ethernet Got IP: %s", ETH.localIP().toString().c_str());
    eth_connected = true;
    enable_napt();
  } else if (event == ARDUINO_EVENT_ETH_LOST_IP) {
    eth_connected = false;
  }
}

void load_config() {
  prefs.begin("router", true);
  cfg_ap_ssid = prefs.getString("ap_ssid", "ESP32P4_Router");
  cfg_ap_pass = prefs.getString("ap_pass", "");
  cfg_wan_mode = prefs.getString("wan_mode", "dhcp");
  cfg_wan_ip = prefs.getString("wan_ip", "");
  cfg_wan_mask = prefs.getString("wan_mask", "");
  cfg_wan_gw = prefs.getString("wan_gw", "");
  cfg_wan_dns = prefs.getString("wan_dns", "");
  prefs.end();
}

void save_config() {
  prefs.begin("router", false);
  prefs.putString("ap_ssid", cfg_ap_ssid);
  prefs.putString("ap_pass", cfg_ap_pass);
  prefs.putString("wan_mode", cfg_wan_mode);
  prefs.putString("wan_ip", cfg_wan_ip);
  prefs.putString("wan_mask", cfg_wan_mask);
  prefs.putString("wan_gw", cfg_wan_gw);
  prefs.putString("wan_dns", cfg_wan_dns);
  prefs.end();
}

void apply_wan_config() {
  if (cfg_wan_mode == "static") {
    IPAddress ip, mask, gw, dns;
    if (ip.fromString(cfg_wan_ip) && mask.fromString(cfg_wan_mask) && gw.fromString(cfg_wan_gw)) {
      if (cfg_wan_dns.length() > 0) dns.fromString(cfg_wan_dns);
      ETH.config(ip, gw, mask, dns);
    }
  }
}

void setup_wifi_ap() {
  WiFi.mode(WIFI_MODE_AP);
  if (cfg_ap_pass.length() >= 8) {
    WiFi.softAP(cfg_ap_ssid.c_str(), cfg_ap_pass.c_str());
  } else {
    WiFi.softAP(cfg_ap_ssid.c_str(), NULL);
  }
  delay(500);
  log_i("WiFi AP started: %s on 192.168.4.1", cfg_ap_ssid.c_str());
}

void setup_ethernet() {
  Network.onEvent(onEthEvent);
  ETH.begin(ETH_PHY_TLK110, 1, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_POWER, ETH_CLK_MODE);
  apply_wan_config();
  delay(100);
}

String stringify_clients() {
  String json = "[";
  wifi_sta_list_t list;
  esp_err_t err = esp_wifi_ap_get_sta_list(&list);
  if (err == ESP_OK) {
    for (int i = 0; i < list.num; i++) {
      wifi_sta_info_t *sta = &list.sta[i];
      if (i > 0) json += ",";
      json += "{\"mac\":\"";
      char mac[18];
      snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
               sta->mac[0], sta->mac[1], sta->mac[2], sta->mac[3], sta->mac[4], sta->mac[5]);
      json += mac;
      json += "\",\"rssi\":";
      json += sta->rssi;
      json += "}";
    }
  }
  json += "]";
  return json;
}

// ---------------------------------------------------------------------------
// Web UI
// ---------------------------------------------------------------------------

static const char PAGE_HEAD[] PROGMEM =
  "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>"
  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<title>ESP32-P4 Router</title>"
  "<style>"
  "*{margin:0;padding:0;box-sizing:border-box;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif}"
  "body{background:#f0f2f5;color:#333;padding:16px}"
  ".container{max-width:800px;margin:0 auto}"
  "h1{font-size:1.4rem;margin-bottom:16px;color:#1a73e8}"
  ".nav{display:flex;gap:4px;margin-bottom:16px;flex-wrap:wrap}"
  ".nav a{padding:8px 16px;background:#e8e8e8;border-radius:6px 6px 0 0;text-decoration:none;color:#555;font-size:.9rem}"
  ".nav a.active{background:#1a73e8;color:#fff}"
  ".page{background:#fff;border-radius:0 8px 8px 8px;padding:20px;box-shadow:0 1px 3px rgba(0,0,0,.1)}"
  ".page h2{margin-bottom:12px;font-size:1.1rem}"
  "label{display:block;margin:10px 0 4px;font-size:.85rem;color:#666}"
  "input[type=text],input[type=password],select{width:100%;padding:8px 10px;border:1px solid #ddd;border-radius:4px;font-size:.9rem}"
  "button{padding:10px 24px;border:none;border-radius:4px;cursor:pointer;font-size:.9rem}"
  ".btn-primary{background:#1a73e8;color:#fff}"
  ".btn-danger{background:#d32f2f;color:#fff}"
  ".btn-primary:hover{background:#1557b0}"
  ".btn-danger:hover{background:#b71c1c}"
  ".info-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px 16px;font-size:.9rem}"
  ".info-grid .label{color:#666}"
  ".info-grid .value{font-weight:500}"
  ".success{color:#2e7d32;font-size:.85rem;margin-top:8px}"
  ".error{color:#c62828;font-size:.85rem;margin-top:8px}"
  "table{width:100%;border-collapse:collapse;font-size:.85rem;margin-top:8px}"
  "th,td{padding:6px 8px;text-align:left;border-bottom:1px solid #eee}"
  "th{color:#666;font-weight:500}"
  ".mt-2{margin-top:12px}"
  "</style></head><body>"
  "<div class='container'>"
  "<h1>ESP32-P4 Router</h1>"
  "<div class='nav'><a href='/' id='nav-status'>Status</a><a href='/wan' id='nav-wan'>WAN</a><a href='/wifi' id='nav-wifi'>WiFi</a><a href='/system' id='nav-system'>Sistem</a></div>"
  "<div class='page' id='page-content'>Loading...</div>"
  "<script>"
  "function activeTab(t){document.querySelectorAll('.nav a').forEach(a=>a.classList.remove('active'));document.getElementById('nav-'+t).classList.add('active')}"
  "async function get(url){let r=await fetch(url);return r.json()}"
  "function show(msg,type){let d=document.getElementById('msg');if(!d){d=document.createElement('div');d.id='msg';document.getElementById('page-content').prepend(d)}d.className=type;d.textContent=msg;setTimeout(()=>d.textContent='',4000)}"
  "async function loadStatus(){"
  "let d=await get('/api/status');"
  "document.getElementById('page-content').innerHTML="
  "'<h2>Status</h2><div class=info-grid>'"
  " +'<span class=label>Ethernet</span><span>'+(d.eth_connected?'<span class=success>Terhubung</span>':'<span class=error>Putus</span>')+'</span>'"
  " +'<span class=label>IP WAN</span><span>'+(d.wan_ip||'-')+'</span>'"
  " +'<span class=label>Gateway</span><span>'+(d.wan_gw||'-')+'</span>'"
  " +'<span class=label>WiFi AP</span><span>'+d.ap_ssid+'</span>'"
  " +'<span class=label>IP LAN</span><span>'+d.ap_ip+'</span>'"
  " +'<span class=label>Klien</span><span>'+d.clients+'</span></div>'"
  " +(d.client_list&&d.client_list.length?'<h2 class=mt-2>Klien Terhubung</h2><table><tr><th>MAC</th><th>RSSI</th></tr>'+d.client_list.map(c=>'<tr><td>'+c.mac+'</td><td>'+c.rssi+' dBm</td></tr>').join('')+'</table>':'');"
  "activeTab('status')"
  "}"
  "async function loadWan(){"
  "let d=await get('/api/wan');"
  "document.getElementById('page-content').innerHTML="
  "'<h2>Pengaturan WAN (Ethernet)</h2>'"
  " +'<label>Mode</label><select id=wan-mode><option value=dhcp'+(d.mode==='dhcp'?' selected':'')+'>DHCP</option><option value=static'+(d.mode==='static'?' selected':'')+'>Static</option></select>'"
  " +'<div id=wan-static><label>IP Address</label><input type=text id=wan-ip value=\"'+(d.ip||'')+'\" placeholder=192.168.1.100>"
  "<label>Subnet Mask</label><input type=text id=wan-mask value=\"'+(d.mask||'')+'\" placeholder=255.255.255.0>"
  "<label>Gateway</label><input type=text id=wan-gw value=\"'+(d.gw||'')+'\" placeholder=192.168.1.1>"
  "<label>DNS</label><input type=text id=wan-dns value=\"'+(d.dns||'')+'\" placeholder=8.8.8.8></div>'"
  " +'<button class=\"btn-primary mt-2\" onclick=saveWan()>Simpan</button>';"
  "document.getElementById('wan-mode').onchange=function(){document.getElementById('wan-static').style.display=this.value==='static'?'block':'none'};"
  "document.getElementById('wan-static').style.display=d.mode==='static'?'block':'none';"
  "activeTab('wan')"
  "}"
  "async function saveWan(){"
  "let mode=document.getElementById('wan-mode').value;"
  "let body={mode};"
  "if(mode==='static'){body.ip=document.getElementById('wan-ip').value;body.mask=document.getElementById('wan-mask').value;body.gw=document.getElementById('wan-gw').value;body.dns=document.getElementById('wan-dns').value}"
  "let r=await fetch('/api/wan',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});"
  "let d=await r.json();"
  "show(d.msg,d.ok?'success':'error')"
  "}"
  "async function loadWifi(){"
  "let d=await get('/api/wifi');"
  "document.getElementById('page-content').innerHTML="
  "'<h2>Pengaturan WiFi AP</h2>'"
  " +'<label>SSID</label><input type=text id=ap-ssid value=\"'+d.ssid+'\">'"
  " +'<label>Password (min 8 karakter, kosongkan untuk open)</label><input type=password id=ap-pass value=\"'+d.pass+'\">'"
  " +'<button class=\"btn-primary mt-2\" onclick=saveWifi()>Simpan &amp; Restart</button>';"
  "activeTab('wifi')"
  "}"
  "async function saveWifi(){"
  "let ssid=document.getElementById('ap-ssid').value;"
  "let pass=document.getElementById('ap-pass').value;"
  "let r=await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid,pass})});"
  "let d=await r.json();"
  "show(d.msg,d.ok?'success':'error')"
  "}"
  "async function loadSystem(){"
  "document.getElementById('page-content').innerHTML="
  "'<h2>System</h2>'"
  " +'<button class=\"btn-danger\" onclick=doReboot()>Restart Router</button>'"
  " +'<button class=\"btn-danger mt-2\" onclick=doReset() style=\"margin-left:8px\">Reset Pabrik</button>'"
  " +'<p class=mt-2 style=font-size:.85rem;color:#666>ESP32-P4 Router v1.0</p>';"
  "activeTab('system')"
  "}"
  "async function doReboot(){if(confirm('Restart router sekarang?')){await fetch('/api/reboot');show('Merestart...','success');setTimeout(()=>location.reload(),8000)}}"
  "async function doReset(){if(confirm('Hapus semua konfigurasi dan restart?')){await fetch('/api/reset');show('Merestart...','success');setTimeout(()=>location.reload(),8000)}}"
  "async function route(){let p=location.pathname;if(p==='/'||p==='')loadStatus();else if(p==='/wan')loadWan();else if(p==='/wifi')loadWifi();else if(p==='/system')loadSystem()}"
  "window.onpopstate=route;document.querySelectorAll('.nav a').forEach(a=>a.onclick=function(e){e.preventDefault();history.pushState(null,'',this.href);route()});"
  "route()"
  "</script></body></html>";

void web_setup() {
  server.on("/", []() { server.send(200, "text/html", FPSTR(PAGE_HEAD)); });
  server.on("/wan", []() { server.send(200, "text/html", FPSTR(PAGE_HEAD)); });
  server.on("/wifi", []() { server.send(200, "text/html", FPSTR(PAGE_HEAD)); });
  server.on("/system", []() { server.send(200, "text/html", FPSTR(PAGE_HEAD)); });

  server.on("/api/status", []() {
    String clients_json = stringify_clients();
    String json = "{";
    json += "\"eth_connected\":" + String(eth_connected ? "true" : "false") + ",";
    json += "\"wan_ip\":\"" + ETH.localIP().toString() + "\",";
    json += "\"wan_gw\":\"" + ETH.gatewayIP().toString() + "\",";
    json += "\"wan_mask\":\"" + ETH.subnetMask().toString() + "\",";
    json += "\"wan_dns\":\"" + ETH.dnsIP().toString() + "\",";
    json += "\"ap_ssid\":\"" + cfg_ap_ssid + "\",";
    json += "\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\",";
    json += "\"client_list\":" + clients_json;
    json += "}";
    server.send(HTTP_OK, "application/json", json);
  });

  server.on("/api/wan", HTTP_GET, []() {
    String json = "{";
    json += "\"mode\":\"" + cfg_wan_mode + "\",";
    json += "\"ip\":\"" + cfg_wan_ip + "\",";
    json += "\"mask\":\"" + cfg_wan_mask + "\",";
    json += "\"gw\":\"" + cfg_wan_gw + "\",";
    json += "\"dns\":\"" + cfg_wan_dns + "\"";
    json += "}";
    server.send(HTTP_OK, "application/json", json);
  });

  server.on("/api/wan", HTTP_POST, []() {
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    String body = server.arg("plain");
    DynamicJsonDocument doc(256);
    DeserializationError err = deserializeJson(doc, body);
    if (err) { server.send(400, "application/json", "{\"ok\":false}"); return; }

    cfg_wan_mode = doc["mode"].as<String>();
    if (cfg_wan_mode == "static") {
      cfg_wan_ip = doc["ip"].as<String>();
      cfg_wan_mask = doc["mask"].as<String>();
      cfg_wan_gw = doc["gw"].as<String>();
      cfg_wan_dns = doc["dns"].as<String>();
    }
    save_config();
    server.send(HTTP_OK, "application/json", "{\"ok\":true,\"msg\":\"Konfigurasi WAN disimpan. Restart untuk menerapkan.\"}");
  });

  server.on("/api/wifi", HTTP_GET, []() {
    String json = "{";
    json += "\"ssid\":\"" + cfg_ap_ssid + "\",";
    json += "\"pass\":\"" + cfg_ap_pass + "\"";
    json += "}";
    server.send(HTTP_OK, "application/json", json);
  });

  server.on("/api/wifi", HTTP_POST, []() {
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    String body = server.arg("plain");
    DynamicJsonDocument doc(256);
    DeserializationError err = deserializeJson(doc, body);
    if (err) { server.send(400, "application/json", "{\"ok\":false}"); return; }

    String ssid = doc["ssid"].as<String>();
    String pass = doc["pass"].as<String>();
    if (ssid.length() == 0) { server.send(400, "application/json", "{\"ok\":false,\"msg\":\"SSID tidak boleh kosong\"}"); return; }
    cfg_ap_ssid = ssid;
    cfg_ap_pass = pass;
    save_config();
    server.send(HTTP_OK, "application/json", "{\"ok\":true,\"msg\":\"Konfigurasi WiFi disimpan. Restart untuk menerapkan.\"}");
  });

  server.on("/api/reboot", []() {
    server.send(HTTP_OK, "application/json", "{\"ok\":true}");
    delay(500);
    ESP.restart();
  });
  server.on("/api/reset", []() {
    prefs.begin("router", false);
    prefs.clear();
    prefs.end();
    server.send(HTTP_OK, "application/json", "{\"ok\":true,\"msg\":\"Konfigurasi dihapus. Restart...\"}");
    delay(500);
    ESP.restart();
  });

  server.begin();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  log_i("ESP32-P4 Router starting...");

  load_config();
  setup_wifi_ap();
  setup_ethernet();
  web_setup();

  WiFi.onEvent(onWiFiEvent);

  log_i("Router ready. Connect WiFi to %s and browse to 192.168.4.1", cfg_ap_ssid.c_str());
}

void loop() {
  server.handleClient();
  if (!napt_enabled && eth_connected && napt_retry_ms && millis() >= napt_retry_ms) {
    enable_napt();
  }
}
