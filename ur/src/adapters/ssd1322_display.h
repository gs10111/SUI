// DE-PURI-DI261924 REV A, folha 1/2 (CN4) - adaptador do painel OLED sobre U8g2, implementando
// a porta src/ports/i_display.h. Implementa a Decisao 12 (controlador do display, autoteste,
// tela principal e LED LIG) e o passo 8/9/10 da ORDEM DE BOOT da Parte 2 do DECISIONS.md.
//
// MODELO DO PAINEL AINDA NAO CONFIRMADO (Decisao 12, itens 1 e 9, e decisao humana 6). O CN4 tem
// 7 pinos sem designador, sem part number e sem GND (o retorno e o CN3-4). A identificacao como
// SSD1322 classe NHD-3.2-25664 vem APENAS da resolucao 256x64 em SPI de 4 fios e do fato de o
// firmware de teste de fabrica (src/drivers/display_u8g2.cpp, env esp32dev-ihm) desenhar com esse
// driver na bancada. Nao e leitura de etiqueta e nao e deteccao automatica: sem MISO nao ha o que
// ler. O veredito de controlador vem da etiqueta do modulo, item controlado da BOM (Decisao 12
// item 9, MEDICAO 16). Se o modulo for outro, nada aqui funciona e NAO ha erro reportavel - so
// tela errada. Por isso verifiable() e false em qualquer circunstancia.
//
// SEGURANCA: begin() bem sucedido prova apenas que o barramento e a biblioteca foram
// configurados, NUNCA que o painel respondeu (Decisao 12 item 13). U8G2::begin() do U8g2 retorna
// 1 incondicionalmente. Nenhuma decisao de rele e nenhuma escrita de DAC pode depender desta
// classe, e a ausencia de imagem jamais inibe a atuacao dos reles.
//
// ORDEM DE BOOT, o ponto critico deste arquivo. begin() pre-reserva o VSPI ANTES de u8g2_.begin(),
// porque o SPI.begin() SEM ARGUMENTOS que o U8g2 dispara internamente instalaria os pinos default
// do barramento, entre eles o MISO default do VSPI, que nesta placa e o IO19 = WDI do STWD100.
// Com a pre-reserva o begin interno do U8g2 vira no-op (retorno antecipado 'if(_spi) return;' em
// libraries/SPI/src/SPI.cpp:73-75 do framework-arduinoespressif32 @ 3.20017).
// A PRE-RESERVA E FEITA COM O PINO REAL DE MISO, NAO COM -1:
//     SPI.begin(kDispSclk, kDispMiso, kDispMosi, kNoPin)   // kDispMiso = IO39, input-only, NC
// Passar MISO = -1 NAO desliga o MISO: SPIClass::begin repassa o -1 a spiAttachMISO, e
// cores/esp32/esp32-hal-spi.c:203-231 substitui o -1 pelo default do barramento ('else if
// (spi->num == VSPI) { miso = 19; }') e em seguida faz 'pinMode(miso, INPUT)' +
// 'pinMatrixInAttach(miso, SPI_MISO_IDX(...))'. Ou seja, a forma SPI.begin(18, -1, 23, -1) e
// exatamente o gesto que sequestra o WDI: poe o IO19 em ENTRADA e o prende a matriz do VSPI, e a
// partir dai o GPIO.out_w1ts/out_w1tc da ISR de timer em IRAM nao move mais o pino. Verificado na
// fonte do core instalado. O passo 8 da base comum (DECISIONS.md L607) e a Decisao 12 item 18
// mandam literalmente 'SPI.begin(18, -1, 23, -1)': o texto aprovado esta errado NESTE ponto e
// precisa de errata humana - o adaptador nao pode obedecer a letra sem entregar o IO19.
// O SS continua kNoPin porque SPIClass::begin nao chama spiAttachSS.
// Depois do u8g2_.begin() ainda se chama o rearm do pino do watchdog (passo 10 da ordem de boot),
// SEMPRE pelo gancho RearmHook do construtor. O gancho e OBRIGATORIO e nao tem default: o IO19 e
// do adaptador do watchdog (Stwd100Watchdog::rearmPin(), que alem do pinMode realinha o nivel com
// a fase da ISR), e este adaptador nunca escreve no IO19 por conta propria. Se o composition root
// passar nullptr, o rearm simplesmente NAO acontece - dois donos do mesmo pino e pior que nenhum.
//
// CUSTO BLOQUEANTE DECLARADO (o que a porta chama de "present() e a unica chamada bloqueante"
// vale para o PROTOCOLO DE QUADRO - clear/drawText/fillRect/drawFrame/present -, que e o que roda
// no ciclo. begin() e hardReset() sao chamadas de BOOT e de RECUPERACAO EXPLICITA e bloqueiam
// muito mais que um quadro; nenhuma das duas pode ser chamada de dentro do ciclo de 50 ms):
//   present()   <= 16,4 ms  (8192 bytes a 4 MHz; MEDICAO 10 revisada, aceitacao <= 25 ms)
//   begin()     ~ 335 ms    NAO sao os 150 ms do passo 9 da ordem de boot. Aritmetica verificada
//                           na biblioteca: u8x8_d_helper_display_init (clib/u8x8_display.c:71-76)
//                           faz RESET alto + delay(reset_pulse_width_ms) + RESET baixo +
//                           delay(reset_pulse_width_ms) + RESET alto + delay(post_reset_wait_ms),
//                           e clib/u8x8_d_ssd1322.c:249-250 traz 100 ms e 100 ms = 300 ms de
//                           delay() dentro do proprio u8g2_.begin(), mais o clearDisplay da RAM
//                           do controlador (~16,4 ms) e o sendBuffer do quadro em branco
//                           (16,4 ms). O orcamento de 150 ms do passo 9 e ARITMETICAMENTE
//                           INALCANCAVEL com este driver: e material para a MEDICAO 10 e para
//                           errata humana do orcamento, nao para conserto no adaptador.
//   hardReset() <= 500 ms   pulso explicito de kResetLowMs + kResetSettleMs (Decisao 12 item 2)
//                           MAIS o caminho de init completo acima, porque o painel volta ao POR e
//                           tem de ser reconfigurado. So no boot ou em recuperacao explicita.
// Os delay() destas duas chamadas sao vTaskDelay (Arduino-ESP32), portanto CEDEM a CPU: a tarefa
// ctrl (core 0, prio 5) segue no seu tick de 50 ms e a ISR de timer em IRAM segue chutando o WDI.
// Quem para durante esses ms e o loopTask - botoes e IHM -, nao o ciclo de seguranca nem o
// cachorro. Ainda assim: chamada de boot/recuperacao, nunca de ciclo.
//
// FONTES E METRICAS. Small = u8g2_font_6x12_tr, Medium = u8g2_font_9x15B_tr,
// Large = u8g2_font_t0_30b_tr, todas "_tr" (ASCII
// puro, sem acentuacao) pela politica da Decisao 12 item 16. lineHeightPx() e textWidthPx() NAO
// sao numeros escritos a mao: saem de u8g2_.getMaxCharHeight() e de u8g2_.getUTF8Width() da fonte
// carregada. Com getMaxCharHeight = 12 px em Small, o NormalScreen::statusRowCapacity() do
// dominio fecha em CINCO linhas, que e a regra 4 de domain/ui/normal_screen.cpp; com 28 px em
// Large as duas linhas de eixo cabem em 1+28+2+28 = 59 de 64 px. textWidthPx() devolve a LARGURA
// DE AVANCO (posicao do cursor depois da string), e nao a largura de tinta que o
// u8g2_GetStrWidth entrega: e essa a semantica que o FakeDisplay cumpre (perGlyph * n) e que
// MenuMachine e CalibrationWizard usam para posicionar o digito em video reverso.
//
// PROTOCOLO DE QUADRO. Full-buffer: clear() e os primitivos so escrevem os 2048 bytes de
// framebuffer estatico dentro do objeto; nada chega ao painel antes de present(), que e a unica
// chamada bloqueante DO CICLO (8192 bytes de relogio a 4 MHz = 16,384 ms, Decisao 12 item 3 e
// MEDICAO 10 revisada). setBusClock(4 MHz) e chamado ANTES de u8g2_.begin(), sem o que o U8g2
// assume os 10 MHz do proprio driver ssd1322 e os 4 MHz de board::kDisplaySpiHz seriam ficcao. Os
// quatro sinais do CN4 (SCLK, MOSI, DC, CS) recebem GPIO_DRIVE_CAP_0 (Decisao 12 item 4): o
// retorno do painel e o mesmo condutor das tres linhas de botao (CN3-4) e IO34/IO35 nao tem
// pull-up.
//
// BRILHO: ESTE ADAPTADOR NAO TEM OPINIAO. begin() NAO chama setContrast. O contraste 255 fixo e
// politica de produto (Decisao 12 item 17, sujeita a MEDICAO 15) e mora com as demais politicas
// de IHM, no composition root, que chama display.setContrast(...) depois do begin(). Sem essa
// chamada o painel fica no valor que a propria sequencia de init do driver escreve (0x9f,
// clib/u8x8_d_ssd1322.c:283), que e caracteristica do U8g2 e nao regra de negocio. O valor
// pedido por setContrast() e memorizado e reaplicado depois de hardReset(); se ninguem pediu
// contraste, nada e reaplicado (reaplicar um "0 nao pedido" apagaria a tela).
//
// SUBSTITUIBILIDADE PELO FakeDisplay (LSP), com as diferencas declaradas:
// (1) present() devolve Err::NotInit enquanto begin() nao tiver passado, onde o fake devolve
//     kOk. E a unica pre-condicao mais forte que o fake, e existe porque devolver kOk sem ter
//     enviado byte nenhum ao painel seria mentir num equipamento de seguranca; a ORDEM DE BOOT
//     garante begin() no passo 9, antes de qualquer desenho. Todo o resto do protocolo de
//     quadro (clear, drawText, fillRect, drawFrame, setOrigin e as metricas) so mexe em RAM e
//     por isso e aceito antes do begin(), exatamente como no fake.
// (2) drawText() nao tem o teto de 24 desenhos por quadro do fake: aqui nao existe lista de
//     desenhos, so o framebuffer. Pre-condicao mais fraca, que o LSP permite.
// (3) showPattern() limpa o framebuffer e compoe o padrao, mas NAO apresenta: quem chama e
//     dono do present(), e a invariante "nada visivel antes do present()" continua valendo,
//     como no fake.
// (4) begin() e hardReset() bloqueiam os ms declarados acima, onde o fake volta instantaneo. E
//     custo de tempo, nao de semantica: o fake e substituto para o HOST, e o orcamento do alvo
//     esta escrito aqui para quem monta a ordem de boot.
// (5) hardReset() pode devolver Err::Io se o init pos-reset falhar, onde o fake sempre devolve
//     kOk. E ramo morto por construcao do U8g2 (U8G2::begin() retorna 1 incondicionalmente);
//     fica escrito porque o dia em que a biblioteca reportar, o adaptador reporta junto.
// Igual ao fake por construcao, e isso e o que o dominio ve: hardReset() em instancia que ja
// passou por begin() DEVOLVE O PAINEL PRONTO (ready_ segue true, present() segue kOk) - nao
// existe caminho sem volta; off() registra o estado apagado mesmo antes do begin() e NAO destroi
// o quadro corrente; 4 padroes com os MESMOS textos de patternDescription(); Err::Range fora de
// faixa em showPattern() e em setOrigin(); Err::Param para texto nulo; textWidthPx(nullptr) = 0;
// verifiable() sempre false.
//
// DESVIO DECLARADO em setOrigin(): o deslocamento anti-queima da Decisao 12 item 17 e somado as
// coordenadas dos primitivos, e nao aplicado dentro de present(). Aplicar dentro do present()
// exigiria deslocar o framebuffer no lugar, o que deslocaria DUAS vezes se o chamador chamasse
// present() duas vezes sem clear() - modo de falha silencioso num equipamento que roda 24/7. O
// resultado visivel e identico enquanto setOrigin() for chamado antes do desenho do quadro, que
// e o uso da Decisao 12 (troca a cada 900000 ms). showPattern() ignora o deslocamento de
// proposito: o criterio de aceitacao do autoteste e "as QUATRO bordas fechadas" e um padrao
// deslocado reprovaria um painel bom.
#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#include <U8g2lib.h>
#pragma GCC diagnostic pop

