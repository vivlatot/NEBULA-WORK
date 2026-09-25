// =====================================================================
//  Cerebro del osito: WiFi + servidor puente (bridge/servidor.py)
//
//  - La configuracion (WiFi, servidor, token) se guarda en NVS.
//  - Si no hay configuracion, o el WiFi falla, la placa abre su propia
//    red "Osito-Config" con un formulario en http://192.168.4.1
//  - Si existe include/secrets.h, sus valores se usan por defecto.
// =====================================================================
#include "ai.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#if __has_include("secrets.h")
  #include "secrets.h"
#endif
#ifndef WIFI_SSID
  #define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
  #define WIFI_PASS ""
#endif
#ifndef OSITO_SERVER
  #define OSITO_SERVER ""
#endif
#ifndef OSITO_TOKEN
  #define OSITO_TOKEN "osito"
#endif

static const char* AP_NAME = "Osito-Config";

struct NetConfig {
  String ssid, pass, server, token;
  bool valid() const { return ssid.length() && server.length(); }
};

struct Request {
  char    event[24];
  AiState st;
};

static NetConfig     cfg;
static QueueHandle_t reqQueue = nullptr;
static QueueHandle_t replyQueue = nullptr;
static volatile bool busy = false;
static volatile bool online = false;
static volatile bool portalOn = false;
static WebServer     portal(80);

// ---------------------------------------------------------------------
static void loadConfig() {
  Preferences p;
  p.begin("osito-net", true);
  cfg.ssid   = p.getString("ssid", WIFI_SSID);
  cfg.pass   = p.getString("pass", WIFI_PASS);
  cfg.server = p.getString("server", OSITO_SERVER);
  cfg.token  = p.getString("token", OSITO_TOKEN);
  p.end();
  while (cfg.server.endsWith("/")) cfg.server.remove(cfg.server.length() - 1);
}

static String htmlEscape(const String& s) {
  String o;
  for (char c : s) {
    if (c == '"') o += "&quot;";
    else if (c == '<') o += "&lt;";
    else if (c == '&') o += "&amp;";
    else o += c;
  }
  return o;
}

static void handleRoot() {
  String h = F("<!doctype html><html lang=es><head><meta charset=utf-8>"
               "<meta name=viewport content='width=device-width,initial-scale=1'>"
               "<title>Osito</title><style>body{font-family:sans-serif;background:#fff0ec;color:#4e281c;"
               "max-width:420px;margin:auto;padding:16px}input{width:100%;padding:10px;margin:4px 0 12px;"
               "box-sizing:border-box;border-radius:8px;border:2px solid #4e281c}button{padding:12px 20px;"
               "border-radius:10px;background:#f4a8b8;border:2px solid #4e281c;font-size:1rem}</style></head>"
               "<body><h2>Configura a tu osito</h2><form method=post action=/guardar>"
               "WiFi de casa (2.4 GHz)<input name=ssid value=\"");
  h += htmlEscape(cfg.ssid);
  h += F("\">Contrasena del WiFi<input name=pass type=password value=\"");
  h += htmlEscape(cfg.pass);
  h += F("\">Servidor (lo que muestra servidor.py)<input name=server placeholder='http://192.168.1.50:8765' value=\"");
  h += htmlEscape(cfg.server);
  h += F("\">Token<input name=token value=\"");
  h += htmlEscape(cfg.token);
  h += F("\"><button>Guardar</button></form></body></html>");
  portal.send(200, "text/html", h);
}

static void handleSave() {
  Preferences p;
  p.begin("osito-net", false);
  p.putString("ssid", portal.arg("ssid"));
  p.putString("pass", portal.arg("pass"));
  p.putString("server", portal.arg("server"));
  p.putString("token", portal.arg("token"));
  p.end();
  portal.send(200, "text/html",
              "<meta charset=utf-8><body style='font-family:sans-serif'><h2>Guardado!</h2>"
              "El osito se reinicia y se conecta a tu WiFi.</body>");
  delay(1500);
  ESP.restart();
}

