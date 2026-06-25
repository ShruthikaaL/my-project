MASTER BOT - CODE 
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"
#include "img_converters.h"

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define IN1 12
#define IN2 13
#define IN3 15
#define IN4 14
#define TRIG_PIN 2
#define ECHO_PIN 4

const char* ssid = "abhi";
const char* password = "abhiram123";

httpd_handle_t server = NULL;
float distance = 999;
unsigned long lastDistanceCheck = 0;
unsigned long stateStartTime = 0;
int robotState = 0;
String slaveIP = "";
String slaveStatus = "Waiting";
String masterStatus = "Running";
String msgLog[10];
int msgCount = 0;

void stopMotors();
void moveForward();
void moveBackward();
void turnLeft();
void turnRight();
float getDistance();
void startServer();
void addMessage(String msg);
void sendToSlave(String endpoint);

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char part_buf[64];
  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) { res = ESP_FAIL; break; }
    if (fb->format != PIXFORMAT_JPEG) {
      bool ok = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
      esp_camera_fb_return(fb); fb = NULL;
      if (!ok) { res = ESP_FAIL; break; }
    } else {
      _jpg_buf_len = fb->len;
      _jpg_buf = fb->buf;
    }
    size_t hlen = snprintf(part_buf, 64,
      "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", _jpg_buf_len);
    res = httpd_resp_send_chunk(req, part_buf, hlen);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)_jpg_buf, _jpg_buf_len);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, "\r\n", 2);
    if (fb) { esp_camera_fb_return(fb); fb = NULL; _jpg_buf = NULL; }
    else if (_jpg_buf) { free(_jpg_buf); _jpg_buf = NULL; }
    if (res != ESP_OK) break;
  }
  return res;
}

static esp_err_t index_handler(httpd_req_t *req) {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">";
  html += "<title>Master Bot</title><style>";
  html += "*{margin:0;padding:0;box-sizing:border-box}";
  html += "body{background:#1a1a2e;color:white;font-family:Arial;display:flex;flex-direction:column;align-items:center;padding:20px}";
  html += "h1{color:#e94560;margin-bottom:15px;font-size:22px}";
  html += "img{width:100%;max-width:640px;border-radius:12px;background:#000;min-height:240px;display:block}";
  html += ".box{margin-top:15px;background:#16213e;padding:12px 20px;border-radius:10px;width:100%;max-width:640px;font-size:14px}";
  html += ".stat{margin-top:6px;font-size:13px;color:#888}.stat span{color:#4CAF50;font-weight:bold}";
  html += ".btns{display:flex;gap:10px;margin-top:10px;flex-wrap:wrap}";
  html += "button{padding:10px 18px;border:none;border-radius:8px;cursor:pointer;font-size:13px;font-weight:bold}";
  html += ".find{background:#e94560;color:white}.stop{background:#c0392b;color:white}.resume{background:#27ae60;color:white}";
  html += ".log{height:130px;overflow-y:auto;font-size:12px;margin-top:8px}";
  html += ".log p{padding:3px 0;border-bottom:1px solid #0f3460}";
  html += ".m{color:#4CAF50}.s{color:#3498db}.sys{color:#888}";
  html += "</style></head><body>";
  html += "<h1>Master Bot - Live Stream</h1>";
  html += "<img src=\"/stream\" id=\"stream\" onerror=\"setTimeout(function(){document.getElementById('stream').src='/stream?t='+Date.now()},2000)\">";
  html += "<div class=\"box\">";
  html += "<div class=\"stat\">Master Status: <span id=\"mstat\">Running</span></div>";
  html += "<div class=\"stat\">Master Distance: <span id=\"mdist\">--</span> cm</div>";
  html += "<div class=\"stat\">Slave Status: <span id=\"sstat\">Waiting</span></div>";
  html += "<div class=\"btns\">";
  html += "<button class=\"find\" onclick=\"cmd('find')\">Find Black Bottle</button>";
  html += "<button class=\"stop\" onclick=\"cmd('stop')\">Stop All</button>";
  html += "<button class=\"resume\" onclick=\"cmd('resume')\">Resume All</button>";
  html += "</div></div>";
  html += "<div class=\"box\"><b>Message Log</b><div class=\"log\" id=\"log\"></div></div>";
  html += "<script>";
  html += "function addLog(msg,type){var l=document.getElementById('log');";
  html += "var p=document.createElement('p');p.className=type;";
  html += "var d=new Date();p.textContent=d.getHours()+':'+String(d.getMinutes()).padStart(2,'0')+':'+String(d.getSeconds()).padStart(2,'0')+' - '+msg;";
  html += "l.insertBefore(p,l.firstChild);}";
  html += "function cmd(a){fetch('/command?action='+a).then(function(r){return r.json();}).then(function(d){addLog(d.message,'m');}).catch(function(){});}";
  html += "function updateStatus(){fetch('/status').then(function(r){return r.json();}).then(function(d){";
  html += "document.getElementById('mstat').textContent=d.masterStatus;";
  html += "document.getElementById('mdist').textContent=d.masterDist;";
  html += "document.getElementById('sstat').textContent=d.slaveStatus;";
  html += "if(d.newMsg&&d.newMsg!==''){addLog(d.newMsg,'s');}";
  html += "}).catch(function(){});}";
  html += "setInterval(updateStatus,1000);addLog('System started','sys');";
  html += "</script></body></html>";
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, html.c_str(), html.length());
  return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req) {
  String newMsg = "";
  if (msgCount > 0) { newMsg = msgLog[msgCount-1]; msgCount = 0; }
  String json = "{";
  json += "\"masterStatus\":\"" + masterStatus + "\",";
  json += "\"masterDist\":\"" + String(distance, 1) + "\",";
  json += "\"slaveStatus\":\"" + slaveStatus + "\",";
  json += "\"newMsg\":\"" + newMsg + "\"";
  json += "}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, json.c_str(), json.length());
  return ESP_OK;
}

