// src/adapters/xtr300_analog_output.h
// Adaptador da porta IAnalogOutput: DAC8562 duplo de 16 bits no HSPI remapeado (folha 2/2,
// TI SLAS719E tabelas 8, 9 e 17) seguido de dois XTR300AIRGWT (folha 2/2, TI SBOS404), eixo
// X = canal A = CN1L(+)/CN1M(-), eixo Y = canal B = CN1N(+)/CN1O(-).
//
// Reaproveita, sem reescrever, os drivers de fabrica ja validados em bancada:
// src/drivers/spi_bus.cpp (SPI.begin com MISO real e nao -1), src/drivers/dac8562.cpp
// (quadro de 24 bits, softReset, powerUpBoth, refGain2, ignoreLdacPin) e
// src/drivers/xtr300.cpp da branch fix/saida-analogica-bipolar (zero bipolar em 0x8000).
// Os tres viram UM arquivo porque a porta e uma so e o adaptador nao pode publicar driver.
//
// Cadeia BIPOLAR, R_OS = 1K, R_GAIN = 10K, sem R_SET (SBOS336C eq. 2), fato fechado em
// DECISIONS.md L133: V_OUT = 5*(V_DAC - 2,5 V) = 25*D/65536 - 12,5 V. Zero eletrico em
// D = 0x8000 (32768), -10,00 V em 6554, +10,00 V em 58982. O codigo 0x0000 e PROIBIDO nesta
// placa (DECISIONS.md L1642 e L811): vale -12,5 V pedidos, satura em ~-12 V e aciona EFLD.
// Ele so aparece aqui como kPorCode, o valor de POR do DAC8562, usado como marca de "nada
// comandado ainda" - nenhum caminho deste arquivo o entrega ao barramento.
//
// GRAMPO DURO DO ADAPTADOR = [kCodeMin, kCodeMax] = [5243, 61342]. E o grampo ELETRICO da
// cadeia, nao a saturacao de +/-10,00 Vcc do manual 5.7 L185 - essa e REGRA DE NEGOCIO,
// aplicada ao ANGULO dentro de domain::AnalogScaler::codeFor(), e nao pode ser repetida aqui
// (analog_scaler.h: "Grampear o codigo nos valores nominais truncaria a calibracao de toda
// placa que precise de trim positivo de ganho"). Os dois numeros sao exatamente a faixa que o
// dominio pode emitir depois do gate de plausibilidade unico de A14:
//   - teto 61342 = domain::AnalogScaler::kCodeMax = CalibrationWizard::kCodeClampMax, o grampo
//     aprovado na decisao 6 item 7 (DECISIONS.md L1604/L1633) para as telas de medicao; a
//     medicao 14 (DECISIONS.md L1679) aceita trim de ganho de ate +4000 LSB, entao um teto em
//     58982 reprovaria placa boa no comissionamento;
//   - piso 5243 = domain::AnalogScaler::kCodeMin = kFaultCode + 1311 (0,500 V), o piso da
//     decisao 9 item 11: abaixo dele a faixa util encostaria no marcador de -11,00 V e a
//     distincao de A2 (sensora morta x estrutura saturada) morreria. AnalogScaler::make()
//     garante mirrorCode() >= 5243, e codeFor(-fundo de escala) DEVOLVE mirrorCode(): um piso
//     em 6554 truncaria a saturacao negativa de toda placa calibrada com trim de zero
//     positivo e devolveria Err::Range a cada 50 ms em regime.
// O codigo de falha 3932 ATRAVESSA o grampo intacto e vale escrita legitima (kOk): ele e o
// faultCode() declarado da propria porta, e o dominio o emite pelo caminho ORDINARIO de
// escrita - AnalogScaler::codeFor(Angle::invalid()) e CalibrationWizard::outputCode() em oito
// estados do assistente, inclusive os dois marcadores de -11,00 V que delimitam a janela de
// override (decisao 6 itens 6 e 7). Grampea-lo em 6554 transformaria "eixo em calibracao" e
// "sensora morta" em -10,00 V, que e leitura LEGITIMA de fundo de escala negativo.
//
// Armadilhas de hardware desta placa que este arquivo trata:
//  - SPI.begin(sck, -1, mosi, -1) NAO desliga o MISO no core do ESP32: ele so troca o default
//    do barramento (HSPI -> IO12 = SYNC do DAC) e faz pinMode INPUT. Por isso o MISO passado e
//    board::kDacMiso = 36, input-only e NC, e nao -1, e begin() RECUSA (Err::Param) um MISO em
//    kNoPin - a promessa nao pode depender de quem constroi o objeto. Verificado no core
//    instalado: framework-arduinoespressif32 2.0.17, esp32-hal-spi.c spiAttachMISO(), ramo
//    "miso < 0 && spi->num == HSPI -> miso = 12". O "MISO -1" entre parenteses no passo 5 de
//    DECISIONS.md L606 e detalhe de implementacao, nao a ordem de boot: o que o passo 5 fixa -
//    o que e escrito, em que ordem e quando a saida deixa o trilho negativo - e obedecido
//    integralmente. Quem chama begin() deve rearmar o WDI (IO19) depois, pela ordem de boot de
//    DECISIONS.md 2.2 passo 10.
//  - SYNC e IO12 (strapping MTDI): so vira saida DENTRO de begin(), nunca antes do boot.
//  - OP_MODE (IO22) NAO e strapping (board::kStrappingPins = {0,2,5,12,15}) e e dirigido na
//    PRIMEIRA linha util de begin(), antes de o DAC ser energizado: com o M2 dos dois XTR300
//    como entrada nao dirigida o modo tensao/corrente e indefinido, e modo corrente e o que A2
//    proibe (DECISIONS.md L63) numa placa sem R_SET.
//  - LDAC esta em nivel alto por R15 10K para +5 V, entao ignoreLdacPin() (SLAS719E tabela 17)
//    faz cada escrita de canal atualizar a saida na hora, sem pulso de LDAC.
//  - O DAC8562 faz POR em zero-scale com a referencia interna desligada: ate begin() rodar a
//    saida esta encostada no trilho negativo. begin() e o passo 5 do setup, orcamento 6 ms.
//
// O QUE begin() PODE E O QUE NAO PODE PROVAR. Ele prova: os pinos declarados existem, SYNC e
// OP_MODE foram dirigidos e os seis quadros de 24 bits foram emitidos. Ele NAO prova que o
// DAC8562 recebeu coisa alguma: o CI nao tem readback, nao ha ADC de conferencia e o VIH de
// 3,5 V do datasheet nao e atendido pelos 3,3 V do ESP32 (medicao pendente). kOk aqui
// significa "o barramento foi dirigido", nunca "a saida esta em -11,00 V" -
// readbackAvailable() e false por isso, e lastCode() e cache de escrita, nao leitura.
//
// BLOQUEIO DECLARADO (pior caso, chamador da base comum):
//  - begin(): 3,5 ms de espera fixa (1 ms de SYNC + 2 ms de softReset + 0,5 ms de acomodacao
//    do XTR300) mais 6 quadros de 24 bits. A 1 MHz da ~3,7 ms; a 100 kHz (kDacSpiMinHz) da
//    ~5,0 ms. Cabe no orcamento de 6 ms do passo 5 da ordem de boot. begin() NAO pode ser
//    chamado de dentro do tick de 50 ms da tarefa ctrl.
//  - write(): 1 quadro, 24 us a 1 MHz, 240 us a 100 kHz. writeBoth() e writeFaultLevel():
//    2 quadros, 48 us a 1 MHz, 480 us a 100 kHz. Nenhum delay(), nenhuma espera de resposta.
//    O pior caso de regime fica em 0,5 ms, 1 % do tick de 50 ms.
//  - Nada aqui roda em ISR nem sobrevive a janela de cache-off da NVS: e tudo codigo de flash
//    chamado da tarefa ctrl, e o chute do watchdog e por ISR em IRAM (passo 1), nao por aqui.
//
// Decisoes implementadas: A2 (nivel de falha -11,00 V = codigo cru 3932 e MODO CORRENTE
// PROIBIDO, OP_MODE fixo em tensao - DECISIONS.md L63, L173..L189, L1641), decisao 9 item 16 /
// L1642 (ponto unico do codigo de zero, 0x8000, e proibicao de 0x0000), ordem de boot passo 5
// (DECISIONS.md L606: configura o DAC e escreve o CODIGO DE FALHA 3932 nos dois canais - nao
// 0x0000, que vale -12,5 V, e nao 0x8000, que vale 0,00 V e e uma leitura legitima; a saida
// sai do trilho negativo direto para -11,00 V, que e kDacBootCode da base comum).
// A matematica angulo -> codigo, a calibracao de dois pontos e a saturacao de +/-10,00 V sao
// domain::AnalogScaler: aqui nao entra grau, nem float, nem regra de calibracao.
// REQ: MAN-2.1-L33/L35, MAN-5.2-L78, MAN-5.7-L155..180.
#pragma once

