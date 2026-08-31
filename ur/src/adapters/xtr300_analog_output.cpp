// src/adapters/xtr300_analog_output.cpp
// Quadro de 24 bits do DAC8562 (comando + endereco + 16 bits de dado), dado amostrado na borda
// de descida do SCLK com SYNC baixo do primeiro ao ultimo bit: folha 2/2, TI SLAS719E tabelas
// 8, 9 e 17, SPI_MODE1, MSB primeiro. Copiado de src/drivers/dac8562.cpp e src/drivers/
// xtr300.cpp (branch fix/saida-analogica-bipolar), que rodaram em bancada no firmware de teste
// de fabrica; o que mudou foi so o que a porta exige: grampo duro da faixa eletrica, Err::Range
// com escrita do valor grampeado, escrita explicita do nivel de falha e recusa do modo corrente.
//
// Ordem de begin() = passo 5 da ordem de boot canonica (DECISIONS.md 2.2 L606):
//   OP_MODE (IO22) como saida em nivel BAIXO = modo tensao, A2 / decisao 9 item 15 -> HSPI
//   begin -> SYNC alto -> settle 1 ms -> softReset -> settle 2 ms -> powerUpBoth -> refGain2 ->
//   ignoreLdacPin -> CODIGO DE FALHA 3932 nos dois canais -> acomodacao do XTR300 (Cc 47 nF
//   sobre R_OS 1K, tau 47 us, 0,1 % em 0,25 ms; kXtrSettleUs = 500 us).
// O M2 e dirigido ANTES de tudo de proposito: o passo 5 diz quando a SAIDA sai do trilho
// negativo, nao manda deixar o M2 flutuando enquanto o DAC e energizado. Dirigir o pino
// primeiro fecha a janela de modo indefinido (modo corrente, sem R_SET, e caminho para
// EFLD/EFCM) e nao muda nenhum instante observavel do passo 5. IO22 nao e strapping.
// O PRIMEIRO codigo escrito e 3932, nao 0x8000: DECISIONS.md L606 e a decisao 7 fecham que "a
// saida sai do trilho negativo direto para o nivel de falha, SEM PASSAR POR 0,00 V, que seria
// uma mentira momentanea de estrutura nivelada" - 0,00 V e a leitura legitima mais provavel de
// estrutura nivelada E a assinatura fisica da UR sem energia. 3932 e kDacBootCode da base
// comum. 0x8000 continua existindo aqui so como midScaleCode(), e begin() nunca o escreve.
//
// writeBoth() escreve os dois canais na mesma passagem, dois quadros consecutivos de 24 bits;
// com LDAC ignorado por registro nao ha atualizacao simultanea por hardware, e o desalinhamento
// entre eixos e o tempo de um quadro (24 bits a 1 MHz = 24 us), tres ordens de grandeza abaixo
// do poll de 50 ms da tarefa ctrl. A porta pede "mesma passagem", nao simultaneidade eletrica.
#include "adapters/xtr300_analog_output.h"

#include <Arduino.h>

namespace {

constexpr uint8_t kCmdWriteUpdateA = 0x18;
constexpr uint8_t kCmdWriteUpdateB = 0x19;
constexpr uint8_t kCmdPowerUp = 0x20;
constexpr uint8_t kCmdSoftReset = 0x28;
constexpr uint8_t kCmdLdacRegister = 0x30;
constexpr uint8_t kCmdRefGain = 0x38;

constexpr uint16_t kDataResetAll = 0x0001;
constexpr uint16_t kDataPowerUpBoth = 0x0003;
constexpr uint16_t kDataRefIntGain2 = 0x0001;
constexpr uint16_t kDataLdacIgnorePin = 0x0003;

constexpr uint8_t kFrameBytes = 3;

constexpr uint32_t kSyncSettleMs = 1;
constexpr uint32_t kResetSettleMs = 2;
constexpr uint32_t kXtrSettleUs = 500;

}  // namespace

static_assert(kAnalogAxisCount == board::kAxisCount, "um eixo por canal do DAC8562");
static_assert(static_cast<uint8_t>(AnalogAxis::X) == 0, "eixo X e o canal A do DAC8562");
static_assert(static_cast<uint8_t>(AnalogAxis::Y) == 1, "eixo Y e o canal B do DAC8562");
static_assert(Xtr300AnalogOutput::kFaultCode < Xtr300AnalogOutput::kCodeMin,
              "o nivel de falha tem de cair fora da faixa util");
static_assert(Xtr300AnalogOutput::kCodeMin - Xtr300AnalogOutput::kFaultCode >= 1311,
              "0,500 V de separacao entre a faixa util e o marcador de -11,00 V (decisao 9 "
              "item 11): abaixo disso a distincao de A2 morre");
static_assert(Xtr300AnalogOutput::kCodeMin < Xtr300AnalogOutput::kZeroCode &&
                  Xtr300AnalogOutput::kZeroCode < Xtr300AnalogOutput::kCodeMax,
              "o zero eletrico tem de caber na faixa util");