static esp_err_t command_handler(httpd_req_t *req) {
  char query[100];
  httpd_req_get_url_query_str(req, query, sizeof(query));
  String q = String(query);
  String response = "{";
  if (q.indexOf("action=find") >= 0) {
    masterStatus = "Searching";
    addMessage("Master: Find the black bottle!");
    sendToSlave("/command?action=find");
    response += "\"message\":\"Master: Sent find command to slave\"";
  } else if (q.indexOf("action=stop") >= 0) {
    masterStatus = "Stopped";
    stopMotors();
    sendToSlave("/command?action=stop");
    addMessage("Master: All bots stopped");
    response += "\"message\":\"Master: All bots stopped\"";
  } else if (q.indexOf("action=resume") >= 0) {
    masterStatus = "Running";
    sendToSlave("/command?action=resume");
    addMessage("Master: All bots resumed");
    response += "\"message\":\"Master: All bots resumed\"";
  } else {
    response += "\"message\":\"Unknown command\"";
  }
  response += "}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, response.c_str(), response.length());
  return ESP_OK;
}

static esp_err_t register_handler(httpd_req_t *req) {
  char query[100];
  httpd_req_get_url_query_str(req, query, sizeof(query));
  String q = String(query);
  int start = q.indexOf("ip=") + 3;
  slaveIP = q.substring(start);
  slaveStatus = "Connected";
  addMessage("Slave connected: " + slaveIP);
  Serial.println("Slave registered: " + slaveIP);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, "OK", 2);
  return ESP_OK;
}

static esp_err_t slavemsg_handler(httpd_req_t *req) {
  char query[200];
  httpd_req_get_url_query_str(req, query, sizeof(query));
  String q = String(query);
  int start = q.indexOf("msg=") + 4;
  String msg = q.substring(start);
  msg.replace("%20", " ");
  msg.replace("%3A", ":");
  msg.replace("%21", "!");
  msg.replace("%2C", ",");
  addMessage("Slave: " + msg);
  Serial.println("Slave says: " + msg);
  if (msg.indexOf("Found") >= 0) {
    slaveStatus = "Found it!";
    masterStatus = "Object Located";
  } else if (msg.indexOf("search") >= 0 || msg.indexOf("OK") >= 0) {
    slaveStatus = "Searching";
  }
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, "OK", 2);
  return ESP_OK;
}