static void startPortal() {
  if (portalOn) return;
  WiFi.mode(cfg.valid() ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAP(AP_NAME);
  portal.on("/", HTTP_GET, handleRoot);
  portal.on("/guardar", HTTP_POST, handleSave);
  portal.onNotFound(handleRoot);
  portal.begin();
  portalOn = true;
  Serial.printf("[ia] Portal abierto: WiFi '%s' -> http://192.168.4.1\n", AP_NAME);
}

// ---------------------------------------------------------------------
static bool httpCall(const char* method, const String& path, const String& body, String& out) {
  String url = cfg.server + path;
  HTTPClient http;
  WiFiClientSecure tls;
  bool ok;
  if (url.startsWith("https://")) {
    tls.setInsecure();  // servidor propio; para produccion, fija el certificado
    ok = http.begin(tls, url);
  } else {
    ok = http.begin(url);
  }
  if (!ok) return false;
  http.setTimeout(20000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Osito-Token", cfg.token);
  int code = strcmp(method, "POST") == 0 ? http.POST(body) : http.GET();
  if (code == 200) out = http.getString();
  else Serial.printf("[ia] %s %s -> %d\n", method, path.c_str(), code);
  http.end();
  return code == 200;
}

static Emotion parseEmotion(const char* e) {
  if (!e) return EMO_NORMAL;
  if (!strcmp(e, "feliz")) return EMO_HAPPY;
  if (!strcmp(e, "triste")) return EMO_SAD;
  if (!strcmp(e, "sorprendido")) return EMO_SURPRISED;
  if (!strcmp(e, "sueno")) return EMO_SLEEPY;
  if (!strcmp(e, "enamorado")) return EMO_LOVE;
  return EMO_NORMAL;
}

static void pushReply(JsonDocument& doc) {
  AiReply r{};
  strlcpy(r.text, doc["texto"] | "", sizeof(r.text));
  r.emotion = parseEmotion(doc["emocion"]);
  if (r.text[0]) xQueueSend(replyQueue, &r, 0);
}

static void doRequest(const Request& q) {
  JsonDocument doc;
  doc["evento"] = q.event;
  JsonObject s = doc["stats"].to<JsonObject>();
  s["food"] = q.st.food;
  s["fun"] = q.st.fun;
  s["energy"] = q.st.energy;
  s["hygiene"] = q.st.hygiene;
  doc["sleeping"] = q.st.sleeping;
  doc["sick"] = q.st.sick;
  doc["poops"] = q.st.poops;
  doc["age_min"] = q.st.ageMin;
  String body, out;
  serializeJson(doc, body);
  if (httpCall("POST", "/hablar", body, out)) {
    JsonDocument res;
    if (!deserializeJson(res, out)) pushReply(res);
  }
}

static portMUX_TYPE agentMux = portMUX_INITIALIZER_UNLOCKED;
static AgentInfo     agent{AG_NONE, ""};
static uint32_t      agentSeq = 0;
static bool          agentChanged = false;

static AgentMode parseAgent(const char* e) {
  if (!e) return AG_NONE;
  if (!strcmp(e, "saludo")) return AG_HELLO;
  if (!strcmp(e, "pensando")) return AG_THINK;
  if (!strcmp(e, "trabajando")) return AG_WORK;
  if (!strcmp(e, "permiso")) return AG_ASK;
  if (!strcmp(e, "terminado")) return AG_DONE;
  return AG_NONE;
}

static void pollInbox() {
  String out;
  if (!httpCall("GET", "/bandeja", "", out)) return;
  JsonDocument res;
  if (deserializeJson(res, out)) return;
  if (res["hay"] == true) pushReply(res);
  uint32_t seq = res["seq"] | 0;
  if (seq != agentSeq) {
    agentSeq = seq;
    portENTER_CRITICAL(&agentMux);
    agent.mode = parseAgent(res["agente"]);
    strlcpy(agent.tool, res["herramienta"] | "", sizeof(agent.tool));
    agentChanged = true;
    portEXIT_CRITICAL(&agentMux);
  }
}

// ---------------------------------------------------------------------
static void netTask(void*) {
  uint32_t lastInbox = 0, connectStart = 0;
  bool connecting = false;

  if (!cfg.valid()) startPortal();
  else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
    connecting = true;
    connectStart = millis();
  }

  for (;;) {
    if (portalOn) portal.handleClient();

    if (cfg.valid()) {
      online = WiFi.status() == WL_CONNECTED;
      if (online && connecting) {
        connecting = false;
        Serial.printf("[ia] WiFi OK, IP %s\n", WiFi.localIP().toString().c_str());
      }
      if (!online && !connecting) {  // se ha caido: reintentar
        WiFi.reconnect();
        connecting = true;
        connectStart = millis();
      }
      if (!online && connecting && millis() - connectStart > 30000) {
        startPortal();  // no conecta: deja arreglar los datos sin perder el reintento
        connectStart = millis();
      }
    }

    Request q;
    if (xQueueReceive(reqQueue, &q, pdMS_TO_TICKS(portalOn ? 5 : 100)) == pdTRUE) {
      if (online) doRequest(q);
      busy = false;
    } else if (online && millis() - lastInbox > 2500) {
      lastInbox = millis();
      pollInbox();
    }
  }
}

// ---------------------------------------------------------------------
void aiBegin() {
  loadConfig();
  reqQueue = xQueueCreate(1, sizeof(Request));
  replyQueue = xQueueCreate(3, sizeof(AiReply));
  xTaskCreatePinnedToCore(netTask, "osito-net", 8192, nullptr, 1, nullptr, 0);
}

bool aiEnabled() { return cfg.valid(); }
bool aiOnline() { return online; }
bool aiBusy() { return busy; }
bool aiPortal() { return portalOn; }

bool aiRequest(const char* event, const AiState& st) {
  if (!reqQueue || !online || busy) return false;
  Request q{};
  strlcpy(q.event, event, sizeof(q.event));
  q.st = st;
  busy = true;
  if (xQueueSend(reqQueue, &q, 0) != pdTRUE) {
    busy = false;
    return false;
  }
  return true;
}

bool aiPoll(AiReply& out) { return replyQueue && xQueueReceive(replyQueue, &out, 0) == pdTRUE; }

bool aiAgent(AgentInfo& out) {
  bool changed;
  portENTER_CRITICAL(&agentMux);
  changed = agentChanged;
  if (changed) {
    out = agent;
    agentChanged = false;
  }
  portEXIT_CRITICAL(&agentMux);
  return changed;
}