static_assert(Xtr300AnalogOutput::kPorCode < Xtr300AnalogOutput::kFaultCode,
              "o valor de POR nao pode ser confundido com codigo comandado nem com a falha");

Xtr300AnalogOutput::Xtr300AnalogOutput()
    : Xtr300AnalogOutput(board::kDacSclk, board::kDacMosi, board::kDacMiso, board::kDacSync,
                         board::kXtrOpMode, board::kDacSpiDefaultHz) {}

Xtr300AnalogOutput::Xtr300AnalogOutput(board::Pin sclk, board::Pin mosi, board::Pin miso,
                                       board::Pin sync, board::Pin opMode, uint32_t clockHz)
    : spi_(HSPI),
      sclk_(sclk),
      mosi_(mosi),
      miso_(miso),
      sync_(sync),
      opMode_(opMode),
      clockHz_(clockHz),
      lastCode_(),
      mode_(AoMode::Voltage),
      ready_(false) {
    if (clockHz_ < board::kDacSpiMinHz) {
        clockHz_ = board::kDacSpiMinHz;
    } else if (clockHz_ > board::kDacSpiMaxHz) {
        clockHz_ = board::kDacSpiMaxHz;
    }
    // Estado de POR do DAC8562, NAO um codigo comandado: antes de begin() nada foi escrito e a
    // saida real esta encostada no trilho negativo (~-12,5 V), como o cabecalho da porta
    // declara. Semear o codigo de falha aqui faria lastCode() anunciar -11,00 V a um
    // diagnostico que rodasse antes do passo 5 - um nivel que o CLP nunca viu.
    for (uint8_t ch = 0; ch < kAnalogAxisCount; ++ch) {
        lastCode_[ch] = kPorCode;
    }
}

