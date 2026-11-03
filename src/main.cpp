#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <RadioLib.h>
#include <heltec-eink-modules.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include "protocol.h"
#include "device_policy.h"
#include "radio_settings.h"
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#include "web_ui.h"
#include "api_docs.h"

using namespace paper;
#if PAPER_REVISION == 12
using Panel = EInkDisplay_WirelessPaperV1_2;
#else
using Panel = EInkDisplay_WirelessPaperV1_1;
#endif
class Screen : public Panel {
 public:
  bool timeout = false;
  void sleepPanel() {
    wait();
#if PAPER_REVISION == 12
    sendCommand(0x10); sendData(0x01);
#else
    sendCommand(0x07); sendData(0xa5);
#endif
  }
 protected:
  void wait() override {
    uint32_t start = millis();
#if PAPER_REVISION == 12
    const int busy = HIGH;
#else
    const int busy = LOW;
#endif
    while (digitalRead(7) == busy) {
      if (millis() - start > 15000) { timeout = true; break; }
      delay(1);
    }
  }
};

Screen screen;
class ApiServer : public WebServer {
 public:
  ApiServer() : WebServer(80) {}
  void bodyTimeout() { _currentClient.Stream::setTimeout(500); }
};
ApiServer server;
DNSServer dns;
Preferences prefs;
SPIClass radioSPI(FSPI);
SX1262 radio = new Module(8, 14, 12, 13, radioSPI);
Assembler assembler;
class Canvas : public GFX {
 public:
  Canvas() : GFX(paper::WIDTH, paper::HEIGHT) {}
  uint8_t pixels[FRAME_SIZE]{};
  uint8_t* getBuffer() { return pixels; }
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x < 0 || x >= int(WIDTH) || y < 0 || y >= int(HEIGHT)) return;
    uint8_t& b = pixels[y * STRIDE + x / 8]; uint8_t mask = 0x80 >> (x % 8);
    if (color) b |= mask; else b &= ~mask;
  }
};
Canvas canvas;
struct FrameJob { uint32_t id; uint8_t pixels[FRAME_SIZE]; };
QueueHandle_t jobs;
SemaphoreHandle_t frameLock;
uint8_t front[FRAME_SIZE]{};
volatile bool displayBusy = false, displayFailed = false;
volatile uint32_t completed = 0;
uint32_t nextJob = 1, accepted = 0, loraPackets = 0, loraErrors = 0;
float lastRSSI = 0, lastSNR = 0, frequency = 0;
volatile bool received = false;
bool radioReady = false;
bool hotspotAlways = false;
uint32_t retryAt = 0, apCheckAt = 0;
uint64_t hotspotChangeAfter = 0;
String apName, hostname, lastError;
int radioError = 0, powerSaveResult = 0;
uint8_t httpBody[MAX_BODY];
size_t httpSize = 0;
bool httpComplete = false, httpOverflow = false;
RadioSettings radioSettings;
struct PacketLog {
  uint32_t id; uint64_t at; uint16_t size; bool tx;
  float rssi,snr; uint8_t bytes[255];
};
PacketLog packetLog[16]{};
uint32_t packetSequence=0, sentPackets=0;
uint64_t nextTxAtUs=0;
uint32_t logPacket(const uint8_t* bytes,size_t n,bool tx,float rssi=0,float snr=0) {
  uint32_t id=++packetSequence;PacketLog& entry=packetLog[(id-1)%16];
  entry.id=id;entry.at=uint64_t(esp_timer_get_time())/1000;entry.size=n;entry.tx=tx;entry.rssi=rssi;entry.snr=snr;
  memcpy(entry.bytes,bytes,n);return id;
}
void radioJson(JsonObject o,const RadioSettings& c) {
  o["frequency_mhz"]=c.frequency;o["bandwidth_khz"]=c.bandwidth;o["spreading_factor"]=c.sf;
  o["coding_rate"]=c.cr;o["sync_word"]=c.sync;o["power_dbm"]=c.power;o["preamble"]=c.preamble;
  o["crc"]=c.crc;o["tx_enabled"]=c.tx;o["duty_cycle_percent"]=c.dutyPercent;
}
bool parseRadio(JsonObjectConst o,RadioSettings& c) {
  // Validate before narrowing numeric fields to avoid overflow and truncation.
  for(JsonPairConst pair:o) {
    const char* k=pair.key().c_str();JsonVariantConst v=pair.value();
    if(!strcmp(k,"frequency_mhz")){if(!v.is<float>())return false;c.frequency=v.as<float>();}
    else if(!strcmp(k,"bandwidth_khz")){if(!v.is<float>())return false;c.bandwidth=v.as<float>();}
    else if(!strcmp(k,"duty_cycle_percent")){if(!v.is<float>())return false;c.dutyPercent=v.as<float>();}
    else if(!strcmp(k,"crc") || !strcmp(k,"tx_enabled")) {
      if(!v.is<bool>())return false;if(!strcmp(k,"crc"))c.crc=v.as<bool>();else c.tx=v.as<bool>();
    } else {
      if(!v.is<int>())return false;int n=v.as<int>();
      if(!strcmp(k,"spreading_factor") && n>=5 && n<=12)c.sf=n;
      else if(!strcmp(k,"coding_rate") && n>=5 && n<=8)c.cr=n;
      else if(!strcmp(k,"sync_word") && n>=0 && n<=255)c.sync=n;
      else if(!strcmp(k,"power_dbm") && n>=-9 && n<=22)c.power=n;
      else if(!strcmp(k,"preamble") && n>=8 && n<=65535)c.preamble=n;
      else return false;
    }
  }
  return validRadio(c);
}
uint64_t uptimeMs() { return uint64_t(esp_timer_get_time()) / 1000; }
void applyHotspotPolicy() {
  unsigned clients=WiFi.softAPgetStationNum();
  bool wanted=hotspotWanted(hotspotAlways,uptimeMs(),clients);
  bool active=bool(WiFi.getMode() & WIFI_AP);
  if (wanted && (!active || WiFi.softAPIP()==IPAddress(0,0,0,0))) {
    WiFi.enableAP(true);
    WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
    WiFi.softAP(apName.c_str(),nullptr,1,0,4);
    dns.start(53,"*",WiFi.softAPIP());
    Serial.println("Hotspot enabled");
  } else if (!wanted && active) {
    dns.stop(); WiFi.enableAP(false);
    Serial.println("Hotspot window ended with no clients; station Wi-Fi stays enabled");
  }
}

