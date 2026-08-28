// Portal Wi-Fi de bancada (folha 1/2, U1 ESP32-WROOM-32D): WiFi.h + WebServer.h do core Arduino.
// Ligar o radio e ato deliberado: a comutacao de RF degrada as medidas analogicas da folha 2/2.
#include "net/wifi_portal.h"

#include <Arduino.h>
#include <WiFi.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "board_pins.h"
#include "core/cmd_parser.h"
#include "core/ctx.h"
#include "core/test_runner.h"
#include "net/web_page.h"
#include "status.h"
#include "verdict.h"

namespace {

constexpr uint16_t kOutCap = 2048;
constexpr uint16_t kReplyCap = 256;
constexpr uint16_t kBodyCap = 192;
constexpr uint8_t kFieldCap = 24;
constexpr uint8_t kEscapeCap = 40;
constexpr uint8_t kJsonDepth = 8;
constexpr uint16_t kJsonReserve = 16;

char g_out[kOutCap];
char g_reply[kReplyCap];

const char* boolText(bool value) {
    return value ? "true" : "false";
}

const char* aoModeText(AoMode value) {
    return (value == AoMode::Current) ? "corrente" : "tensao";
}

double safeNumber(float value) {
    if (!isfinite(value)) {
        return 0.0;
    }
    return static_cast<double>(value);
}

void escapeInto(char* out, uint16_t cap, const char* in) {
    if (out == nullptr || cap == 0u) {
        return;
    }
    uint16_t w = 0;
    if (in != nullptr) {
        for (uint16_t r = 0; in[r] != '\0' && (w + 2u) < cap; ++r) {
            const char c = in[r];
            const unsigned char u = static_cast<unsigned char>(c);
            if (c == '"' || c == '\\') {
                out[w++] = '\\';
                out[w++] = c;
            } else if (u < 0x20u || u > 0x7Eu) {
                out[w++] = ' ';
            } else {
                out[w++] = c;
            }
        }
    }
    out[w] = '\0';
}

int hexDigitValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

void urlDecode(const char* in, uint16_t len, char* out, uint16_t cap) {
    uint16_t w = 0;
    for (uint16_t r = 0; r < len && (w + 1u) < cap; ++r) {
        const char c = in[r];
        if (c == '+') {
            out[w++] = ' ';
            continue;
        }
        if (c == '%' && (r + 2u) < len) {
            const int hi = hexDigitValue(in[r + 1u]);
            const int lo = hexDigitValue(in[r + 2u]);
            if (hi >= 0 && lo >= 0) {
                out[w++] = static_cast<char>((hi << 4) | lo);
                r = static_cast<uint16_t>(r + 2u);
                continue;
            }
        }
        out[w++] = c;
    }
    out[w] = '\0';
}

bool fieldFromForm(const char* body, const char* key, char* out, uint16_t cap) {
    if (body == nullptr || key == nullptr || out == nullptr || cap == 0u) {
        return false;
    }
    const size_t keyLen = strlen(key);
    if (keyLen == 0u) {
        return false;
    }
    const char* p = body;
    while (*p != '\0') {
        const char* amp = strchr(p, '&');
        const size_t segLen = (amp != nullptr) ? static_cast<size_t>(amp - p) : strlen(p);
        if (segLen > keyLen && p[keyLen] == '=' && strncmp(p, key, keyLen) == 0) {
            urlDecode(p + keyLen + 1u, static_cast<uint16_t>(segLen - keyLen - 1u), out, cap);
            return true;
        }
        if (amp == nullptr) {
            break;
        }
        p = amp + 1;
    }
    return false;
}

Verdict verdictOfId(const Report& rep, const char* id) {
    for (uint8_t i = 0; i < rep.count(); ++i) {
        if (strcmp(rep.at(i).id, id) == 0) {
            return rep.at(i).verdict;
        }
    }
    return Verdict::NotRun;
}

class JsonBuf {
public:
    JsonBuf(char* buf, uint16_t cap) : buf_(buf), cap_(cap), len_(0), depth_(0), over_(false), closers_{} {
        if (buf_ != nullptr && cap_ > 0u) {
            buf_[0] = '\0';
        }
    }