void addMessage(String msg) {
  if (msgCount < 10) { msgLog[msgCount] = msg; msgCount++; }
}

void sendToSlave(String endpoint) {
  if (slaveIP == "") { Serial.println("Slave not connected!"); return; }
  WiFiClient client;
  if (client.connect(slaveIP.c_str(), 80)) {
    client.println("GET " + endpoint + " HTTP/1.0");
    client.println("Host: " + slaveIP);
    client.println("Connection: close");
    client.println();
    delay(100);
    client.stop();
    Serial.println("Sent to slave: " + endpoint);
  } else {
    Serial.println("Failed to reach slave: " + slaveIP);
  }
}

void startServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.max_uri_handlers = 10;
  httpd_uri_t index_uri =    { .uri="/",         .method=HTTP_GET, .handler=index_handler,    .user_ctx=NULL };
  httpd_uri_t stream_uri =   { .uri="/stream",   .method=HTTP_GET, .handler=stream_handler,   .user_ctx=NULL };
  httpd_uri_t status_uri =   { .uri="/status",   .method=HTTP_GET, .handler=status_handler,   .user_ctx=NULL };
  httpd_uri_t command_uri =  { .uri="/command",  .method=HTTP_GET, .handler=command_handler,  .user_ctx=NULL };
  httpd_uri_t register_uri = { .uri="/register", .method=HTTP_GET, .handler=register_handler, .user_ctx=NULL };
  httpd_uri_t slavemsg_uri = { .uri="/slavemsg", .method=HTTP_GET, .handler=slavemsg_handler, .user_ctx=NULL };
  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &stream_uri);
    httpd_register_uri_handler(server, &status_uri);
    httpd_register_uri_handler(server, &command_uri);
    httpd_register_uri_handler(server, &register_uri);
    httpd_register_uri_handler(server, &slavemsg_uri);
    Serial.println("Master server started!");
  }
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== MASTER STARTING ===");
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  stopMotors();
  Serial.println("Motors OK!");

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0=Y2_GPIO_NUM; config.pin_d1=Y3_GPIO_NUM;
  config.pin_d2=Y4_GPIO_NUM; config.pin_d3=Y5_GPIO_NUM;
  config.pin_d4=Y6_GPIO_NUM; config.pin_d5=Y7_GPIO_NUM;
  config.pin_d6=Y8_GPIO_NUM; config.pin_d7=Y9_GPIO_NUM;
  config.pin_xclk=XCLK_GPIO_NUM; config.pin_pclk=PCLK_GPIO_NUM;
  config.pin_vsync=VSYNC_GPIO_NUM; config.pin_href=HREF_GPIO_NUM;
  config.pin_sscb_sda=SIOD_GPIO_NUM; config.pin_sscb_scl=SIOC_GPIO_NUM;
  config.pin_pwdn=PWDN_GPIO_NUM; config.pin_reset=RESET_GPIO_NUM;
  config.xclk_freq_hz=20000000; config.pixel_format=PIXFORMAT_JPEG;
  config.frame_size=FRAMESIZE_QVGA; config.jpeg_quality=12; config.fb_count=1;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera init failed!"); return;
  }
  Serial.println("Camera OK!");

  pinMode(TRIG_PIN, OUTPUT); pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  Serial.println("Ultrasonic OK!");

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println();
  Serial.println("WiFi Connected!");
  Serial.print("Open: http://"); Serial.println(WiFi.localIP());

  startServer();
  Serial.println("GO!");
}

void loop() {
  unsigned long now = millis();
  if (now - lastDistanceCheck >= 500) {
    lastDistanceCheck = now;
    distance = getDistance();
  }
  if (masterStatus == "Stopped") { stopMotors(); delay(10); return; }
  switch (robotState) {
    case 0:
      if (distance > 0 && distance <= 10) { stopMotors(); robotState=1; stateStartTime=now; }
      else moveForward();
      break;
    case 1: if (now-stateStartTime>=200) { moveBackward(); robotState=2; stateStartTime=now; } break;
    case 2: if (now-stateStartTime>=600) { stopMotors(); robotState=3; stateStartTime=now; } break;
    case 3: if (now-stateStartTime>=200) { if(random(2)==0)turnLeft();else turnRight(); robotState=4; stateStartTime=now; } break;
    case 4: if (now-stateStartTime>=700) { stopMotors(); robotState=0; stateStartTime=now; } break;
  }
  delay(10);
}