void readHttpBody() {
  // The core's plain String parser truncates binary at a zero byte. Stream
  // bounded raw bodies instead, for both JSON and framebuffer requests.
  if (server.header("Content-Type").startsWith("multipart/")) {
    httpSize = 0; httpComplete = false; httpOverflow = true; return;
  }
  HTTPRaw& raw = server.raw();
  if (raw.status == RAW_START) { server.bodyTimeout(); httpSize = 0; httpComplete = false; httpOverflow = false; }
  else if (raw.status == RAW_WRITE) {
    if (httpSize + raw.currentSize > MAX_BODY) httpOverflow = true;
    if (!httpOverflow) memcpy(httpBody + httpSize, raw.buf, raw.currentSize);
    httpSize += raw.currentSize;
  } else if (raw.status == RAW_END) httpComplete = !httpOverflow;
  else if (raw.status == RAW_ABORTED) httpComplete = false;
}
bool readJson(JsonDocument& doc) {
  return httpComplete && !httpOverflow && !deserializeJson(doc,httpBody,httpSize);
}

void IRAM_ATTR onRadio() { received = true; }
void reply(int code, const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}
void errorReply(int code, const String& message) {
  JsonDocument doc; doc["error"] = message; String body; serializeJson(doc, body); reply(code, body);
}