// Grampo ELETRICO da cadeia. O codigo de falha atravessa intacto: ele e o faultCode()
// declarado da porta e chega por write() no caminho ORDINARIO (angulo invalido e os oito
// estados do assistente que devolvem kFaultCode), nao por writeFaultLevel() - que e por-placa
// e nao serve ao override de UM eixo da decisao 6. A saturacao de +/-10,00 Vcc do manual 5.7
// L185 NAO esta aqui: e do angulo, dentro de domain::AnalogScaler::codeFor().
uint16_t Xtr300AnalogOutput::clampUseful(uint16_t code) {
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

// Sem readback no DAC8562: emitir o quadro e tudo o que este nivel consegue afirmar.
Status Xtr300AnalogOutput::writeFrame(uint8_t cmd, uint16_t data) {
    uint8_t buf[kFrameBytes];
    buf[0] = cmd;
    buf[1] = static_cast<uint8_t>((data >> 8) & 0xFF);
    buf[2] = static_cast<uint8_t>(data & 0xFF);
    spi_.beginTransaction(SPISettings(clockHz_, MSBFIRST, SPI_MODE1));
    digitalWrite(static_cast<uint8_t>(sync_), LOW);
    for (uint8_t i = 0; i < kFrameBytes; ++i) {
        spi_.transfer(buf[i]);
    }
    digitalWrite(static_cast<uint8_t>(sync_), HIGH);
    spi_.endTransaction();
    return kOk;
}

Status Xtr300AnalogOutput::writeChannelRaw(uint8_t ch, uint16_t code) {
    const Status st = writeFrame(ch == 0 ? kCmdWriteUpdateA : kCmdWriteUpdateB, code);
    if (st.ok()) {
        lastCode_[ch] = code;
    }
    return st;
}

Status Xtr300AnalogOutput::begin() {
    ready_ = false;
    // miso_ entra na validacao junto com os outros: passar kNoPin (-1) aqui NAO desliga o MISO.
    // Verificado no core instalado (framework-arduinoespressif32 2.0.17, esp32-hal-spi.c,
    // spiAttachMISO): miso < 0 no HSPI vira IO12 - que nesta placa e o SYNC do DAC8562 - e
    // sofre pinMode(INPUT) + pinMatrixInAttach. Recusar e o unico jeito de o codigo honrar o
    // que o cabecalho promete (MISO = board::kDacMiso = 36, input-only e NC, nunca -1).
    if (sclk_ == board::kNoPin || mosi_ == board::kNoPin || miso_ == board::kNoPin ||
        sync_ == board::kNoPin || opMode_ == board::kNoPin) {
        return Status(Err::Param);
    }

    // Primeiro o M2: enquanto OP_MODE for entrada nao dirigida o modo dos dois XTR300 e
    // indefinido, e ligar o DAC nessa janela e o caminho para o modo corrente que A2 proibe.
    pinMode(static_cast<uint8_t>(opMode_), OUTPUT);
    digitalWrite(static_cast<uint8_t>(opMode_), LOW);
    mode_ = AoMode::Voltage;

    spi_.begin(sclk_, miso_, mosi_, board::kNoPin);

    pinMode(static_cast<uint8_t>(sync_), OUTPUT);
    digitalWrite(static_cast<uint8_t>(sync_), HIGH);
    delay(kSyncSettleMs);

    Status st = writeFrame(kCmdSoftReset, kDataResetAll);
    if (st.failed()) {
        return st;
    }
    delay(kResetSettleMs);
    st = writeFrame(kCmdRefGain, kDataRefIntGain2);
    if (st.failed()) {
        return st;
    }
    st = writeFrame(kCmdLdacRegister, kDataLdacIgnorePin);
    if (st.failed()) {
        return st;
    }
    // Estado de boot = kDacBootCode = codigo de falha. A saida sai do trilho negativo direto
    // para -11,00 V, sem passar por 0,00 V (DECISIONS.md L606 e decisao 7 item (b)).
    //
    // O DADO VEM ANTES DO POWER-UP, E ESSA ORDEM E O REQUISITO. O soft reset restaura o POR do
    // DAC8562, que e ZERO-SCALE (kPorCode = 0x0000). Energizar os dois canais com os
    // registradores de dado ainda em 0x0000 DIRIGE a saida em 0x0000 durante os quadros
    // seguintes - ~72 us a 1 MHz, ~720 us em kDacSpiMinHz = 100 kHz - e 0x0000 vale -12,5 V,
    // satura o XTR300 em ~-12 V, abre a malha da IA e aciona o EFLD; com Cc = 47 nF sobre
    // R_OS = 1K a constante de tempo e 47 us, entao em 720 us a saida chega la de fato.
    // DECISIONS.md 2.5 e categorico: 0x0000 e PROIBIDO em qualquer caminho desta placa. Os
    // comandos 0x18/0x19 (write and update) atualizam o registrador de dado mesmo com o canal
    // em power-down, entao escrever 3932 primeiro faz a saida NASCER no codigo de falha quando
    // o power-up chegar. O orcamento de 6 ms do passo 5 nao muda: e a mesma quantidade de
    // quadros, em outra ordem.
    for (uint8_t ch = 0; ch < kAnalogAxisCount; ++ch) {
        st = writeChannelRaw(ch, kFaultCode);
        if (st.failed()) {
            return st;
        }
    }
    st = writeFrame(kCmdPowerUp, kDataPowerUpBoth);
    if (st.failed()) {
        return st;
    }

    delayMicroseconds(kXtrSettleUs);
    ready_ = true;
    return kOk;
}

Status Xtr300AnalogOutput::write(AnalogAxis axis, uint16_t code) {
    const uint8_t ch = static_cast<uint8_t>(axis);
    if (ch >= kAnalogAxisCount) {
        return Status(Err::Param);
    }
    if (!ready_) {
        return Status(Err::NotInit);
    }
    const uint16_t clamped = clampUseful(code);
    const Status st = writeChannelRaw(ch, clamped);
    if (st.failed()) {
        return st;
    }
    // clamped == code tambem no codigo de falha, que passa intacto: escrita legitima, kOk.
    return clamped == code ? kOk : Status(Err::Range);
}

Status Xtr300AnalogOutput::writeBoth(uint16_t codeX, uint16_t codeY) {
    if (!ready_) {
        return Status(Err::NotInit);
    }
    const uint16_t clampedX = clampUseful(codeX);
    const uint16_t clampedY = clampUseful(codeY);
    const Status stX = writeChannelRaw(static_cast<uint8_t>(AnalogAxis::X), clampedX);
    const Status stY = writeChannelRaw(static_cast<uint8_t>(AnalogAxis::Y), clampedY);
    if (stX.failed()) {
        return stX;
    }
    if (stY.failed()) {
        return stY;
    }
    return (clampedX == codeX && clampedY == codeY) ? kOk : Status(Err::Range);
}

Status Xtr300AnalogOutput::writeFaultLevel() {
    if (!ready_) {
        return Status(Err::NotInit);
    }
    Status first = kOk;
    for (uint8_t ch = 0; ch < kAnalogAxisCount; ++ch) {
        const Status st = writeChannelRaw(ch, kFaultCode);
        if (st.failed() && first.ok()) {
            first = st;
        }
    }
    return first;
}

uint16_t Xtr300AnalogOutput::lastCode(AnalogAxis axis) const {
    const uint8_t ch = static_cast<uint8_t>(axis);
    if (ch >= kAnalogAxisCount) {
        // Eixo que nao existe nunca foi comandado: devolve a marca de POR, nao um nivel.
        return kPorCode;
    }
    return lastCode_[ch];
}

Status Xtr300AnalogOutput::setMode(AoMode desired) {
    if (!ready_) {
        return Status(Err::NotInit);
    }
    // Guarda de A2 / decisao 9 item 15 (o produto so vende tensao), nao propriedade dos
    // jumpers - a fiacao esta declarada em modeSelectable().
    if (desired != AoMode::Voltage) {
        return Status(Err::Unsupported);
    }
    digitalWrite(static_cast<uint8_t>(opMode_), LOW);
    mode_ = AoMode::Voltage;
    return kOk;
}