float getDistance() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long dur = pulseIn(ECHO_PIN, HIGH, 5000);
  if (dur == 0) return 999;
  return dur * 0.034 / 2;
}

void moveForward()  { digitalWrite(IN1,HIGH);digitalWrite(IN2,LOW);digitalWrite(IN3,HIGH);digitalWrite(IN4,LOW); }
void moveBackward() { digitalWrite(IN1,LOW);digitalWrite(IN2,HIGH);digitalWrite(IN3,LOW);digitalWrite(IN4,HIGH); }
void turnLeft()     { digitalWrite(IN1,LOW);digitalWrite(IN2,HIGH);digitalWrite(IN3,HIGH);digitalWrite(IN4,LOW); }
void turnRight()    { digitalWrite(IN1,HIGH);digitalWrite(IN2,LOW);digitalWrite(IN3,LOW);digitalWrite(IN4,HIGH); }
void stopMotors()   { digitalWrite(IN1,LOW);digitalWrite(IN2,LOW);digitalWrite(IN3,LOW);digitalWrite(IN4,LOW); }

SLAVE BOT - CODE

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"
#include "img_converters.h"

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define IN1 12
#define IN2 13
#define IN3 15
#define IN4 14
#define TRIG_PIN 2
#define ECHO_PIN 4

const char* ssid = "abhi";
const char* password = "abhiram123";

// *** UPDATE THIS after master boots and shows its IP ***
const char* masterIP = "10.233.112.148";

httpd_handle_t server = NULL;
float distance = 999;
unsigned long lastDistanceCheck = 0;
unsigned long stateStartTime = 0;
unsigned long lastWiFiCheck = 0;
int robotState = 0;
bool searchMode = false;
String slaveStatus = "Waiting";

void stopMotors();
void moveForward();
void moveBackward();
void turnLeft();
void turnRight();
float getDistance();
void startServer();
void sendToMaster(String msg);
void registerWithMaster();

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char part_buf[64];
  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) { res = ESP_FAIL; break; }
    if (fb->format != PIXFORMAT_JPEG) {
      bool ok = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
      esp_camera_fb_return(fb); fb = NULL;
      if (!ok) { res = ESP_FAIL; break; }
    } else {
      _jpg_buf_len = fb->len;
      _jpg_buf = fb->buf;
    }
    size_t hlen = snprintf(part_buf, 64,
      "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", _jpg_buf_len);
    res = httpd_resp_send_chunk(req, part_buf, hlen);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)_jpg_buf, _jpg_buf_len);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, "\r\n", 2);
    if (fb) { esp_camera_fb_return(fb); fb = NULL; _jpg_buf = NULL; }
    else if (_jpg_buf) { free(_jpg_buf); _jpg_buf = NULL; }
    if (res != ESP_OK) break;
  }
  return res;
}