void displayWorker(void*) {
  FrameJob job;
  for (;;) {
    if (xQueueReceive(jobs, &job, portMAX_DELAY) != pdTRUE) continue;
    screen.timeout = false;
    screen.fastmodeOff();
    screen.fillScreen(WHITE);
    for (int y = 0; y < int(HEIGHT); ++y)
      for (int x = 0; x < int(WIDTH); ++x)
        if (job.pixels[y * STRIDE + x / 8] & (0x80 >> (x % 8))) screen.drawPixel(x, y, BLACK);
    screen.update();
    screen.sleepPanel();
    displayFailed = screen.timeout;
    if (!screen.timeout) {
      xSemaphoreTake(frameLock, portMAX_DELAY);
      memcpy(front, job.pixels, FRAME_SIZE);
      completed = job.id;
      xSemaphoreGive(frameLock);
    }
    Serial.printf("Display job %lu: %s\n", (unsigned long)job.id, screen.timeout ? "panel timeout" : "done");
    displayBusy = false;
  }
}
bool enqueueFrame(const uint8_t* data, uint32_t& id) {
  if (displayBusy) { lastError = "Screen is updating; retry after it finishes"; return false; }
  FrameJob job; job.id = nextJob++; memcpy(job.pixels, data, FRAME_SIZE);
  displayBusy = true;
  if (xQueueSend(jobs, &job, 0) != pdTRUE) { displayBusy = false; lastError = "Display queue is full"; return false; }
  id = accepted = job.id; return true;
}
bool number(JsonVariantConst value, int low, int high) { return value.is<int>() && value.as<int>() >= low && value.as<int>() <= high; }
bool validPoint(JsonObjectConst o, const char* x, const char* y) {
  return number(o[x], -500, 750) && number(o[y], -500, 622);
}
bool validColor(JsonVariantConst v) {
  if (number(v,0,1)) return true;
  if (!v.is<const char*>()) return false;
  String color=v.as<String>(); color.toLowerCase();
  return color=="black" || color=="white" || color=="#000000" || color=="#ffffff";
}
int ink(JsonVariantConst v, int fallback) {
  if (v.isNull()) return fallback;
  if (v.is<int>()) return v.as<int>();
  String color=v.as<String>(); color.toLowerCase();
  return color=="black" || color=="#000000" ? 1 : 0;
}
bool renderJson(const uint8_t* data, size_t len, uint32_t& id) {
  if (len > MAX_BODY) { lastError = "JSON exceeds 8192 bytes"; return false; }
  JsonDocument doc;
  if (deserializeJson(doc, data, len) || !doc.is<JsonObject>()) { lastError = "Expected a JSON object"; return false; }
  if (!doc["action"].isNull()) {
    lastError = "Display commands cannot control Wi-Fi; use /api/hotspot for its policy"; return false;
  }
  bool hasText = doc["text"].is<const char*>();
  bool hasOps = doc["ops"].is<JsonArray>();
  bool clear = doc["clear"] | true;
  if ((!hasText && !hasOps && !doc["clear"].is<bool>()) || (doc["text"].isNull() == false && !hasText)) { lastError = "Supply text, ops, or clear"; return false; }
  if (!doc["clear"].isNull() && !doc["clear"].is<bool>()) { lastError = "clear must be true or false"; return false; }
  if (!doc["ops"].isNull() && !hasOps) { lastError = "ops must be an array"; return false; }
  if ((!doc["size"].isNull() && !number(doc["size"],1,8)) ||
      (!doc["x"].isNull() && !number(doc["x"],-500,750)) ||
      (!doc["y"].isNull() && !number(doc["y"],-500,622)) ||
      (!doc["wrap"].isNull() && !doc["wrap"].is<bool>()) ||
      (!doc["color"].isNull() && !validColor(doc["color"])) ||
      (!doc["background"].isNull() && !validColor(doc["background"]))) {
    lastError="Invalid text settings: size 1..8, integer x/y, boolean wrap, black/white colors"; return false;
  }
  if (hasText && strlen(doc["text"])>2000) { lastError="Text is limited to 2000 bytes"; return false; }
  JsonArrayConst ops = doc["ops"].as<JsonArrayConst>();
  if (ops.size() > 100) { lastError = "At most 100 drawing operations"; return false; }
  // Validate every operation before touching the canvas or physical display.
  for (JsonObjectConst o : ops) {
    String type = o["type"] | "";
    bool valid = validPoint(o, "x", "y") && (o["color"].isNull() || validColor(o["color"]));
    if (type == "text") valid &= o["text"].is<const char*>() && (o["size"].isNull() || number(o["size"], 1, 8)) && (o["wrap"].isNull() || o["wrap"].is<bool>());
    else if (type == "line") valid &= validPoint(o, "x2", "y2");
    else if (type == "rect") valid &= number(o["w"], 1, 500) && number(o["h"], 1, 500) && (o["fill"].isNull() || o["fill"].is<bool>());
    else if (type == "circle") valid &= number(o["r"], 1, 250) && (o["fill"].isNull() || o["fill"].is<bool>());
    else if (type != "pixel") valid = false;
    if (!valid) { lastError = "Invalid drawing operation: " + type; return false; }
  }
  if (displayBusy) { lastError = "Screen is updating; retry after it finishes"; return false; }
  int colorDefault=ink(doc["color"],1);
  bool wrapDefault=doc["wrap"] | true;
  if (clear) canvas.fillScreen(ink(doc["background"],0));
  else { xSemaphoreTake(frameLock, portMAX_DELAY); memcpy(canvas.getBuffer(), front, FRAME_SIZE); xSemaphoreGive(frameLock); }
  canvas.setTextWrap(wrapDefault); canvas.setFont(nullptr);
  if (hasText) { canvas.setTextSize(doc["size"] | 2); canvas.setTextColor(colorDefault); canvas.setCursor(doc["x"] | 4,doc["y"] | 4); canvas.print(doc["text"].as<const char*>()); }
  for (JsonObjectConst o : ops) {
    String type = o["type"].as<String>(); int x = o["x"], y = o["y"], color = ink(o["color"],colorDefault);
    if (type == "text") { canvas.setTextWrap(o["wrap"] | wrapDefault); canvas.setTextSize(o["size"] | (doc["size"] | 1)); canvas.setTextColor(color); canvas.setCursor(x,y); canvas.print(o["text"].as<const char*>()); }
    else if (type == "line") canvas.drawLine(x,y,o["x2"],o["y2"],color);
    else if (type == "rect") { if (o["fill"] | false) canvas.fillRect(x,y,o["w"],o["h"],color); else canvas.drawRect(x,y,o["w"],o["h"],color); }
    else if (type == "circle") { if (o["fill"] | false) canvas.fillCircle(x,y,o["r"],color); else canvas.drawCircle(x,y,o["r"],color); }
    else canvas.drawPixel(x,y,color);
  }
  return enqueueFrame(canvas.getBuffer(), id);
}

