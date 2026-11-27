#include <DNSServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <sys/time.h>
#include "panel.h"
#include "storage.h"
#include "web.h"
#include "web_page.h"

namespace Web {
namespace {
constexpr uint32_t ConnectTimeoutMs = 20000;

WebServer server(80);
DNSServer dns;
Mode current = Mode::Connecting;
String savedSsid;
uint32_t connectStartedAt = 0;
bool everConnected = false;
bool restartPending = false;
uint32_t restartAt = 0;

void startSetup() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP_STA);  // STA side is needed to scan for networks
    WiFi.softAP(SetupSsid, SetupPassword);
    // Hand out the board as DNS server; without it phones never find the setup page.
    const IPAddress ip(192, 168, 4, 1);
    WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0), IPAddress(static_cast<uint32_t>(0)), ip);
    dns.start(53, "*", WiFi.softAPIP());  // every name points here: phones open the page by themselves
    current = Mode::Setup;
    Serial.printf("Wi-Fi setup: join \"%s\" (password %s), open http://%s\n", SetupSsid, SetupPassword,
                  WiFi.softAPIP().toString().c_str());
}

void sendJson(const JsonDocument& doc, int code = 200) {
    String out;
    serializeJson(doc, out);
    server.send(code, "application/json", out);
}

void sendOk() { server.send(200, "application/json", "{\"ok\":true}"); }
void sendError(const char* message) {
    JsonDocument doc;
    doc["error"] = message;
    sendJson(doc, 400);
}