#include <stdint.h>

#include <SPI.h>

#include "board_pins.h"
#include "ports/i_analog_output.h"
#include "status.h"

class Xtr300AnalogOutput final : public IAnalogOutput {
public:
    // PENDENCIA DECLARADA: estes cinco numeros sao os de ur_base.h (DECISIONS.md 2.5), que
    // ainda nao existe na arvore. Quando nascer, viram urbase::kDacZeroCode / kDacFaultCode /
    // kDacMinUsefulCode / kDacMaxUsefulCode em UM commit - decisao 9 item 16 exige ponto unico.
    static constexpr uint16_t kZeroCode = 0x8000;      // 0,00 V; NUNCA escrito por begin()
    static constexpr uint16_t kFullScaleCode = 0xFFFF;
    static constexpr uint16_t kFaultCode = 3932;       // -11,00 V, atravessa o grampo
    static constexpr uint16_t kCodeMin = 5243;         // = domain::AnalogScaler::kCodeMin
    static constexpr uint16_t kCodeMax = 61342;        // = domain::AnalogScaler::kCodeMax
    // Valor de POR do DAC8562 (zero-scale, ref interna desligada). NAO e um codigo escrito:
    // e a marca de "nada foi comandado neste eixo ainda", devolvida por lastCode() antes de
    // begin(). Escrever 0x0000 e proibido nesta placa e nenhum caminho daqui o faz.
    static constexpr uint16_t kPorCode = 0x0000;