bool initRadio(float mhz) {
  bool wasReady=radioReady;
  if (radioReady) { radio.clearDio1Action(); radio.standby(); }
  radioReady = false;
  frequency = mhz;
  if (mhz <= 0) { if(wasReady)radio.sleep();radioError = 0; return true; }
  Platform::VExtOn();
  radioSPI.begin(9, 11, 10, 8);
  radioError = radio.begin(mhz,radioSettings.bandwidth,radioSettings.sf,radioSettings.cr,radioSettings.sync,radioSettings.power,radioSettings.preamble,1.8,false);
  if (radioError != RADIOLIB_ERR_NONE) return false;
  radioError=radio.setCRC(radioSettings.crc?2:0);
  if(radioError!=RADIOLIB_ERR_NONE)return false;
  radio.setDio2AsRfSwitch(true);
  radio.setDio1Action(onRadio);
  radioError = radio.startReceive();
  radioReady = radioError == RADIOLIB_ERR_NONE;
  received = false;
  return radioReady;
}

void status() {
  JsonDocument d;
  d["device"] = hostname; d["width"] = WIDTH; d["height"] = HEIGHT;
  d["frame_bytes"] = FRAME_SIZE; d["firmware"] = "1.3.0"; d["panel_revision"] = PAPER_REVISION;
  d["ssid"] = WIFI_SSID; d["connected"] = WiFi.status() == WL_CONNECTED;
  d["ip"] = WiFi.localIP().toString(); d["hotspot"] = apName; d["hotspot_ip"] = "192.168.4.1";
  d["mac"] = WiFi.macAddress(); d["hotspot_mac"] = WiFi.softAPmacAddress();
  d["url"] = "http://" + hostname + ".local/"; d["http_port"] = 80;
  d["last_dhcp_ip"] = prefs.getString("last_ip", "");
  d["dhcp_restore_last_ip"] = bool(CONFIG_LWIP_DHCP_RESTORE_LAST_IP);
  d["busy"] = displayBusy; d["display_error"] = displayFailed; d["accepted_job"] = accepted; d["completed_job"] = completed;
  d["power_mode"] = "online"; d["cpu_mhz"] = getCpuFrequencyMhz();
  unsigned clients=WiFi.softAPgetStationNum();
  d["hotspot_always_on"] = hotspotAlways; d["wifi_shutdown_allowed"] = false;
  d["hotspot_mode"] = hotspotAlways ? "always" : "first-hour";
  d["uptime_seconds"] = uptimeMs()/1000;
  d["hotspot_clients"] = clients;
  d["hotspot_seconds_remaining"] = hotspotSecondsRemaining(uptimeMs());
  d["hotspot_held_open"] = !hotspotAlways && uptimeMs()>=HOTSPOT_WINDOW_MS && clients>0;
  d["hotspot_active"] = bool(WiFi.getMode() & WIFI_AP) && WiFi.softAPIP() != IPAddress(0,0,0,0);
  d["wifi_power_save_result"] = powerSaveResult; d["wifi_power_save_note"] = "AP+STA may limit modem sleep; current is unmeasured";
  d["radio_ready"] = radioReady; d["radio_mhz"] = frequency; d["radio_error"] = radioError;
  radioJson(d["radio"].to<JsonObject>(),radioSettings);
  d["lora_sent_packets"]=sentPackets;d["latest_packet_id"]=packetSequence;
  uint64_t now=esp_timer_get_time();d["tx_wait_ms"]=now<nextTxAtUs?(nextTxAtUs-now+999)/1000:0;
  d["lora_packets"] = loraPackets; d["lora_errors"] = loraErrors; d["last_rssi"] = lastRSSI; d["last_snr"] = lastSNR;
  d["last_command_error"] = lastError;
  String out; serializeJson(d,out); reply(200,out);
}
void acceptedReply(uint32_t id) { reply(202, "{\"accepted\":true,\"job\":" + String(id) + "}"); }
void routes() {
  const char* headers[] = {"Content-Type"}; server.collectHeaders(headers,1);
  server.on("/",HTTP_GET, [] { server.send_P(200,"text/html; charset=utf-8", WEB_UI); });
  server.on("/api",HTTP_GET, [] { server.send_P(200,"text/html; charset=utf-8", API_DOCS); });
  server.on("/api/status",HTTP_GET,status);
  server.on("/api/frame",HTTP_GET, [] {
    uint8_t copy[FRAME_SIZE]; xSemaphoreTake(frameLock,portMAX_DELAY); memcpy(copy,front,FRAME_SIZE); xSemaphoreGive(frameLock);
    server.sendHeader("Cache-Control","no-store"); server.setContentLength(FRAME_SIZE);
    server.send(200,"application/octet-stream", ""); server.sendContent((const char*)copy,FRAME_SIZE);
  });
  server.on("/api/frame",HTTP_POST, [] {
    uint32_t id;
    if (!httpComplete || httpOverflow || httpSize!=FRAME_SIZE) { errorReply(400,"Expected exactly 3904 bytes: 250x122, row-padded, MSB first, 1=black"); return; }
    if (!enqueueFrame(httpBody,id)) errorReply(409,lastError); else acceptedReply(id);
  },readHttpBody);
  server.on("/api/display",HTTP_POST, [] {
    uint32_t id;
    if (!httpComplete || httpOverflow) { errorReply(400,"Expected a raw JSON body of at most 8192 bytes"); return; }
    if (!renderJson(httpBody,httpSize,id)) errorReply(displayBusy?409:400,lastError); else acceptedReply(id);
  },readHttpBody);
  server.on("/api/radio",HTTP_GET,status);
  server.on("/api/radio",HTTP_POST, [] {
    JsonDocument d;
    RadioSettings candidate=radioSettings;
    if (!readJson(d) || !d.is<JsonObject>() || !d.size() || !parseRadio(d.as<JsonObjectConst>(),candidate)) { errorReply(400,"Invalid radio settings; see /api for ranges"); return; }
    if (displayBusy) { errorReply(409,"Wait for the screen update to finish"); return; }
    RadioSettings previous=radioSettings;radioSettings=candidate;
    if (!initRadio(candidate.frequency)) { int failed=radioError;radioSettings=previous;initRadio(previous.frequency);errorReply(503,"Radio initialization failed: " + String(failed));return; }
    JsonDocument saved;radioJson(saved.to<JsonObject>(),radioSettings);String config;serializeJson(saved,config);
    prefs.putString("radio_cfg",config);status();
  },readHttpBody);
  server.on("/api/lora/messages",HTTP_GET, [] {
    uint32_t after=0;
    if(server.hasArg("after")) {
      String a=server.arg("after");
      if(!a.length() || a.length()>10){errorReply(400,"after must be a packet ID");return;}
      uint64_t parsed=0;for(char c:a){if(c<'0'||c>'9'){errorReply(400,"after must be a packet ID");return;}parsed=parsed*10+c-'0';}
      if(parsed>UINT32_MAX){errorReply(400,"after is out of range");return;}after=parsed;
    }
    JsonDocument d;d["latest_id"]=packetSequence;JsonArray entries=d["messages"].to<JsonArray>();
    uint32_t first=packetSequence>16?packetSequence-15:1;
    d["oldest_id"]=packetSequence?first:0;d["truncated"]=after>0 && after+uint64_t(1)<first;
    for(uint32_t id=first;id<=packetSequence && id>0;++id){
      if(id<=after)continue;const PacketLog& entry=packetLog[(id-1)%16];
      JsonObject m=entries.add<JsonObject>();m["id"]=id;m["uptime_ms"]=entry.at;m["direction"]=entry.tx?"tx":"rx";m["bytes"]=entry.size;
      String hex,text;hex.reserve(entry.size*2);text.reserve(entry.size);bool printable=true;const char* digits="0123456789abcdef";
      for(unsigned i=0;i<entry.size;++i){uint8_t v=entry.bytes[i];hex+=digits[v>>4];hex+=digits[v&15];if(v<32 && v!='\n' && v!='\r' && v!='\t')printable=false;if(v>=127)printable=false;text+=char(v);}
      m["hex"]=hex;if(printable)m["text"]=text;else m["text"]=nullptr;
      if(!entry.tx){m["rssi"]=entry.rssi;m["snr"]=entry.snr;}
    }
    String body;serializeJson(d,body);reply(200,body);
  });
  server.on("/api/lora/send",HTTP_POST, [] {
    if(!radioReady){errorReply(409,"Configure a radio frequency first");return;}
    if(!radioSettings.tx){errorReply(403,"Radio transmit is disabled; enable tx_enabled in /api/radio");return;}
    JsonDocument d;if(!readJson(d)||!d.is<JsonObject>()){errorReply(400,"Supply text or hex");return;}
    bool text=d["text"].is<const char*>(),hex=d["hex"].is<const char*>();
    if(text==hex){errorReply(400,"Supply exactly one of text or hex");return;}
    uint8_t payload[255];size_t n=0;
    if(text){String value=d["text"].as<String>();n=value.length();if(!n||n>255){errorReply(400,"Text must contain 1..255 bytes");return;}memcpy(payload,value.c_str(),n);}
    else {
      String value=d["hex"].as<String>();if(!value.length() || value.length()>510 || value.length()%2){errorReply(400,"hex must contain 1..255 bytes as paired hex digits");return;}
      auto digit=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
      for(unsigned i=0;i<value.length();i+=2){int a=digit(value[i]),b=digit(value[i+1]);if(a<0||b<0){errorReply(400,"Invalid hex digit");return;}payload[n++]=(a<<4)|b;}
    }
    uint64_t now=esp_timer_get_time();
    if(now<nextTxAtUs){server.sendHeader("Retry-After",String((unsigned long)((nextTxAtUs-now+999999)/1000000)));errorReply(429,"Transmit pacing limit; check tx_wait_ms in /api/status");return;}
    uint64_t airtime=radio.getTimeOnAir(n);nextTxAtUs=now+txIntervalUs(airtime,radioSettings.dutyPercent);
    radio.clearDio1Action();received=false;
    int state=radio.transmit(payload,n);
    radio.setDio1Action(onRadio);received=false;int rxState=radio.startReceive();
    if(rxState!=RADIOLIB_ERR_NONE){radioReady=false;radioError=rxState;}
    if(state!=RADIOLIB_ERR_NONE){++loraErrors;errorReply(503,"Radio transmit failed: "+String(state));return;}
    ++sentPackets;uint32_t id=logPacket(payload,n,true);
    reply(200,"{\"sent\":true,\"id\":"+String(id)+",\"bytes\":"+String(n)+",\"receiver_ready\":"+(radioReady?"true":"false")+"}");
  },readHttpBody);
  server.on("/api/power",HTTP_POST, [] {
    JsonDocument d;
    if (!readJson(d)) { errorReply(400,"Expected JSON with mode=online"); return; }
    String mode=d["mode"]|"";
    if (mode=="online") reply(200,"{\"mode\":\"online\",\"station_shutdown_allowed\":false}");
    else errorReply(403,"Station Wi-Fi shutdown is disabled; use /api/hotspot to choose the hotspot policy");
  },readHttpBody);
  server.on("/api/hotspot",HTTP_GET,status);
  server.on("/api/hotspot",HTTP_POST, [] {
    JsonDocument d;
    if (!readJson(d) || !d["mode"].is<const char*>()) { errorReply(400,"mode must be first-hour or always"); return; }
    String mode=d["mode"].as<String>();
    if (mode!="always" && mode!="first-hour") { errorReply(400,"mode must be first-hour or always"); return; }
    hotspotAlways=mode=="always";
    prefs.putBool("ap_always",hotspotAlways);
    hotspotChangeAfter=uptimeMs()+750; apCheckAt=0;
    status();
  },readHttpBody);
  server.onNotFound([] { errorReply(404,"Not found; open / for the display controls"); });
}
void startWifi() {
  WiFi.persistent(true); WiFi.mode(WIFI_AP_STA); WiFi.setHostname(hostname.c_str());
  WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
  WiFi.softAP(apName.c_str(),nullptr,1,0,4);
  WiFi.setAutoReconnect(true);
  if (strlen(WIFI_SSID)>0) WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  WiFi.setSleep(true); powerSaveResult=esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  dns.start(53,"*",WiFi.softAPIP()); server.begin();
  if (MDNS.begin(hostname.c_str())) MDNS.addService("http","tcp",80);
  retryAt=millis();
  Serial.printf("Hotspot: %s  http://192.168.4.1/\n",apName.c_str());
}
void processRadio() {
  if (!radioReady || !received) return;
  received=false;
  uint8_t packet[255]; size_t n=radio.getPacketLength();
  int state=n<=sizeof(packet)?radio.readData(packet,n):RADIOLIB_ERR_PACKET_TOO_LONG;
  lastRSSI=radio.getRSSI(); lastSNR=radio.getSNR();
  radio.startReceive();
  if (state!=RADIOLIB_ERR_NONE) { ++loraErrors; return; }
  ++loraPackets;
  logPacket(packet,n,false,lastRSSI,lastSNR);
  if(n<2 || packet[0]!='W' || packet[1]!='P')return;
  Result result=assembler.add(packet,n,millis());
  if (result==Result::Invalid || result==Result::Busy) { ++loraErrors; return; }
  if (result!=Result::Complete) return;
  uint32_t id=0;
  bool ok=assembler.kind==1?renderJson(assembler.body,assembler.total,id):enqueueFrame(assembler.body,id);
  if (ok) assembler.accept(); else ++loraErrors;
  Serial.printf("LoRa command: %s, job %lu\n",ok?"accepted":lastError.c_str(),(unsigned long)id);
}
void setup() {
  Serial.begin(115200);
  Serial.println("Wireless Paper API by Ramazan Yavuz: provided as is, without warranty. You accept all risk from installation and use.");
  pinMode(18,OUTPUT); digitalWrite(18,LOW); pinMode(0,INPUT_PULLUP);
  uint64_t mac=ESP.getEfuseMac(); char suffix[5]; snprintf(suffix,sizeof(suffix),"%04X",unsigned((mac>>32)&0xffff));
  apName=String("WirelessPaper-")+suffix; hostname=String("wireless-paper-")+suffix; hostname.toLowerCase();
  prefs.begin("paper",false); frequency=prefs.getFloat("frequency",0);
  radioSettings.frequency=frequency;
  if(prefs.isKey("radio_cfg")){JsonDocument saved;if(!deserializeJson(saved,prefs.getString("radio_cfg"))){RadioSettings candidate;if(parseRadio(saved.as<JsonObjectConst>(),candidate)){radioSettings=candidate;frequency=candidate.frequency;}}}
  hotspotAlways=prefs.getBool("ap_always",false);
  pinMode(12,OUTPUT); digitalWrite(12,LOW); delay(10); digitalWrite(12,HIGH);
  frameLock=xSemaphoreCreateMutex(); jobs=xQueueCreate(1,sizeof(FrameJob));
  screen.setRotation(1);
  xTaskCreatePinnedToCore(displayWorker,"screen",8192,nullptr,1,nullptr,1);
  routes(); startWifi(); initRadio(frequency);
  JsonDocument banner;
  banner["text"]=String("Setup hotspot\n")+apName+"\nhttp://192.168.4.1/\n"+(hotspotAlways?"Always on":"On for 60 minutes")+"\nSettings: Web UI";
  banner["size"]=2;
  String boot;serializeJson(banner,boot);uint32_t bootJob;
  renderJson((const uint8_t*)boot.c_str(),boot.length(),bootJob);
  Serial.printf("Wireless Paper API ready. Radio MHz: %.3f\n",frequency);
}
void loop() {
  if (radioReady && digitalRead(14)==HIGH) received=true;
  processRadio();
  {
    dns.processNextRequest(); server.handleClient();
    static bool wasConnected=false;
    bool connected=WiFi.status()==WL_CONNECTED;
    if (connected && !wasConnected) {
      String ip=WiFi.localIP().toString();
      if (prefs.getString("last_ip","")!=ip) prefs.putString("last_ip",ip);
      Serial.printf("Wi-Fi: %s  http://%s/\n",WIFI_SSID,ip.c_str());
    }
    wasConnected=connected;
    if (strlen(WIFI_SSID)>0 && !connected && millis()-retryAt>30000) { retryAt=millis(); WiFi.reconnect(); }
  }
  if (millis()-apCheckAt>1000 && uptimeMs()>=hotspotChangeAfter) {
    apCheckAt=millis();
    applyHotspotPolicy();
  }
  delay(5);
}