bool readBody(JsonDocument& doc) {
    if (deserializeJson(doc, server.arg("plain"))) {
        sendError("Bad JSON");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- handlers
void handleStatus() {
    const PanelStatus panel = panelStatus();
    JsonDocument doc;
    doc["status"] = panel.status;
    doc["filling"] = panel.filling;
    doc["weight"] = panel.weight / 10.0;
    doc["target"] = panel.target / 10.0;
    doc["recipe"] = Recipes::active().id;
    doc["recipeName"] = Recipes::active().name;
    doc["revision"] = Recipes::revision();
    doc["clockSet"] = clockNow() != 0;
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["mode"] = current == Mode::Connected ? "connected" : (current == Mode::Setup ? "setup" : "connecting");
    wifi["network"] = network();
    wifi["address"] = address();
    wifi["rssi"] = current == Mode::Connected ? WiFi.RSSI() : 0;
    sendJson(doc);
}

void handleRecipes() {
    JsonDocument doc;
    doc["active"] = Recipes::active().id;
    JsonArray list = doc["recipes"].to<JsonArray>();
    for (int i = 0; i < Recipes::count(); ++i) Recipes::toJson(list.add<JsonObject>(), Recipes::at(i));
    sendJson(doc);
}

void handleSaveRecipe() {
    JsonDocument body;
    if (!readBody(body)) return;
    Recipes::Recipe recipe;
    Recipes::fromJson(body.as<JsonObjectConst>(), recipe);
    recipe.id = body["id"] | 0;
    const uint32_t id = Recipes::upsert(recipe);
    if (!id) return sendError("Could not save");
    JsonDocument doc;
    doc["id"] = id;
    sendJson(doc);
}

void handleDeleteRecipe() {
    if (!Recipes::remove(server.arg("id").toInt())) return sendError("Cannot delete the last recipe");
    sendOk();
}

void handleSelectRecipe() {
    const int index = Recipes::indexOf(server.arg("id").toInt());
    if (index < 0) return sendError("Unknown recipe");
    if (panelStatus().filling) return sendError("Busy filling");
    Recipes::select(index);
    sendOk();
}

void handleLog() {
    File file = LittleFS.exists(FillLog::Path) ? LittleFS.open(FillLog::Path, "r") : File();
    if (server.hasArg("download")) server.sendHeader("Content-Disposition", "attachment; filename=powderreload-log.csv");
    if (!file) return server.send(200, "text/csv", "");
    server.streamFile(file, "text/csv");
    file.close();
}

void handleTime() {
    JsonDocument body;
    if (!readBody(body)) return;
    const uint32_t now = body["time"] | 0;
    // The browser's clock: lets the log have dates even without internet.
    if (now > 1700000000 && !clockNow()) {
        timeval tv{static_cast<time_t>(now), 0};
        settimeofday(&tv, nullptr);
    }
    sendOk();
}

void handleScan() {
    const int found = WiFi.scanNetworks();
    JsonDocument doc;
    JsonArray list = doc.to<JsonArray>();
    for (int i = 0; i < found; ++i) {
        if (WiFi.SSID(i).isEmpty()) continue;
        JsonObject item = list.add<JsonObject>();
        item["ssid"] = WiFi.SSID(i);
        item["rssi"] = WiFi.RSSI(i);
        item["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    sendJson(doc);
}

void handleWifi() {
    JsonDocument body;
    if (!readBody(body)) return;
    const String ssid = body["ssid"] | "";
    if (ssid.isEmpty()) return sendError("Pick a network");
    Preferences prefs;
    prefs.begin("wifi", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", body["password"] | "");
    prefs.end();
    sendOk();
    restartPending = true;  // restart after the reply is sent
    restartAt = millis() + 1000;
}

// Setup mode: send every foreign request (phone "internet check" pages included) to the setup page,
// which makes phones pop it up on their own.
bool redirectToPortal() {
    if (current != Mode::Setup) return false;
    const String portal = WiFi.softAPIP().toString();
    if (server.hostHeader() == portal) return false;
    server.sendHeader("Location", "http://" + portal + "/", true);
    server.send(302, "text/plain", "");
    return true;
}

void handleNotFound() {
    if (redirectToPortal()) return;
    if (current == Mode::Setup) {
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "");
        return;
    }
    server.send(404, "text/plain", "Not found");
}
}

void begin() {
    WiFi.persistent(false);
    WiFi.setHostname(Hostname);
    Preferences prefs;
    prefs.begin("wifi", false);  // read-write creates the namespace, so no error on first boot
    savedSsid = prefs.isKey("ssid") ? prefs.getString("ssid") : String();
    const String password = prefs.isKey("pass") ? prefs.getString("pass") : String();
    prefs.end();

    if (savedSsid.isEmpty()) {
        startSetup();
    } else {
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        WiFi.begin(savedSsid.c_str(), password.c_str());
        current = Mode::Connecting;
        connectStartedAt = millis();
    }

    server.on("/", HTTP_GET, [] {
        if (!redirectToPortal()) server.send(200, "text/html", WebPage);
    });
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/fill", HTTP_POST, [] { panelFill() ? sendOk() : sendError("Cannot fill now"); });
    server.on("/api/stop", HTTP_POST, [] { panelStop(); sendOk(); });
    server.on("/api/tare", HTTP_POST, [] { panelTare(); sendOk(); });
    server.on("/api/recipes", HTTP_GET, handleRecipes);
    server.on("/api/recipes", HTTP_POST, handleSaveRecipe);
    server.on("/api/recipes/delete", HTTP_POST, handleDeleteRecipe);
    server.on("/api/recipes/select", HTTP_POST, handleSelectRecipe);
    server.on("/api/log", HTTP_GET, handleLog);
    server.on("/api/log/clear", HTTP_POST, [] { FillLog::clear(); sendOk(); });
    server.on("/api/time", HTTP_POST, handleTime);
    server.on("/api/scan", HTTP_GET, handleScan);
    server.on("/api/wifi", HTTP_POST, handleWifi);
    server.onNotFound(handleNotFound);
    server.begin();
}

void loop() {
    if (current == Mode::Setup) dns.processNextRequest();
    server.handleClient();

    if (restartPending && static_cast<int32_t>(millis() - restartAt) >= 0) ESP.restart();

    const bool connected = WiFi.status() == WL_CONNECTED;
    if (current == Mode::Connecting && connected) {
        current = Mode::Connected;
        if (!everConnected) {
            everConnected = true;
            MDNS.begin(Hostname);
            MDNS.addService("http", "tcp", 80);
            configTime(0, 0, "pool.ntp.org", "time.google.com");
        }
        Serial.printf("Wi-Fi: %s  http://%s.local  http://%s\n", savedSsid.c_str(), Hostname,
                      WiFi.localIP().toString().c_str());
    } else if (current == Mode::Connected && !connected) {
        current = Mode::Connecting;  // auto-reconnect is on
    } else if (current == Mode::Connecting && !everConnected && millis() - connectStartedAt > ConnectTimeoutMs) {
        Serial.println("Wi-Fi: cannot join the saved network");
        startSetup();
    }
}

Mode mode() { return current; }

String network() { return current == Mode::Setup ? String(SetupSsid) : savedSsid; }

String address() {
    if (current == Mode::Setup) return WiFi.softAPIP().toString();
    if (current == Mode::Connected) return WiFi.localIP().toString();
    return "";
}

void forget() {
    Preferences prefs;
    prefs.begin("wifi", false);
    prefs.clear();
    prefs.end();
    ESP.restart();
}
}
