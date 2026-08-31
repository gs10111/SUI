// src/ports/i_analog_output.h
// Duas saidas analogicas: DAC8562 (16 bits, ref interna 2,5 V, ganho 2) seguido de
// XTR300 com offset por VREF (R_OS = 1K no pino SET, R_GAIN = 10K, sem R_SET).
// A cadeia e BIPOLAR: V_OUT = 25*D/65536 - 12,5 V. O zero eletrico e o codigo
// 0x8000, NAO 0x0000 - 0x0000 vale -12,5 V, isto e, fundo de escala negativo.
// A porta so fala em CODIGO DE DAC (uint16). A conversao angulo -> codigo, a
// calibracao de dois pontos e a saturacao em +/-10,00 V sao dominio puro
// (AnalogScaler, aritmetica inteira), nao adaptador: e por isso que aqui nao ha
// nenhum setEngineering(float) e nenhum parametro em graus.
// Alvo: Xtr300AnalogOutput (src/drivers/xtr300.cpp) sobre Dac8562 + SpiBus.
// Fake: FakeAnalogOutput (test/native) - registra os codigos escritos por eixo e
//       reprova qualquer escrita fora de [codeMin(), codeMax()].
// REQ:  MAN-2.1-L33/L35 (bipolar -10..+10 V, D/A de 16 bits, duas saidas),
//       MAN-5.2-L78 (saidas atualizadas no Modo Normal),
//       MAN-5.7-L155..180 (Auto Calibracao em dois pontos e saturacao),
//       decisao 6 (override so no eixo em calibracao), decisao 9, decisao 10,
//       decisao 7 item 7 / decisao 8 item H (nivel eletrico de falha).
#pragma once

#include <stdint.h>

#include "status.h"

enum class AnalogAxis : uint8_t {
    X = 0,  // bornes CN1L(+)/CN1M(-)
    Y,      // bornes CN1N(+)/CN1O(-)
};

constexpr uint8_t kAnalogAxisCount = 2;

enum class AoMode : uint8_t {
    Voltage = 0,  // M2 = L
    Current,      // M2 = H
};

class IAnalogOutput {
public:
    virtual ~IAnalogOutput() = default;
    IAnalogOutput(const IAnalogOutput&) = delete;
    IAnalogOutput& operator=(const IAnalogOutput&) = delete;

    // Configura o CI e escreve um codigo definido nos dois canais ANTES de
    // qualquer outro periferico. O DAC8562 faz POR em zero-scale com a referencia
    // interna desligada: ate begin() rodar, as saidas estao encostadas no trilho
    // negativo. begin() e o primeiro passo do setup, nao o ultimo.
    virtual Status begin() = 0;
    virtual bool ready() const = 0;

    virtual uint8_t axisCount() const = 0;

    // --- escrita, so em codigo de DAC ---
    virtual Status write(AnalogAxis axis, uint16_t code) = 0;

    // Escreve os dois eixos na mesma passagem. Via normal do ciclo de controle:
    // evita apresentar ao CLP um eixo do ciclo novo e outro do ciclo anterior.
    virtual Status writeBoth(uint16_t codeX, uint16_t codeY) = 0;

    // Leva AMBOS os eixos ao nivel eletrico de falha (fora da faixa de medicao,
    // dentro do swing do XTR300), para que o CLP a jusante distinga falha de
    // inclinacao maxima. Usado na perda de link, na faixa mecanica excedida e na
    // janela de energizacao.
    virtual Status writeFaultLevel() = 0;

    // --- constantes eletricas do canal, para o dominio calcular sem adivinhar ---
    virtual uint16_t fullScaleCode() const = 0;  // 0xFFFF
    virtual uint16_t midScaleCode() const = 0;   // 0x8000 = 0,000 V nominal
    virtual uint16_t faultCode() const = 0;      // codigo do nivel de falha
    virtual uint16_t codeMin() const = 0;        // grampo duro inferior
    virtual uint16_t codeMax() const = 0;        // grampo duro superior
    // Todo codigo e grampeado em [codeMin(), codeMax()] pelo adaptador. Escrita
    // fora da faixa devolve Err::Range E grava o valor grampeado: em equipamento
    // de seguranca a saida nunca fica no valor errado por causa de um erro de
    // faixa a montante.

    // Estado COMANDADO. Nao ha leitura de volta: o DAC8562 nao tem readback e
    // nao existe ADC de conferencia (readbackAvailable() == false).
    virtual uint16_t lastCode(AnalogAxis axis) const = 0;
    virtual bool readbackAvailable() const = 0;

    // --- modo tensao/corrente ---
    // Existe UM unico net OP_MODE para os DOIS XTR300: o modo e global, nunca por
    // eixo, e a assinatura reflete isso. Alem disso o net so chega ao M2 se os
    // jumpers estiverem na posicao "uC"; na posicao fixa setMode() nao tem efeito
    // e nao ha como o firmware perceber - por isso modeSelectable() e uma
    // declaracao de configuracao, nao uma medida.
    virtual Status setMode(AoMode mode) = 0;
    virtual AoMode mode() const = 0;
    virtual bool modeSelectable() const = 0;

protected:
    IAnalogOutput() = default;
};
