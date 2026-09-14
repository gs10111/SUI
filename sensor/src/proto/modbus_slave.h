// PUSI-DI261930: escravo Modbus RTU no RS-485 - PROTOCOLO RECOMENDADO PARA PRODUCAO nesta placa.
// Angulos X/Y/Z como int16 em decimos de grau nos registradores 0/1/2 (ver sensor/include/sensor_map.h).
#pragma once

#include <stdint.h>

#include "board_pins.h"
#include "iface/islave_protocol.h"

class ModbusRtuSlave : public ISlaveProtocol {
public:
    static constexpr uint8_t kBroadcastAddr = 0;
    static constexpr uint8_t kFuncReadHolding = 0x03;
    static constexpr uint8_t kFuncReadInput = 0x04;
    // A UNICA funcao de ESCRITA deste escravo, acrescentada em 2026-09-14 para que a supervisora
    // possa ligar o radio desta placa a partir do painel. Ver sensor_map.h kRegCmdOta: um unico
    // endereco, dois valores possiveis, um unico efeito - ligar ou desligar o WiFi. Nenhuma
    // escrita neste enlace altera medicao, limite, calibracao ou saida.
    static constexpr uint8_t kFuncWriteSingle = 0x06;
    static constexpr uint8_t kExceptionMask = 0x80;
    static constexpr uint8_t kExIllegalFunction = 0x01;
    static constexpr uint8_t kExIllegalDataAddress = 0x02;
    static constexpr uint8_t kExIllegalDataValue = 0x03;
    static constexpr uint16_t kMaxReadCount = 125;
    static constexpr uint16_t kMinRequestLen = 4;
    static constexpr uint16_t kReadRequestLen = 8;
    static constexpr uint16_t kExceptionLen = 5;
    static constexpr uint16_t kReadOverhead = 5;

    explicit ModbusRtuSlave(uint8_t id = board::kModbusSlaveId);

    const char* name() const override;
    void reset() override;
    uint16_t handle(const uint8_t* request, uint16_t len, const uint16_t* registers,
                    uint16_t registerCount, uint8_t* response, uint16_t cap) override;
    uint32_t requests() const override;
    uint32_t responses() const override;
    uint32_t badFrames() const override;

    // Colhe um comando de OTA recebido, UMA vez. Devolve false quando nao chegou nada novo.
    //
    // O protocolo nao executa nada: ele so entende o quadro e deixa o comando aqui. Quem liga
    // radio e o main, que e quem tem WiFi - um escravo Modbus que ligasse radio sozinho seria um
    // caminho de atuacao escondido dentro de um decodificador de quadro.
    bool takeOtaCommand(uint16_t& valor);
    uint32_t otaCommands() const { return otaCommands_; }

    void setSlaveId(uint8_t id);
    uint8_t slaveId() const { return slaveId_; }
    uint32_t exceptions() const { return exceptions_; }

private:
    uint16_t readRegisters(uint8_t func, const uint8_t* request, const uint16_t* registers,
                           uint16_t registerCount, uint8_t* response, uint16_t cap);
    uint16_t buildException(uint8_t func, uint8_t code, uint8_t* response, uint16_t cap);
    uint16_t writeSingle(const uint8_t* request, bool broadcast, uint8_t* response, uint16_t cap);

    uint32_t requests_;
    uint32_t responses_;
    uint32_t badFrames_;
    uint32_t exceptions_;
    uint32_t otaCommands_;
    uint16_t otaPendente_;
    bool otaPendenteValido_;
    uint8_t slaveId_;
};