    void add(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    void close(uint8_t levels);
    void finish() { close(depth_); }

private:
    void track(const char* text);
    void putChar(char c);
    void stripComma();

    char* buf_;
    uint16_t cap_;
    uint16_t len_;
    uint8_t depth_;
    bool over_;
    char closers_[kJsonDepth];
};

void JsonBuf::add(const char* fmt, ...) {
    if (buf_ == nullptr || fmt == nullptr || over_) {
        return;
    }
    const uint16_t limit = (cap_ > kJsonReserve) ? static_cast<uint16_t>(cap_ - kJsonReserve) : 0u;
    if (len_ >= limit) {
        over_ = true;
        return;
    }
    const size_t room = static_cast<size_t>(limit - len_);
    va_list ap;
    va_start(ap, fmt);
    const int written = vsnprintf(buf_ + len_, room, fmt, ap);
    va_end(ap);
    if (written < 0 || static_cast<size_t>(written) >= room) {
        buf_[len_] = '\0';
        over_ = true;
        return;
    }
    track(buf_ + len_);
    len_ = static_cast<uint16_t>(len_ + static_cast<uint16_t>(written));
}

void JsonBuf::close(uint8_t levels) {
    uint8_t remaining = levels;
    while (remaining > 0u && depth_ > 0u) {
        stripComma();
        putChar(closers_[depth_ - 1u]);
        --depth_;
        --remaining;
    }
    if (depth_ > 0u) {
        putChar(',');
    }
}

void JsonBuf::track(const char* text) {
    bool inString = false;
    bool escaped = false;
    for (const char* p = text; *p != '\0'; ++p) {
        const char c = *p;
        if (escaped) {
            escaped = false;
            continue;
        }
        if (inString) {
            if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '{' || c == '[') {
            if (depth_ < kJsonDepth) {
                closers_[depth_] = (c == '{') ? '}' : ']';
                ++depth_;
            }
        } else if (c == '}' || c == ']') {
            if (depth_ > 0u) {
                --depth_;
            }
        }
    }
}

void JsonBuf::putChar(char c) {
    if (buf_ == nullptr || static_cast<uint16_t>(len_ + 1u) >= cap_) {
        return;
    }
    buf_[len_] = c;
    ++len_;
    buf_[len_] = '\0';
}

void JsonBuf::stripComma() {
    while (len_ > 0u && buf_[len_ - 1u] == ',') {
        --len_;
        buf_[len_] = '\0';
    }
}

}  // namespace

WifiPortal* g_wifiPortal = nullptr;
WifiPortal* WifiPortal::s_active = nullptr;

WifiPortal::WifiPortal(Ctx& ctx)
    : ctx_(ctx),
      server_(kHttpPort),
      mode_(Mode::Off),
      lastMode_(Mode::Off),
      savedMode_(Mode::Off),
      running_(false),
      control_(false),
      hasSaved_(false),
      routesReady_(false),
      requests_(0),
      ssid_{},
      pass_{},
      ip_{},
      savedSsid_{},
      savedPass_{} {
    s_active = this;
    snprintf(ip_, sizeof(ip_), "0.0.0.0");
    registerRoutes();
}

void WifiPortal::registerRoutes() {
    if (routesReady_) {
        return;
    }
    server_.on("/", HTTP_GET, &WifiPortal::onRoot);
    server_.on("/api/status", HTTP_GET, &WifiPortal::onStatus);
    server_.on("/api/report", HTTP_GET, &WifiPortal::onReport);
    server_.on("/api/relay", HTTP_POST, &WifiPortal::onRelay);
    server_.on("/api/ao", HTTP_POST, &WifiPortal::onAo);
    server_.on("/api/mode", HTTP_POST, &WifiPortal::onMode);
    server_.on("/api/safe", HTTP_POST, &WifiPortal::onSafe);
    server_.onNotFound(&WifiPortal::onNotFound);
    routesReady_ = true;
}

const char* WifiPortal::modeName() const {
    switch (mode_) {
        case Mode::AccessPoint: return "AP";
        case Mode::Station: return "STA";
        case Mode::Off: return "OFF";
    }
    return "OFF";
}

void WifiPortal::setControlEnabled(bool enabled) {
    control_ = enabled;
}

Status WifiPortal::checkCredentials(const char* newSsid, const char* newPass) const {
    if (newSsid == nullptr || newSsid[0] == '\0') {
        return Status(Err::Param);
    }
    if (strlen(newSsid) >= kSsidCap) {
        return Status(Err::Range);
    }
    if (newPass != nullptr && newPass[0] != '\0') {
        const size_t len = strlen(newPass);
        if (len < kMinPassLen || len >= kPassCap) {
            return Status(Err::Range);
        }
    }
    return kOk;
}

void WifiPortal::storeCredentials(const char* newSsid, const char* newPass) {
    snprintf(ssid_, sizeof(ssid_), "%s", (newSsid != nullptr) ? newSsid : "");
    snprintf(pass_, sizeof(pass_), "%s", (newPass != nullptr) ? newPass : "");
}

void WifiPortal::captureIp(bool accessPoint) {
    const IPAddress addr = accessPoint ? WiFi.softAPIP() : WiFi.localIP();
    snprintf(ip_, sizeof(ip_), "%u.%u.%u.%u", static_cast<unsigned>(addr[0]), static_cast<unsigned>(addr[1]),
             static_cast<unsigned>(addr[2]), static_cast<unsigned>(addr[3]));
}

void WifiPortal::radioDown() {
    WiFi.mode(WIFI_OFF);
    mode_ = Mode::Off;
    running_ = false;
    control_ = false;
    snprintf(ip_, sizeof(ip_), "0.0.0.0");
}

void WifiPortal::noteRadioUp() {
    ctx_.io.writeLine("");
    ctx_.io.writeLine("======== RADIO WI-FI LIGADO ========");
    ctx_.io.printf("Modo %s   SSID %s   IP %s   HTTP na porta %u\r\n", modeName(), ssid_, ip_,
                   static_cast<unsigned>(kHttpPort));
    ctx_.io.printf("Senha: %s\r\n", (pass_[0] == '\0') ? "NENHUMA (rede aberta)" : "definida");
    ctx_.io.writeLine("A comutacao de RF injeta ruido na faixa das medidas analogicas e COMPROMETE a");
    ctx_.io.writeLine("tolerancia de +/-0,5 % de fundo de escala, alem de elevar o pico de corrente");
    ctx_.io.writeLine("no +5 V. Desligue com 'wifi off' antes de qualquer medida de precisao.");
    ctx_.io.writeLine("Controle pela pagina comeca BLOQUEADO: libere com 'wifi control on'.");
    ctx_.io.writeLine("====================================");
}

Status WifiPortal::startAccessPoint(const char* apSsid, const char* apPass) {
    const Status check = checkCredentials(apSsid, apPass);
    if (check.failed()) {
        return check;
    }
    stop();
    storeCredentials(apSsid, apPass);
    if (!WiFi.mode(WIFI_AP)) {
        radioDown();
        return Status(Err::Io);
    }
    const char* secret = (pass_[0] == '\0') ? nullptr : pass_;
    if (!WiFi.softAP(ssid_, secret)) {
        radioDown();
        return Status(Err::Io);
    }
    captureIp(true);
    mode_ = Mode::AccessPoint;
    lastMode_ = Mode::AccessPoint;
    running_ = true;
    control_ = false;
    requests_ = 0;
    server_.begin();
    noteRadioUp();
    return kOk;
}

Status WifiPortal::startStation(const char* staSsid, const char* staPass, uint32_t timeoutMs) {
    const Status check = checkCredentials(staSsid, staPass);
    if (check.failed()) {
        return check;
    }
    stop();
    storeCredentials(staSsid, staPass);
    if (!WiFi.mode(WIFI_STA)) {
        radioDown();
        return Status(Err::Io);
    }
    const char* net = ssid_;
    const char* secret = (pass_[0] == '\0') ? nullptr : pass_;
    if (WiFi.begin(net, secret) == WL_CONNECT_FAILED) {
        WiFi.disconnect(true, false);
        radioDown();
        return Status(Err::Io);
    }
    const uint32_t start = ctx_.io.nowMs();
    while (WiFi.status() != WL_CONNECTED) {
        if ((ctx_.io.nowMs() - start) >= timeoutMs) {
            WiFi.disconnect(true, false);
            radioDown();
            return Status(Err::Timeout);
        }
        ctx_.io.idle();
        delay(kStaPollMs);
    }
    captureIp(false);
    mode_ = Mode::Station;
    lastMode_ = Mode::Station;
    running_ = true;
    control_ = false;
    requests_ = 0;
    server_.begin();
    noteRadioUp();
    return kOk;
}

Status WifiPortal::stop() {
    if (running_) {
        server_.stop();
    }
    if (mode_ == Mode::AccessPoint) {
        WiFi.softAPdisconnect(true);
    } else if (mode_ == Mode::Station) {
        WiFi.disconnect(true, false);
    }
    radioDown();
    return kOk;
}

void WifiPortal::poll() {
    if (!running_) {
        return;
    }
    server_.handleClient();
}

Status WifiPortal::saveConfig() {
    const Mode target = (mode_ != Mode::Off) ? mode_ : lastMode_;
    if (ssid_[0] == '\0' || target == Mode::Off) {
        return Status(Err::NotInit);
    }
    Status st = ctx_.kv.putString(kKeySsid, ssid_);
    if (st.failed()) {
        return st;
    }
    st = ctx_.kv.putString(kKeyPass, pass_);
    if (st.failed()) {
        return st;
    }
    st = ctx_.kv.putU8(kKeyMode, static_cast<uint8_t>(target));
    if (st.failed()) {
        return st;
    }
    snprintf(savedSsid_, sizeof(savedSsid_), "%s", ssid_);
    snprintf(savedPass_, sizeof(savedPass_), "%s", pass_);
    savedMode_ = target;
    hasSaved_ = true;
    return kOk;
}

Status WifiPortal::loadConfig() {
    savedSsid_[0] = '\0';
    savedPass_[0] = '\0';
    savedMode_ = Mode::Off;
    hasSaved_ = false;

    char text[kSsidCap];
    text[0] = '\0';
    const Status st = ctx_.kv.getString(kKeySsid, text, sizeof(text));
    if (st.failed() || text[0] == '\0') {
        return Status(Err::Storage);
    }
    snprintf(savedSsid_, sizeof(savedSsid_), "%s", text);

    char secret[kPassCap];
    secret[0] = '\0';
    if (ctx_.kv.getString(kKeyPass, secret, sizeof(secret)).ok()) {
        snprintf(savedPass_, sizeof(savedPass_), "%s", secret);
    }

    uint8_t stored = 0;
    if (ctx_.kv.getU8(kKeyMode, stored).ok() && stored <= static_cast<uint8_t>(Mode::Station)) {
        savedMode_ = static_cast<Mode>(stored);
    }
    hasSaved_ = true;
    return kOk;
}

void WifiPortal::onRoot() {
    if (s_active != nullptr) {
        s_active->handleRoot();
    }
}

void WifiPortal::onStatus() {
    if (s_active != nullptr) {
        s_active->handleStatus();
    }
}

void WifiPortal::onReport() {
    if (s_active != nullptr) {
        s_active->handleReport();
    }
}

void WifiPortal::onRelay() {
    if (s_active != nullptr) {
        s_active->handleRelay();
    }
}

void WifiPortal::onAo() {
    if (s_active != nullptr) {
        s_active->handleAo();
    }
}

void WifiPortal::onMode() {
    if (s_active != nullptr) {
        s_active->handleMode();
    }
}

void WifiPortal::onSafe() {
    if (s_active != nullptr) {
        s_active->handleSafe();
    }
}

void WifiPortal::onNotFound() {
    if (s_active != nullptr) {
        s_active->handleNotFound();
    }
}

bool WifiPortal::argText(const char* key, char* out, uint16_t cap) {
    if (key == nullptr || out == nullptr || cap == 0u) {
        return false;
    }
    out[0] = '\0';
    if (server_.hasArg(key)) {
        snprintf(out, cap, "%s", server_.arg(key).c_str());
        return true;
    }
    if (!server_.hasArg("plain")) {
        return false;
    }
    char body[kBodyCap];
    snprintf(body, sizeof(body), "%s", server_.arg("plain").c_str());
    return fieldFromForm(body, key, out, cap);
}

void WifiPortal::sendJsonf(int code, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_reply, sizeof(g_reply), fmt, ap);
    va_end(ap);
    server_.send(code, "application/json", g_reply);
}

