// Comando 'wifi' do console (folha 1/2, U1 ESP32-WROOM-32D): sobe/derruba o radio e libera o controle web.
// Ligar o radio e ato deliberado: com RF no ar a medida analogica da folha 2/2 perde a exatidao.
#include <string.h>

#include "core/cmd_parser.h"
#include "core/command.h"
#include "core/ctx.h"
#include "net/wifi_portal.h"
#include "status.h"

namespace {

constexpr uint32_t kStaTimeoutMs = 15000;

const char* yesNo(bool value) {
    return value ? "SIM" : "NAO";
}

const char* modeText(WifiPortal::Mode value) {
    switch (value) {
        case WifiPortal::Mode::AccessPoint: return "AP";
        case WifiPortal::Mode::Station: return "STA";
        case WifiPortal::Mode::Off: return "OFF";
    }
    return "OFF";
}

WifiPortal* portalOf(Ctx& ctx) {
    if (g_wifiPortal == nullptr) {
        ctx.io.writeLine("ERRO: portal indisponivel (nenhum WifiPortal registrado em g_wifiPortal)");
        return nullptr;
    }
    return g_wifiPortal;
}

void printRadioAlert(Ctx& ctx) {
    ctx.io.writeLine("**********************************************************************");
    ctx.io.writeLine("* RADIO LIGADO: NAO faca medida analogica de precisao agora.          *");
    ctx.io.writeLine("* A comutacao de RF injeta ruido na faixa das medidas e compromete a  *");
    ctx.io.writeLine("* tolerancia de +/-0,5 % de fundo de escala; o pico no +5 V tambem    *");
    ctx.io.writeLine("* sobe. Desligue com 'wifi off' antes de medir.                       *");
    ctx.io.writeLine("**********************************************************************");
}

void printAccess(Ctx& ctx, WifiPortal& portal) {
    ctx.io.printf("Modo %s   SSID %s   IP %s\r\n", portal.modeName(), portal.ssid(), portal.ipText());
    ctx.io.printf("Pagina em http://%s/   estado em http://%s/api/status\r\n", portal.ipText(),
                  portal.ipText());
    ctx.io.printf("Controle pela pagina: %s (libere com 'wifi control on')\r\n",
                  portal.controlEnabled() ? "LIBERADO" : "BLOQUEADO");
}

class CmdWifi : public ICommand {
public:
    const char* name() const override { return "wifi"; }

    const char* usage() const override {
        return "uso: wifi status | wifi ap <ssid> [senha] | wifi sta <ssid> <senha> | wifi off | "
               "wifi control <on|off> | wifi save | wifi forget";
    }

    void execute(Ctx& ctx, uint8_t argc, const char* const* argv) override {
        WifiPortal* portal = portalOf(ctx);
        if (portal == nullptr) {
            return;
        }
        if (argc < 2) {
            doStatus(ctx, *portal);
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "status")) {
            doStatus(ctx, *portal);
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "ap")) {
            doAp(ctx, *portal, argc, argv);
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "sta")) {
            doSta(ctx, *portal, argc, argv);
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "off")) {
            doOff(ctx, *portal);
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "control")) {
            doControl(ctx, *portal, argc, argv);
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "save")) {
            doSave(ctx, *portal);
            return;
        }
        if (cmd::equalsIgnoreCase(argv[1], "forget")) {
            doForget(ctx, *portal);
            return;
        }
        ctx.io.writeLine(usage());
    }

