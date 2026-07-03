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
#include <driver/i2c_master.h>
#include <driver/i2s_std.h>
#include <driver/gpio.h>

#define HTTP_OK 200

// Audio pins (ES8311 + NS4150B amplifier)
#define I2C_SDA_IO      7
#define I2C_SCL_IO      8
#define I2S_MCLK_IO     13
#define I2S_BCLK_IO     12
#define I2S_WS_IO       10
#define I2S_DOUT_IO     9
#define PA_CTRL_IO      53
#define ES8311_ADDR     0x18
#define SAMPLE_RATE     44100

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

// ---------------------------------------------------------------------------
// ES8311 Audio
// ---------------------------------------------------------------------------

static i2c_master_bus_handle_t i2c_bus = NULL;
static i2s_chan_handle_t i2s_tx = NULL;

static esp_err_t es8311_write_reg(uint8_t reg, uint8_t val) {
  if (!i2c_bus) return ESP_FAIL;
  i2c_master_dev_handle_t dev;
  i2c_device_config_t dev_cfg = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = ES8311_ADDR,
    .scl_speed_hz = 100000,
  };
  ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &dev));
  uint8_t data[2] = {reg, val};
  esp_err_t ret = i2c_master_transmit(dev, data, sizeof(data), -1);
  i2c_master_bus_rm_device(dev);
  return ret;
}

static void es8311_init(void) {
  // Reset
  es8311_write_reg(0x00, 0x3F);
  delay(10);
  es8311_write_reg(0x00, 0x03);

  // Clock: MCLK = 256*Fs
  es8311_write_reg(0x01, 0x08); // MCLK divider = 1
  es8311_write_reg(0x02, 0x60); // SCLK divider = 1, MCLK_SRC = MCLK pin
  es8311_write_reg(0x03, 0x00);
  es8311_write_reg(0x04, 0x00); // Chip control: no reset, no power down
  es8311_write_reg(0x08, 0x40); // Sample rate: 48k (MCLK=256*Fs)
  es8311_write_reg(0x09, 0x02); // Word length 16bit, I2S format

  // DAC path
  es8311_write_reg(0x0C, 0x04); // DAC soft mute off, DAC de-emphasis off
  es8311_write_reg(0x0D, 0x01); // DAC mono mix L+R, L/R volume set individually
  es8311_write_reg(0x10, 0x02); // ADC mono mix L+R
  es8311_write_reg(0x11, 0x7F); // ADC input volume max
  
  // Volume
  es8311_write_reg(0x14, 0x1A); // DAC volume L = 26 (0dB)
  es8311_write_reg(0x15, 0x1A); // DAC volume R = 26 (0dB)
  es8311_write_reg(0x1C, 0x02); // GPIO2 as output for MCLK

  // Analog
  es8311_write_reg(0x20, 0x20); // DAC analog: L/R mixer sel, soft ramp
  es8311_write_reg(0x21, 0x30); // DAC analog: L/R volume = 0dB
  es8311_write_reg(0x22, 0x00); // ADC analog
  es8311_write_reg(0x23, 0x10); // ADC analog: PGA gain = 0
  es8311_write_reg(0x24, 0x08); // Analog: HP/LR MUX: DAC L->HP_L, DAC R->HP_R
  es8311_write_reg(0x25, 0x00); // Analog: HP L/R select
  es8311_write_reg(0x26, 0x2E); // Analog: ADC/DAC power up, ISET, VMID
  es8311_write_reg(0x28, 0x08); // Analog LP MUX
  es8311_write_reg(0x2F, 0x00); // Charge pump

  // Power up output
  es8311_write_reg(0x31, 0x08); // HP driver power
  delay(10);
  es8311_write_reg(0x31, 0x0C); // HP driver power + enable
  delay(20);
}

static esp_err_t audio_init(void) {
  // I2C init
  i2c_master_bus_config_t i2c_cfg = {
    .i2c_port = -1,
    .sda_io_num = (gpio_num_t)I2C_SDA_IO,
    .scl_io_num = (gpio_num_t)I2C_SCL_IO,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .flags = { .enable_internal_pullup = true },
  };
  if (i2c_new_master_bus(&i2c_cfg, &i2c_bus) != ESP_OK) {
    log_e("I2C init failed");
    return ESP_FAIL;
  }
  log_i("I2C master bus initialized");

  // Configure PA pin
  gpio_config_t pa_cfg = {
    .pin_bit_mask = BIT64(PA_CTRL_IO),
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&pa_cfg);
  gpio_set_level((gpio_num_t)PA_CTRL_IO, 0);
  log_i("PA control pin configured");

  // I2S init
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)0, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;
  if (i2s_new_channel(&chan_cfg, &i2s_tx, NULL) != ESP_OK) {
    log_e("I2S new channel failed");
    return ESP_FAIL;
  }
  log_i("I2S channel created");

  i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = (gpio_num_t)I2S_MCLK_IO,
      .bclk = (gpio_num_t)I2S_BCLK_IO,
      .ws = (gpio_num_t)I2S_WS_IO,
      .dout = (gpio_num_t)I2S_DOUT_IO,
      .din = (gpio_num_t)I2S_GPIO_UNUSED,
      .invert_flags = {false, false, false},
    },
  };
  std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  if (i2s_channel_init_std_mode(i2s_tx, &std_cfg) != ESP_OK) {
    log_e("I2S std mode init failed");
    return ESP_FAIL;
  }
  if (i2s_channel_enable(i2s_tx) != ESP_OK) {
    log_e("I2S enable failed");
    return ESP_FAIL;
  }
  log_i("I2S initialized");

  // ES8311 init
  es8311_init();
  log_i("ES8311 codec initialized");

  // Enable PA
  gpio_set_level((gpio_num_t)PA_CTRL_IO, 1);
  log_i("Amplifier enabled");

  return ESP_OK;
}

