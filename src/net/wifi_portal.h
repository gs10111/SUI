// Portal Wi-Fi de bancada (folha 1/2, U1 ESP32-WROOM-32D): AP/STA opcionais servindo pagina e API HTTP.
// O radio nasce DESLIGADO: RF ligado polui a medida analogica da folha 2/2 e eleva o pico no +5 V.
#pragma once

#include <stdint.h>

#include <WebServer.h>

#include "iface/iradio.h"
#include "status.h"

struct Ctx;

class WifiPortal : public IRadio {
public:
    enum class Mode : uint8_t { Off = 0, AccessPoint, Station };

    static constexpr uint8_t kSsidCap = 33;
    static constexpr uint8_t kPassCap = 65;
    static constexpr uint16_t kHttpPort = 80;
    static constexpr uint8_t kIpCap = 16;
    static constexpr uint8_t kMinPassLen = 8;
    static constexpr uint32_t kStaPollMs = 20;

    static constexpr const char* kKeySsid = "wifi_ssid";
    static constexpr const char* kKeyPass = "wifi_pass";
    static constexpr const char* kKeyMode = "wifi_mode";

    explicit WifiPortal(Ctx& ctx);

    Status startAccessPoint(const char* apSsid, const char* apPass);
    Status startStation(const char* staSsid, const char* staPass, uint32_t timeoutMs);
    Status stop() override;
    void poll();

    bool running() const override { return running_; }
    Mode mode() const { return mode_; }
    const char* modeName() const override;
    const char* ssid() const { return ssid_; }
    const char* ipText() const { return ip_; }
    uint32_t requestsServed() const { return requests_; }

    bool controlEnabled() const { return control_; }
    void setControlEnabled(bool enabled);

    Status saveConfig();
    Status loadConfig();
    bool hasSavedConfig() const { return hasSaved_; }
    const char* savedSsid() const { return savedSsid_; }
    Mode savedMode() const { return savedMode_; }

private:
    static void onRoot();
    static void onStatus();
    static void onReport();
    static void onRelay();
    static void onAo();
    static void onMode();
    static void onSafe();
    static void onNotFound();

    void handleRoot();
    void handleStatus();
    void handleReport();
    void handleRelay();
    void handleAo();
    void handleMode();
    void handleSafe();
    void handleNotFound();

    void registerRoutes();
    void radioDown();
    void noteRadioUp();
    void captureIp(bool accessPoint);
    void storeCredentials(const char* newSsid, const char* newPass);
    Status checkCredentials(const char* newSsid, const char* newPass) const;

    bool argText(const char* key, char* out, uint16_t cap);
    bool controlAllowed();
    void sendJsonf(int code, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
    void sendError(int code, const char* message);
    void buildStatusJson(char* out, uint16_t cap);

    static WifiPortal* s_active;

    Ctx& ctx_;
    WebServer server_;
    Mode mode_;
    Mode lastMode_;
    Mode savedMode_;
    bool running_;
    bool control_;
    bool hasSaved_;
    bool routesReady_;
    uint32_t requests_;
    char ssid_[kSsidCap];
    char pass_[kPassCap];
    char ip_[kIpCap];
    char savedSsid_[kSsidCap];
    char savedPass_[kPassCap];
};

extern WifiPortal* g_wifiPortal;