#include <stdint.h>

#include "board_pins.h"
#include "ports/i_display.h"
#include "status.h"

namespace adapters {

class Ssd1322Display final : public IDisplay {
public:
    using RearmHook = void (*)();

    static constexpr uint8_t kPatternCount = 4;
    static constexpr int8_t kOriginLimitPx = 2;
    static constexpr uint32_t kBusClockHz = board::kDisplaySpiHz;
    static constexpr uint32_t kResetLowMs = 10;
    static constexpr uint32_t kResetSettleMs = 120;
    static constexpr uint16_t kRulerStepPx = 16;
    static constexpr uint16_t kRulerTickPx = 8;
    static constexpr uint8_t kColumnMarkCount = 5;

    // Sem valor default de proposito: o passo 10 da ordem de boot tem de ter dono unico e
    // verificavel. Passe Stwd100Watchdog::rearmPin (ou um trampolim para ele).
    explicit Ssd1322Display(RearmHook rearmWatchdogPin);

    Status begin() override;
    Status hardReset() override;

    uint16_t widthPx() const override;
    uint16_t heightPx() const override;

    uint8_t lineHeightPx(TextFont font) const override;
    uint16_t textWidthPx(TextFont font, const char* text) const override;

    Status clear() override;
    Status drawText(int16_t x, int16_t y, const char* text, TextFont font,
                    TextInk ink) override;
    Status fillRect(int16_t x, int16_t y, uint16_t w, uint16_t h, bool on) override;
    Status drawFrame(int16_t x, int16_t y, uint16_t w, uint16_t h) override;
    Status present() override;