static void play_tone(int freq_hz, int duration_ms) {
  if (!i2s_tx) {
    log_w("I2S not initialized");
    return;
  }
  int16_t samples[512];
  int nsamples = SAMPLE_RATE * duration_ms / 1000;
  for (int i = 0; i < nsamples; ) {
    int chunk = (nsamples - i) > 256 ? 256 : (nsamples - i);
    for (int j = 0; j < chunk; j++) {
      float t = (float)(i / 2) / SAMPLE_RATE;
      int16_t v = (int16_t)(sinf(2 * M_PI * freq_hz * t) * 32000);
      samples[j * 2] = v;
      samples[j * 2 + 1] = v;
      i += 2;
    }
    size_t written;
    i2s_channel_write(i2s_tx, samples, chunk * sizeof(int16_t) * 2, &written, portMAX_DELAY);
  }
  // Fade out
  for (int j = 0; j < 256; j++) {
    float t = (float)j / 256.0;
    int16_t v = (int16_t)(sinf(2 * M_PI * freq_hz * j / SAMPLE_RATE) * 32000 * (1 - t));
    samples[j * 2] = v;
    samples[j * 2 + 1] = v;
  }
  size_t written;
  i2s_channel_write(i2s_tx, samples, 256 * sizeof(int16_t) * 2, &written, portMAX_DELAY);
  log_i("Tone %dHz played for %dms", freq_hz, duration_ms);
}

static void audio_test(void) {
  play_tone(440, 500);
  delay(100);
  play_tone(880, 500);
}

// ---------------------------------------------------------------------------
// Network & Config
// ---------------------------------------------------------------------------

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
  ".btn-success{background:#2e7d32;color:#fff}"
  ".btn-primary:hover{background:#1557b0}"
  ".btn-danger:hover{background:#b71c1c}"
  ".btn-success:hover{background:#1b5e20}"
  ".info-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px 16px;font-size:.9rem}"
  ".info-grid .label{color:#666}"
  ".info-grid .value{font-weight:500}"
  ".success{color:#2e7d32;font-size:.85rem;margin-top:8px}"
  ".error{color:#c62828;font-size:.85rem;margin-top:8px}"
  "table{width:100%;border-collapse:collapse;font-size:.85rem;margin-top:8px}"
  "th,td{padding:6px 8px;text-align:left;border-bottom:1px solid #eee}"
  "th{color:#666;font-weight:500}"
  ".mt-2{margin-top:12px}"
  ".btn-audio{display:inline-block;padding:10px 24px;background:#2e7d32;color:#fff;border:none;border-radius:4px;cursor:pointer;font-size:.9rem}"
  ".btn-audio:disabled{background:#aaa;cursor:not-allowed}"
  "</style></head><body>"
  "<div class='container'>"
  "<h1>ESP32-P4 Router</h1>"
  "<div class='nav'><a href='/' id='nav-status'>Status</a><a href='/wan' id='nav-wan'>WAN</a><a href='/wifi' id='nav-wifi'>WiFi</a><a href='/audio' id='nav-audio'>Audio</a><a href='/system' id='nav-system'>Sistem</a></div>"
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
  " +'<span class=label>Klien</span><span>'+(d.client_list?d.client_list.length:0)+'</span>'"
  " +'</div>'"
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
  "async function loadAudio(){"
  "document.getElementById('page-content').innerHTML="
  "'<h2>Test Speaker</h2>'"
  " +'<p style=margin-bottom:12px;font-size:.9rem;color:#666>Tekan tombol untuk memutar nada uji pada speaker.</p>'"
  " +'<button class=\"btn-audio\" id=btn-test onclick=testSpeaker()>Putar Nada</button>'"
  " +'<p id=audio-status style=margin-top:12px;font-size:.85rem></p>';"
  "activeTab('audio')"
  "}"
  "async function testSpeaker(){"
  "let btn=document.getElementById('btn-test');btn.disabled=true;btn.textContent='Memutar...';"
  "document.getElementById('audio-status').textContent='';"
  "let r=await fetch('/api/audio/test');"
  "let d=await r.json();"
  "show(d.msg,d.ok?'success':'error');"
  "btn.disabled=false;btn.textContent='Putar Nada'"
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
  "async function route(){let p=location.pathname;if(p==='/'||p==='')loadStatus();else if(p==='/wan')loadWan();else if(p==='/wifi')loadWifi();else if(p==='/audio')loadAudio();else if(p==='/system')loadSystem()}"
  "window.onpopstate=route;document.querySelectorAll('.nav a').forEach(a=>a.onclick=function(e){e.preventDefault();history.pushState(null,'',this.href);route()});"
  "route()"
  "</script></body></html>";

void web_setup() {
  server.on("/", []() { server.send(200, "text/html", FPSTR(PAGE_HEAD)); });
  server.on("/wan", []() { server.send(200, "text/html", FPSTR(PAGE_HEAD)); });
  server.on("/wifi", []() { server.send(200, "text/html", FPSTR(PAGE_HEAD)); });
  server.on("/audio", []() { server.send(200, "text/html", FPSTR(PAGE_HEAD)); });
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

  server.on("/api/audio/test", []() {
    audio_test();
    server.send(HTTP_OK, "application/json", "{\"ok\":true,\"msg\":\"Speaker test selesai\"}");
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

  esp_err_t ret = audio_init();
  if (ret == ESP_OK) {
    log_i("Audio system initialized");
  } else {
    log_w("Audio init failed (%d), speaker test will be unavailable", ret);
  }

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