private:
    void doStatus(Ctx& ctx, WifiPortal& portal) {
        if (!portal.hasSavedConfig()) {
            portal.loadConfig();
        }
        ctx.io.writeLine("---- PORTAL WI-FI ----");
        ctx.io.printf("Radio        : %s\r\n", portal.running() ? "LIGADO" : "DESLIGADO");
        ctx.io.printf("Modo         : %s\r\n", portal.modeName());
        ctx.io.printf("%-13s: %s\r\n", portal.running() ? "SSID" : "Ultimo SSID",
                      (portal.ssid()[0] != '\0') ? portal.ssid() : "(nenhum)");
        ctx.io.printf("IP           : %s   porta HTTP %u\r\n", portal.ipText(),
                      static_cast<unsigned>(WifiPortal::kHttpPort));
        ctx.io.printf("Requisicoes  : %lu\r\n", static_cast<unsigned long>(portal.requestsServed()));
        ctx.io.printf("Controle     : %s\r\n", portal.controlEnabled() ? "LIBERADO" : "BLOQUEADO");
        if (portal.hasSavedConfig()) {
            ctx.io.printf("Config na NVS: SSID %s   modo %s\r\n", portal.savedSsid(),
                          modeText(portal.savedMode()));
        } else {
            ctx.io.writeLine("Config na NVS: nenhuma");
        }
        ctx.io.printf("Medida fina  : liberada? %s\r\n", yesNo(!portal.running()));
        if (portal.running()) {
            printRadioAlert(ctx);
        }
    }

    void doAp(Ctx& ctx, WifiPortal& portal, uint8_t argc, const char* const* argv) {
        if (argc < 3) {
            ctx.io.writeLine(usage());
            return;
        }
        const char* secret = (argc >= 4) ? argv[3] : nullptr;
        if (secret == nullptr) {
            ctx.io.writeLine("AVISO: sem senha a rede sobe ABERTA: qualquer um no alcance abre a pagina.");
        } else if (strlen(secret) < WifiPortal::kMinPassLen) {
            ctx.io.printf("ERRO: senha WPA2 exige no minimo %u caracteres.\r\n",
                          static_cast<unsigned>(WifiPortal::kMinPassLen));
            return;
        }
        const Status st = portal.startAccessPoint(argv[2], secret);
        if (st.failed()) {
            ctx.io.printf("wifi ap: ERRO %s (ssid ate %u caracteres, senha de %u a %u)\r\n", errName(st.err),
                          static_cast<unsigned>(WifiPortal::kSsidCap - 1u),
                          static_cast<unsigned>(WifiPortal::kMinPassLen),
                          static_cast<unsigned>(WifiPortal::kPassCap - 1u));
            return;
        }
        printAccess(ctx, portal);
    }

    void doSta(Ctx& ctx, WifiPortal& portal, uint8_t argc, const char* const* argv) {
        if (argc < 4) {
            ctx.io.writeLine(usage());
            return;
        }
        ctx.io.printf("Conectando em '%s': o console fica parado por ate %lu ms.\r\n", argv[2],
                      static_cast<unsigned long>(kStaTimeoutMs));
        const Status st = portal.startStation(argv[2], argv[3], kStaTimeoutMs);
        if (st.err == Err::Timeout) {
            ctx.io.writeLine("wifi sta: ERRO Timeout: nao associou. Confira SSID, senha e alcance.");
            ctx.io.writeLine("O radio ja voltou para WIFI_OFF.");
            return;
        }
        if (st.failed()) {
            ctx.io.printf("wifi sta: ERRO %s\r\n", errName(st.err));
            return;
        }
        printAccess(ctx, portal);
    }

    void doOff(Ctx& ctx, WifiPortal& portal) {
        const Status st = portal.stop();
        if (st.failed()) {
            ctx.io.printf("wifi off: ERRO %s\r\n", errName(st.err));
            return;
        }
        ctx.io.writeLine("Radio DESLIGADO (WIFI_OFF) e controle pela pagina de volta a BLOQUEADO.");
        ctx.io.writeLine("Medida analogica de precisao liberada.");
    }

    void doControl(Ctx& ctx, WifiPortal& portal, uint8_t argc, const char* const* argv) {
        bool on = false;
        if (argc < 3 || !cmd::parseOnOff(argv[2], on)) {
            ctx.io.writeLine(usage());
            return;
        }
        portal.setControlEnabled(on);
        if (on) {
            ctx.io.writeLine("Controle pela pagina LIBERADO: POST de rele, DAC, modo e estado seguro agem.");
            ctx.io.writeLine("Quem abrir a pagina aciona rele de verdade: use so com a placa na bancada.");
            return;
        }
        ctx.io.writeLine("Controle pela pagina BLOQUEADO: todo POST responde 403. Leitura continua livre.");
    }

    void doSave(Ctx& ctx, WifiPortal& portal) {
        const Status st = portal.saveConfig();
        if (st.err == Err::NotInit) {
            ctx.io.writeLine("ERRO: nada a salvar. Suba o radio com 'wifi ap' ou 'wifi sta' antes.");
            return;
        }
        if (st.failed()) {
            ctx.io.printf("wifi save: ERRO %s\r\n", errName(st.err));
            return;
        }
        ctx.io.printf("Gravado na NVS: SSID %s   modo %s (a senha vai em texto claro).\r\n",
                      portal.savedSsid(), modeText(portal.savedMode()));
    }

    void doForget(Ctx& ctx, WifiPortal& portal) {
        bool removed = false;
        removed = ctx.kv.remove(WifiPortal::kKeySsid).ok() || removed;
        removed = ctx.kv.remove(WifiPortal::kKeyPass).ok() || removed;
        removed = ctx.kv.remove(WifiPortal::kKeyMode).ok() || removed;
        portal.loadConfig();
        if (!removed) {
            ctx.io.writeLine("Nada a apagar: nao havia config de Wi-Fi na NVS.");
            return;
        }
        ctx.io.writeLine("Config de Wi-Fi apagada da NVS. O radio em uso continua como esta ate 'wifi off'.");
    }
};

}  // namespace

REGISTER_COMMAND(CmdWifi);