static esp_err_t index_handler(httpd_req_t *req) {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">";
  html += "<title>Slave Bot</title><style>";
  html += "*{margin:0;padding:0;box-sizing:border-box}";
  html += "body{background:#1a1a2e;color:white;font-family:Arial;display:flex;flex-direction:column;align-items:center;padding:20px}";
  html += "h1{color:#3498db;margin-bottom:15px;font-size:22px}";
  html += "img{width:100%;max-width:640px;border-radius:12px;background:#000;min-height:240px;display:block}";
  html += ".box{margin-top:15px;background:#16213e;padding:12px 20px;border-radius:10px;width:100%;max-width:640px;font-size:14px}";
  html += ".stat{margin-top:6px;font-size:13px;color:#888}.stat span{color:#4CAF50;font-weight:bold}";
  html += "</style></head><body>";
  html += "<h1>Slave Bot - Live Stream</h1>";
  html += "<img src=\"/stream\" id=\"stream\" onerror=\"setTimeout(function(){document.getElementById('stream').src='/stream?t='+Date.now()},2000)\">";
  html += "<div class=\"box\">";
  html += "<div class=\"stat\">Status: <span id=\"status\">Waiting</span></div>";
  html += "<div class=\"stat\">Distance: <span id=\"dist\">--</span> cm</div>";
  html += "<div class=\"stat\">WiFi: <span id=\"wifi\">Connected</span></div>";
  html += "</div>";
  html += "<script>";
  html += "function updateStatus(){fetch('/status').then(function(r){return r.json();}).then(function(d){";
  html += "document.getElementById('status').textContent=d.status;";
  html += "document.getElementById('dist').textContent=d.distance;";
  html += "document.getElementById('wifi').textContent=d.wifi;";
  html += "}).catch(function(){document.getElementById('wifi').textContent='Disconnected';});}";
  html += "setInterval(updateStatus,1000);";
  html += "</script></body></html>";
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, html.c_str(), html.length());
  return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req) {
  char json[150];
  snprintf(json, sizeof(json),
    "{\"status\":\"%s\",\"distance\":\"%.1f\",\"wifi\":\"%s\"}",
    slaveStatus.c_str(), distance,
    WiFi.status() == WL_CONNECTED ? "Connected" : "Reconnecting...");
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, json, strlen(json));
  return ESP_OK;
}

static esp_err_t command_handler(httpd_req_t *req) {
  char query[100];
  httpd_req_get_url_query_str(req, query, sizeof(query));
  String q = String(query);
  if (q.indexOf("action=find") >= 0) {
    searchMode = true;
    slaveStatus = "Searching";
    robotState = 0;
    Serial.println("Received: Find command");
    sendToMaster("OK%2C starting search for black bottle!");
  } else if (q.indexOf("action=stop") >= 0) {
    searchMode = false;
    slaveStatus = "Stopped";
    stopMotors();
    robotState = 0;
    Serial.println("Received: Stop command");
  } else if (q.indexOf("action=resume") >= 0) {
    searchMode = true;
    slaveStatus = "Searching";
    robotState = 0;
    Serial.println("Received: Resume command");
    sendToMaster("Resuming search!");
  }
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, "OK", 2);
  return ESP_OK;
}

void sendToMaster(String msg) {
  WiFiClient client;
  if (client.connect(masterIP, 80)) {
    String url = "/slavemsg?msg=" + msg;
    client.println("GET " + url + " HTTP/1.0");
    client.println("Host: " + String(masterIP));
    client.println("Connection: close");
    client.println();
    delay(100);
    client.stop();
    Serial.println("Sent to master: " + msg);
  } else {
    Serial.println("Could not reach master!");
  }
}

void registerWithMaster() {
  WiFiClient client;
  String myIP = WiFi.localIP().toString();
  if (client.connect(masterIP, 80)) {
    String url = "/register?ip=" + myIP;
    client.println("GET " + url + " HTTP/1.0");
    client.println("Host: " + String(masterIP));
    client.println("Connection: close");
    client.println();
    delay(200);
    client.stop();
    Serial.println("Registered with master! My IP: " + myIP);
  } else {
    Serial.println("Could not register with master!");
  }
}

void startServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.max_uri_handlers = 8;
  httpd_uri_t index_uri =   { .uri="/",        .method=HTTP_GET, .handler=index_handler,   .user_ctx=NULL };
  httpd_uri_t stream_uri =  { .uri="/stream",  .method=HTTP_GET, .handler=stream_handler,  .user_ctx=NULL };
  httpd_uri_t status_uri =  { .uri="/status",  .method=HTTP_GET, .handler=status_handler,  .user_ctx=NULL };
  httpd_uri_t command_uri = { .uri="/command", .method=HTTP_GET, .handler=command_handler, .user_ctx=NULL };
  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &stream_uri);
    httpd_register_uri_handler(server, &status_uri);
    httpd_register_uri_handler(server, &command_uri);
    Serial.println("Slave server started!");
  }
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== SLAVE STARTING ===");
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  stopMotors();
  Serial.println("Motors OK!");

  camera_config_t config;
  config.ledc_channel=LEDC_CHANNEL_0; config.ledc_timer=LEDC_TIMER_0;
  config.pin_d0=Y2_GPIO_NUM; config.pin_d1=Y3_GPIO_NUM;
  config.pin_d2=Y4_GPIO_NUM; config.pin_d3=Y5_GPIO_NUM;
  config.pin_d4=Y6_GPIO_NUM; config.pin_d5=Y7_GPIO_NUM;
  config.pin_d6=Y8_GPIO_NUM; config.pin_d7=Y9_GPIO_NUM;
  config.pin_xclk=XCLK_GPIO_NUM; config.pin_pclk=PCLK_GPIO_NUM;
  config.pin_vsync=VSYNC_GPIO_NUM; config.pin_href=HREF_GPIO_NUM;
  config.pin_sscb_sda=SIOD_GPIO_NUM; config.pin_sscb_scl=SIOC_GPIO_NUM;
  config.pin_pwdn=PWDN_GPIO_NUM; config.pin_reset=RESET_GPIO_NUM;
  config.xclk_freq_hz=20000000; config.pixel_format=PIXFORMAT_JPEG;
  config.frame_size=FRAMESIZE_QVGA; config.jpeg_quality=12; config.fb_count=1;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera init failed!"); return;
  }
  Serial.println("Camera OK!");

  pinMode(TRIG_PIN, OUTPUT); pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  Serial.println("Ultrasonic OK!");

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500); Serial.print("."); attempts++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Connected!");
    Serial.print("Slave IP: http://"); Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi failed!");
  }

  startServer();

  // Register with master after short delay
  delay(1000);
  registerWithMaster();
  sendToMaster("Slave connected and ready!");

  Serial.println("GO!");
}

void loop() {
  unsigned long now = millis();

  if (now - lastWiFiCheck >= 10000) {
    lastWiFiCheck = now;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi lost! Reconnecting...");
      WiFi.reconnect();
    }
  }

  if (now - lastDistanceCheck >= 500) {
    lastDistanceCheck = now;
    distance = getDistance();

    // Object detection simulation — when close object found while searching
    if (searchMode && distance > 0 && distance < 15) {
      slaveStatus = "Found object!";
      sendToMaster("Found the black bottle!");
      searchMode = false;
      stopMotors();
    }
  }

  if (!searchMode) { stopMotors(); delay(10); return; }

  switch (robotState) {
    case 0:
      if (distance > 0 && distance <= 10) { stopMotors(); robotState=1; stateStartTime=now; }
      else moveForward();
      break;
    case 1: if (now-stateStartTime>=200) { moveBackward(); robotState=2; stateStartTime=now; } break;
    case 2: if (now-stateStartTime>=600) { stopMotors(); robotState=3; stateStartTime=now; } break;
    case 3: if (now-stateStartTime>=200) { if(random(2)==0)turnLeft();else turnRight(); robotState=4; stateStartTime=now; } break;
    case 4: if (now-stateStartTime>=700) { stopMotors(); robotState=0; stateStartTime=now; } break;
  }

  delay(10);
}

float getDistance() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long dur = pulseIn(ECHO_PIN, HIGH, 5000);
  if (dur == 0) return 999;
  return dur * 0.034 / 2;
}

void moveForward()  { digitalWrite(IN1,HIGH);digitalWrite(IN2,LOW);digitalWrite(IN3,HIGH);digitalWrite(IN4,LOW); }
void moveBackward() { digitalWrite(IN1,LOW);digitalWrite(IN2,HIGH);digitalWrite(IN3,LOW);digitalWrite(IN4,HIGH); }
void turnLeft()     { digitalWrite(IN1,LOW);digitalWrite(IN2,HIGH);digitalWrite(IN3,HIGH);digitalWrite(IN4,LOW); }
void turnRight()    { digitalWrite(IN1,HIGH);digitalWrite(IN2,LOW);digitalWrite(IN3,LOW);digitalWrite(IN4,HIGH); }
void stopMotors()   { digitalWrite(IN1,LOW);digitalWrite(IN2,LOW);digitalWrite(IN3,LOW);digitalWrite(IN4,LOW); }