void WifiPortal::sendError(int code, const char* message) {
    char escaped[kEscapeCap * 2u];
    escapeInto(escaped, sizeof(escaped), message);
    sendJsonf(code, "{\"erro\":\"%s\"}", escaped);
}

bool WifiPortal::controlAllowed() {
    if (control_) {
        return true;
    }
    sendError(403, "controle bloqueado");
    return false;
}

void WifiPortal::handleRoot() {
    ++requests_;
    server_.send_P(200, "text/html", kWebPage);
}

void WifiPortal::handleStatus() {
    ++requests_;
    buildStatusJson(g_out, static_cast<uint16_t>(sizeof(g_out)));
    server_.send(200, "application/json", g_out);
}

void WifiPortal::handleReport() {
    ++requests_;
    char format[kFieldCap];
    const bool csv = argText("format", format, sizeof(format)) && cmd::equalsIgnoreCase(format, "csv");
    if (csv) {
        ctx_.report.formatCsv(g_out, static_cast<uint16_t>(sizeof(g_out)));
    } else {
        ctx_.report.formatHuman(g_out, static_cast<uint16_t>(sizeof(g_out)));
    }
    server_.send(200, "text/plain", g_out);
}

void WifiPortal::handleRelay() {
    ++requests_;
    if (!controlAllowed()) {
        return;
    }
    char indexText[kFieldCap];
    char stateText[kFieldCap];
    if (!argText("index", indexText, sizeof(indexText)) || !argText("state", stateText, sizeof(stateText))) {
        sendError(400, "corpo esperado: index=<0..3>&state=<0|1>");
        return;
    }
    uint32_t index = 0;
    if (!cmd::parseU32(indexText, index) || index >= board::kRelayCount) {
        sendError(400, "index fora de 0..3");
        return;
    }
    uint32_t state = 0;
    if (!cmd::parseU32(stateText, state) || state > 1u) {
        sendError(400, "state deve ser 0 ou 1");
        return;
    }
    if (index >= ctx_.relays.count()) {
        sendError(400, "index fora do banco de saidas");
        return;
    }
    const uint8_t slot = static_cast<uint8_t>(index);
    const Status st = ctx_.relays.set(slot, state != 0u);
    if (st.failed()) {
        sendJsonf(500, "{\"erro\":\"banco de reles recusou (%s)\"}", errName(st.err));
        return;
    }
    sendJsonf(200, "{\"ok\":true,\"index\":%u,\"net\":\"%s\",\"rele\":\"%s\",\"ligado\":%s}",
              static_cast<unsigned>(slot), board::kRelayMap[slot].net, board::kRelayMap[slot].relay,
              boolText(state != 0u));
}