    Status setOrigin(int8_t dx, int8_t dy) override;

    Status setContrast(uint8_t value) override;
    Status off() override;

    uint8_t patternCount() const override { return kPatternCount; }
    const char* patternDescription(uint8_t index) const override;
    Status showPattern(uint8_t index) override;

    bool verifiable() const override { return false; }
    const char* driverName() const override { return "u8g2-ssd1322-nhd-256x64-4w-hw-spi"; }

    bool ready() const { return ready_; }
    bool isOff() const { return off_; }
    int8_t originDx() const { return originDx_; }
    int8_t originDy() const { return originDy_; }
    uint8_t lastPattern() const { return pattern_; }

private:
    // Caminho unico de init do painel, compartilhado por begin() e hardReset(). BLOQUEIA (ver
    // CUSTO BLOQUEANTE DECLARADO). blankFrame = true limpa o framebuffer antes de envia-lo (boot);
    // false preserva o quadro corrente, que e o que a recuperacao quer.
    bool initPanel(bool blankFrame);
    void selectFont(TextFont font) const;
    void softenBusDrive() const;
    void rearmWdiPin() const;
    u8g2_uint_t shiftedX(int16_t x) const;
    u8g2_uint_t shiftedY(int16_t y) const;

    mutable U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI u8g2_;
    RearmHook rearm_;
    int8_t originDx_;
    int8_t originDy_;
    uint8_t contrast_;
    uint8_t pattern_;
    bool contrastSet_;
    bool ready_;
    bool off_;
};

}  // namespace adapters
