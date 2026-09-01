// Console de diagnostico da sensora PUSI-DI261930 em UART0 a 115200: janela de bancada, sem operador.
// Nao conduz teste e nao bloqueia: o trabalho da placa e responder ao RS-485.
#pragma once

#include <stdint.h>

#include "core/sensor_ctx.h"

class SensorConsole {
public:
    explicit SensorConsole(SensorCtx& ctx);

    void begin();
    void poll();

private:
    static constexpr uint16_t kLineBytes = 64;

    void handleLine(char* line);
    void prompt();
    void printBanner();
    void printPinout();
    void printHelp();
    void cmdAngle();
    void cmdRaw();
    void cmdStatus();
    void printSto(const InclinometerDiag& diag);
    void cmdWhoAmI();
    void cmdSelfTest();
    void cmdBypass(const char* arg);
    void cmdReinit();
    void cmdLink();
    void cmdProto(const char* arg);
    void cmdWdt();
    void cmdVer();
    void cmdProbe(const char* arg);
    void cmdTrace();
    void cmdSpiRaw(const char* arg);
    void cmdSpiLoop(const char* arg);
    void showProtocol();

    SensorCtx& ctx_;
    char line_[kLineBytes];
    uint16_t len_;
    bool overflow_;
    bool lastCr_;
};