void WifiPortal::handleAo() {
    ++requests_;
    if (!controlAllowed()) {
        return;
    }
    char axisText[kFieldCap];
    if (!argText("axis", axisText, sizeof(axisText))) {
        sendError(400, "corpo esperado: axis=<0|1> com code=<0..65535> ou value=<float>");
        return;
    }
    uint8_t axis = 0;
    if (!cmd::parseAxis(axisText, axis) || axis >= board::kAxisCount) {
        sendError(400, "axis deve ser 0 ou 1");
        return;
    }

    char codeText[kFieldCap];
    if (argText("code", codeText, sizeof(codeText))) {
        uint32_t code = 0;
        if (!cmd::parseU32(codeText, code) || code > 0xFFFFu) {
            sendError(400, "code fora de 0..65535");
            return;
        }
        const Status st = ctx_.ao.setRaw(axis, static_cast<uint16_t>(code));
        if (st.failed()) {
            sendJsonf(500, "{\"erro\":\"saida analogica recusou (%s)\"}", errName(st.err));
            return;
        }
        sendJsonf(200, "{\"ok\":true,\"eixo\":\"%s\",\"code\":%u}", board::kAxisName[axis],
                  static_cast<unsigned>(code));
        return;
    }

    char valueText[kFieldCap];
    if (!argText("value", valueText, sizeof(valueText))) {
        sendError(400, "informe code=<0..65535> ou value=<float>");
        return;
    }
    float value = 0.0f;
    if (!cmd::parseFloat(valueText, value)) {
        sendError(400, "value nao e um numero valido");
        return;
    }
    const AoMode active = ctx_.ao.mode();
    if (!ctx_.cal.has(axis, active)) {
        sendJsonf(409, "{\"erro\":\"eixo %s sem calibracao no modo %s: rode 'cal' no console\"}",
                  board::kAxisName[axis], aoModeText(active));
        return;
    }
    const Status st = ctx_.ao.setEngineering(axis, value);
    if (st.err == Err::NotCalibrated) {
        sendJsonf(409, "{\"erro\":\"eixo %s sem calibracao no modo %s: rode 'cal' no console\"}",
                  board::kAxisName[axis], aoModeText(active));
        return;
    }
    if (st.failed()) {
        sendJsonf(500, "{\"erro\":\"saida analogica recusou (%s)\"}", errName(st.err));
        return;
    }
    uint16_t applied = 0;
    ctx_.ao.getRaw(axis, applied);
    sendJsonf(200, "{\"ok\":true,\"eixo\":\"%s\",\"modo\":\"%s\",\"valor\":%.4f,\"code\":%u}",
              board::kAxisName[axis], aoModeText(active), safeNumber(value),
              static_cast<unsigned>(applied));
}

