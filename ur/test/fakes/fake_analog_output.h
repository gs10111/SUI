// Saida analogica de teste: registra o CODIGO escrito em cada eixo, na ordem, para que o
// teste possa afirmar "o CLP viu -11,00 V aqui" sem voltimetro.
//
// O fake so vale se for SUBSTITUIVEL por Xtr300AnalogOutput (src/adapters/): mesmas
// pre-condicoes, mesma semantica de erro, mesmos limites. Um fake mais permissivo que o alvo
// faz a suite mentir - o dominio passa no host e a placa manda -10,00 V onde devia mandar
// -11,00 V. Os pontos em que este fake se recusa a ser generoso, todos copiados do alvo:
//
// 1. GRAMPO ELETRICO em [kCodeMin, kCodeMax] = [5243, 61342], que e exatamente a faixa que
//    domain::AnalogScaler::make() pode produzir (mirrorCode() >= 5243, fullScaleCode() <=
//    61342). NAO e a saturacao de +/-10,00 Vcc do manual 5.7 L185: essa e do angulo, dentro
//    de AnalogScaler::codeFor(). Um fake que grampeasse em 6554..58982 reprovaria a
//    calibracao de placa com trim positivo de ganho, que e legitima.
// 2. O CODIGO DE FALHA 3932 ATRAVESSA o grampo e vale kOk. Ele chega pelo caminho ORDINARIO
//    de escrita (AnalogScaler::codeFor(Angle::invalid()) e CalibrationWizard::outputCode()
//    nos oito estados fora das telas de medicao, inclusive os dois marcadores da janela de
//    override da decisao 6). Grampea-lo viraria -10,00 V, leitura LEGITIMA de fundo de
//    escala, e mataria a distincao de A2 entre "sensora morta" e "estrutura saturada".
// 3. Escrita fora da faixa devolve Err::Range E GRAVA O VALOR GRAMPEADO. Em equipamento de
//    seguranca a saida nunca fica no valor errado por causa de um erro de faixa a montante.
// 4. Antes de begin(), write()/writeBoth()/writeFaultLevel()/setMode() devolvem Err::NotInit
//    e lastCode() devolve kPorCode (0x0000, o zero-scale do POR do DAC8562) - NAO o codigo de
//    falha: ate begin() rodar nada foi comandado e a saida real esta no trilho negativo.
// 5. begin() escreve o CODIGO DE FALHA nos dois canais, nunca 0x8000. A saida sai do trilho
//    negativo direto para -11,00 V, sem passar por 0,00 V (DECISIONS.md L606, decisao 7).
// 6. readbackAvailable() e false: o DAC8562 nao tem readback e nao ha ADC de conferencia.
//    lastCode() e cache de ESCRITA. Nenhuma decisao de rele pode depender desta porta.
// 7. setMode(AoMode::Current) devolve Err::Unsupported (guarda de A2 / decisao 9 item 15),
//    e modeSelectable() e true (jumpers na posicao "uC", como o passo 5 do boot pressupoe).
#pragma once

#include <stdint.h>

#include "ports/i_analog_output.h"

namespace test {

class FakeAnalogOutput : public IAnalogOutput {
public:
    static constexpr uint16_t kZeroCode = 0x8000;
    static constexpr uint16_t kFullScaleCode = 0xFFFF;
    static constexpr uint16_t kFaultCode = 3932;
    static constexpr uint16_t kCodeMin = 5243;
    static constexpr uint16_t kCodeMax = 61342;
    static constexpr uint16_t kPorCode = 0x0000;

    static constexpr uint8_t kLogCap = 32;

    struct Write {
        AnalogAxis axis;
        uint16_t code;
    };

    FakeAnalogOutput()
        : log_(), logCount_(0), logDropped_(0), writes_(0), begins_(0), lastCode_(),
          injected_(Err::Ok), mode_(AoMode::Voltage), ready_(false) {
        for (uint8_t ch = 0; ch < kAnalogAxisCount; ++ch) {
            lastCode_[ch] = kPorCode;
        }
    }

    Status begin() override {
        ++begins_;
        ready_ = false;
        if (injected_ != Err::Ok) {
            return takeInjected();
        }
        ready_ = true;
        mode_ = AoMode::Voltage;
        for (uint8_t ch = 0; ch < kAnalogAxisCount; ++ch) {
            record(static_cast<AnalogAxis>(ch), kFaultCode);
        }
        return kOk;
    }

    bool ready() const override { return ready_; }

    uint8_t axisCount() const override { return kAnalogAxisCount; }