    Xtr300AnalogOutput();
    Xtr300AnalogOutput(board::Pin sclk, board::Pin mosi, board::Pin miso, board::Pin sync,
                       board::Pin opMode, uint32_t clockHz);

    Status begin() override;
    bool ready() const override { return ready_; }

    uint8_t axisCount() const override { return kAnalogAxisCount; }

    Status write(AnalogAxis axis, uint16_t code) override;
    Status writeBoth(uint16_t codeX, uint16_t codeY) override;
    Status writeFaultLevel() override;

    uint16_t fullScaleCode() const override { return kFullScaleCode; }
    uint16_t midScaleCode() const override { return kZeroCode; }
    uint16_t faultCode() const override { return kFaultCode; }
    uint16_t codeMin() const override { return kCodeMin; }
    uint16_t codeMax() const override { return kCodeMax; }

    uint16_t lastCode(AnalogAxis axis) const override;
    bool readbackAvailable() const override { return false; }

    // Declaracao de FIACAO, como a porta manda: true = jumpers J3/J4/J5/J6/J13/J14 na posicao
    // "uC", o net OP_MODE chega ao M2 dos dois XTR300 e o nivel BAIXO escrito em begin() e o
    // que fixa o modo tensao. E a configuracao pressuposta pelo passo 5 da ordem de boot
    // (DECISIONS.md L606) e pela decisao 9 item 15 (L1641). Pendente da medicao 13: se a placa
    // sair com os jumpers na posicao FIXA, este valor tem de virar false E o cabecalho tem de
    // passar a dizer que a garantia de modo tensao e do jumper, nao do firmware.
    // A recusa de AoMode::Current em setMode() e coisa OUTRA: e a guarda de A2 / decisao 9
    // item 15 ("a aplicacao nunca chama setMode()"), regra de aplicacao e nao fato da fiacao.
    Status setMode(AoMode desired) override;
    AoMode mode() const override { return mode_; }
    bool modeSelectable() const override { return true; }

    uint32_t clockHz() const { return clockHz_; }

private:
    static uint16_t clampUseful(uint16_t code);

    Status writeFrame(uint8_t cmd, uint16_t data);
    Status writeChannelRaw(uint8_t ch, uint16_t code);

    SPIClass spi_;
    board::Pin sclk_;
    board::Pin mosi_;
    board::Pin miso_;
    board::Pin sync_;
    board::Pin opMode_;
    uint32_t clockHz_;
    uint16_t lastCode_[kAnalogAxisCount];
    AoMode mode_;
    bool ready_;
};