void WifiPortal::handleMode() {
    ++requests_;
    if (!controlAllowed()) {
        return;
    }
    char modeText[kFieldCap];
    if (!argText("mode", modeText, sizeof(modeText))) {
        sendError(400, "corpo esperado: mode=v|i");
        return;
    }
    AoMode target = AoMode::Voltage;
    if (cmd::equalsIgnoreCase(modeText, "v")) {
        target = AoMode::Voltage;
    } else if (cmd::equalsIgnoreCase(modeText, "i")) {
        target = AoMode::Current;
    } else {
        sendError(400, "mode deve ser v ou i");
        return;
    }
    const Status st = ctx_.ao.setMode(target);
    if (st.failed()) {
        sendJsonf(500, "{\"erro\":\"XTR300 recusou a troca de modo (%s)\"}", errName(st.err));
        return;
    }
    sendJsonf(200, "{\"ok\":true,\"modo\":\"%s\"}", aoModeText(ctx_.ao.mode()));
}

void WifiPortal::handleSafe() {
    ++requests_;
    if (!controlAllowed()) {
        return;
    }
    ctx_.safe.enterSafeState();
    sendJsonf(200, "{\"ok\":true,\"estado\":\"seguro\"}");
}

void WifiPortal::handleNotFound() {
    ++requests_;
    sendError(404, "rota desconhecida");
}