    Status write(AnalogAxis axis, uint16_t code) override {
        const uint8_t ch = static_cast<uint8_t>(axis);
        if (ch >= kAnalogAxisCount) {
            return Status(Err::Param);
        }
        if (!ready_) {
            return Status(Err::NotInit);
        }
        if (injected_ != Err::Ok) {
            return takeInjected();
        }
        const uint16_t clamped = clampUseful(code);
        record(axis, clamped);
        return clamped == code ? kOk : Status(Err::Range);
    }

    Status writeBoth(uint16_t codeX, uint16_t codeY) override {
        if (!ready_) {
            return Status(Err::NotInit);
        }
        if (injected_ != Err::Ok) {
            return takeInjected();
        }
        const uint16_t clampedX = clampUseful(codeX);
        const uint16_t clampedY = clampUseful(codeY);
        record(AnalogAxis::X, clampedX);
        record(AnalogAxis::Y, clampedY);
        return (clampedX == codeX && clampedY == codeY) ? kOk : Status(Err::Range);
    }

    Status writeFaultLevel() override {
        if (!ready_) {
            return Status(Err::NotInit);
        }
        if (injected_ != Err::Ok) {
            return takeInjected();
        }
        for (uint8_t ch = 0; ch < kAnalogAxisCount; ++ch) {
            record(static_cast<AnalogAxis>(ch), kFaultCode);
        }
        return kOk;
    }

    uint16_t fullScaleCode() const override { return kFullScaleCode; }
    uint16_t midScaleCode() const override { return kZeroCode; }
    uint16_t faultCode() const override { return kFaultCode; }
    uint16_t codeMin() const override { return kCodeMin; }
    uint16_t codeMax() const override { return kCodeMax; }

    uint16_t lastCode(AnalogAxis axis) const override {
        const uint8_t ch = static_cast<uint8_t>(axis);
        return ch < kAnalogAxisCount ? lastCode_[ch] : kPorCode;
    }

    bool readbackAvailable() const override { return false; }

    Status setMode(AoMode desired) override {
        if (!ready_) {
            return Status(Err::NotInit);
        }
        if (desired != AoMode::Voltage) {
            return Status(Err::Unsupported);
        }
        mode_ = AoMode::Voltage;
        return kOk;
    }

    AoMode mode() const override { return mode_; }
    bool modeSelectable() const override { return true; }

    // --- instrumentacao de teste (nao faz parte da porta) ---

    uint32_t writeCount() const { return writes_; }
    uint32_t beginCount() const { return begins_; }
    uint8_t logCount() const { return logCount_; }
    uint32_t logDropped() const { return logDropped_; }

    bool logAt(uint8_t index, Write& out) const {
        if (index >= logCount_) {
            return false;
        }
        out = log_[index];
        return true;
    }

    // Algum eixo ja recebeu este codigo? Serve para provar o que NAO foi escrito - o caso do
    // 0x8000 na energizacao, que seria a mentira "estrutura nivelada".
    bool everWrote(uint16_t code) const {
        for (uint8_t i = 0; i < logCount_; ++i) {
            if (log_[i].code == code) {
                return true;
            }
        }
        return false;
    }

    // Falha de barramento a ser devolvida pela PROXIMA chamada que fale com o CI. Existe para
    // que o dominio prove o que faz quando a saida analogica nao responde.
    void failNext(Err err) { injected_ = err; }

    void clearLog() {
        logCount_ = 0;
        logDropped_ = 0;
    }

private:
    static uint16_t clampUseful(uint16_t code) {
        if (code == kFaultCode) {
            return code;
        }
        if (code < kCodeMin) {
            return kCodeMin;
        }
        if (code > kCodeMax) {
            return kCodeMax;
        }
        return code;
    }

    Status takeInjected() {
        const Err err = injected_;
        injected_ = Err::Ok;
        return Status(err);
    }

    void record(AnalogAxis axis, uint16_t code) {
        ++writes_;
        lastCode_[static_cast<uint8_t>(axis)] = code;
        if (logCount_ < kLogCap) {
            log_[logCount_].axis = axis;
            log_[logCount_].code = code;
            ++logCount_;
        } else {
            ++logDropped_;
        }
    }

    Write log_[kLogCap];
    uint8_t logCount_;
    uint32_t logDropped_;
    uint32_t writes_;
    uint32_t begins_;
    uint16_t lastCode_[kAnalogAxisCount];
    Err injected_;
    AoMode mode_;
    bool ready_;
};

}  // namespace test
