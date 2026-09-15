// Contexto da sensora PUSI-DI261930 montado em main.cpp: agregado de interfaces estreitas.
// A saida do console e uma porta propria para que nenhum modulo dependa de Serial.
#pragma once

#include <stdint.h>

#include "iface/iinclinometer.h"
#include "iface/islave_protocol.h"
#include "iface/iserial_transport.h"
#include "iface/iwatchdog.h"

class ISensorIO {
public:
    virtual ~ISensorIO() = default;
    virtual void write(const char* text) = 0;
    virtual void writeLine(const char* text) = 0;
    virtual void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) = 0;
    virtual bool readByte(uint8_t& out) = 0;
    virtual uint32_t nowMs() const = 0;
};

struct SensorCtx {
    ISensorIO& io;
    IInclinometer& tilt;
    ISerialTransport& link;
    IWatchdog& wdt;
    ISlaveProtocol** protocol;
    ISlaveProtocol* jigProtocol;
    ISlaveProtocol* modbusProtocol;
    const uint16_t* registers;
    uint16_t registerCount;
    const char* fwVersion;
    const char* boardRev;

    // GANCHOS DE ATUALIZACAO, NO FIM DA ESTRUTURA E DE PROPOSITO: este agregado e inicializado
    // por POSICAO em main.cpp, entao qualquer campo novo no meio deslocaria todos os seguintes em
    // silencio - g_io iria parar no lugar do gancho. Ficam no fim, sem entrar na lista, e sao
    // atribuidos depois.
    //
    // O console nao liga radio sozinho: ele chama de volta o main, que e quem tem WiFi. Sao
    // ponteiros e nao chamada direta porque o console compila no host, onde WiFi nao existe - e
    // porque um console que falasse com o radio direto seria um segundo dono do ponto de acesso,
    // ao lado do comando que chega pelo RS-485.
    void (*otaLigar)() = nullptr;
    void (*otaDesligar)() = nullptr;
    const char* (*otaEndereco)() = nullptr;
};