void WifiPortal::buildStatusJson(char* out, uint16_t cap) {
    JsonBuf json(out, cap);
    char escaped[kEscapeCap];

    json.add("{");
    json.add("\"fw\":\"%s\",", ctx_.fwVersion);
    json.add("\"rev\":\"%s\",", ctx_.boardRev);
    escapeInto(escaped, sizeof(escaped), ctx_.report.serial());
    json.add("\"serie\":\"%s\",", escaped);
    json.add("\"uptimeMs\":%lu,", static_cast<unsigned long>(ctx_.io.nowMs()));
    json.add("\"resetReason\":\"%s\",",
             (ctx_.boot.resetReasonName != nullptr) ? ctx_.boot.resetReasonName : "?");

    escapeInto(escaped, sizeof(escaped), ssid_);
    json.add("\"radio\":{\"modo\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"controle\":%s,\"req\":%lu},",
             modeName(), escaped, ip_, boolText(control_), static_cast<unsigned long>(requests_));

    json.add("\"wdt\":{\"chutando\":%s,\"chutes\":%lu,\"periodoMs\":%lu},", boolText(ctx_.wdt.kicking()),
             static_cast<unsigned long>(ctx_.wdt.kickCount()),
             static_cast<unsigned long>(ctx_.wdt.kickPeriodMs()));

    const AoMode aoMode = ctx_.ao.mode();
    json.add("\"ao\":{\"modo\":\"%s\",\"spiHz\":%lu,\"eixos\":[", aoModeText(aoMode),
             static_cast<unsigned long>(ctx_.ao.spiHz()));
    for (uint8_t axis = 0; axis < board::kAxisCount; ++axis) {
        uint16_t code = 0;
        ctx_.ao.getRaw(axis, code);
        const bool calibrated = ctx_.cal.has(axis, aoMode);
        float value = 0.0f;
        if (calibrated) {
            ctx_.cal.valueFor(axis, aoMode, code, value);
        }
        json.add("{\"nome\":\"%s\",\"code\":%u,\"calibrado\":%s,\"valor\":%.4f},", board::kAxisName[axis],
                 static_cast<unsigned>(code), boolText(calibrated), safeNumber(value));
    }
    json.close(2);

    json.add("\"reles\":[");
    for (uint8_t i = 0; i < board::kRelayCount; ++i) {
        bool on = false;
        if (i < ctx_.relays.count()) {
            ctx_.relays.get(i, on);
        }
        json.add("{\"net\":\"%s\",\"rele\":\"%s\",\"bornes\":\"%s\",\"ligado\":%s},",
                 board::kRelayMap[i].net, board::kRelayMap[i].relay, board::kRelayMap[i].screwTerminals,
                 boolText(on));
    }
    json.close(1);

    const SerialStats& stats = ctx_.rs485.stats();
    const Angle angle = {0.0f, 0.0f, false};
    json.add("\"rs485\":{\"baud\":%lu,\"protocolo\":\"%s\",\"framesOk\":%lu,\"crc\":%lu,\"timeout\":%lu,"
             "\"enquadramento\":%lu,\"anguloX\":%.2f,\"anguloY\":%.2f,\"anguloValido\":%s},",
             static_cast<unsigned long>(ctx_.rs485.baud()), ctx_.proto.name(),
             static_cast<unsigned long>(stats.framesOk), static_cast<unsigned long>(stats.crcErrors),
             static_cast<unsigned long>(stats.timeouts), static_cast<unsigned long>(stats.framingErrors),
             safeNumber(angle.x), safeNumber(angle.y), boolText(angle.valid));

    json.add("\"display\":\"%s\",", ctx_.display.driverName());

    const bool busy = (ctx_.runner != nullptr) && ctx_.runner->busy();
    json.add("\"teste\":{\"emExecucao\":%s,\"itens\":[", boolText(busy));
    uint8_t total = TestRegistry::count();
    if (total > TestRegistry::kMax) {
        total = TestRegistry::kMax;
    }
    uint16_t used = 0;
    for (uint8_t slot = 0; slot < total; ++slot) {
        uint8_t best = total;
        for (uint8_t i = 0; i < total; ++i) {
            if ((used & static_cast<uint16_t>(1u << i)) != 0u) {
                continue;
            }
            const ITest* candidate = TestRegistry::at(i);
            if (candidate == nullptr) {
                used = static_cast<uint16_t>(used | static_cast<uint16_t>(1u << i));
                continue;
            }
            if (best >= total || candidate->order() < TestRegistry::at(best)->order()) {
                best = i;
            }
        }
        if (best >= total) {
            break;
        }
        used = static_cast<uint16_t>(used | static_cast<uint16_t>(1u << best));
        const ITest* item = TestRegistry::at(best);
        json.add("{\"id\":\"%s\",\"nome\":\"%s\",\"veredito\":\"%s\"},", item->id(), item->name(),
                 verdictName(verdictOfId(ctx_.report, item->id())));
    }
    json.finish();
}
