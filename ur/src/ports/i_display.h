// src/ports/i_display.h
// Painel OLED do CN4 (SSD1322, 256x64, SPI 4 fios, sem MISO). Porta de DESENHO:
// nao conhece teclado, nao conhece menu, nao conhece angulo. So pixels e texto.
// ATENCAO DE SEGURANCA: o CN4 nao tem via de leitura de volta. begin() bem
// sucedido prova apenas que o barramento foi configurado, NUNCA que o painel
// respondeu (verifiable() == false). Nenhuma decisao de rele ou de saida
// analogica pode depender desta porta, e a ausencia de imagem jamais inibe a
// atuacao dos reles.
// Alvo: U8g2Display (src/drivers/display_u8g2.cpp), SSD1322 NHD 256x64 4W HW SPI.
// Fake: FakeDisplay (test/native) - framebuffer 256x64 em array estatico, com
//       captura das strings desenhadas para assercao literal das telas do manual.
// REQ:  MAN-2.1-L26 (OLED 3,2", 256x64), MAN-4-L60 (indicacao da medicao),
//       MAN-5-L66..68 (autoteste do display e logomarca), MAN-5.3..5.11 (telas
//       literais), MAN-7-L297 (mensagem de falha de comunicacao),
//       decisao 12 (padrao de verificacao, batimento de 1 Hz, deslocamento anti-burn-in).
#pragma once

#include <stdint.h>

#include "status.h"

// TRES degraus, com papel fixo. A regra que separa Small de Medium nao e estetica: o painel tem
// 256x64 px e a linha mais longa da IHM ("Valor Limite X1(graus):+000,0", 29 caracteres) so cabe
// inteira em Small. Por isso o texto FIXO - cabecalho de menu, rotulo de campo, dica de falha -
// desce para Small e se encolhe no canto, liberando o resto da tela para o que o operador
// precisa ler de longe, que sobe para Medium.
enum class TextFont : uint8_t {
    Small = 0,  // texto fixo: cabecalho no canto, rotulo de campo, dica, linha secundaria
    Medium,     // conteudo: itens de menu, opcao escolhida, coluna de estado, mensagem
    Large,      // area de medicao (X: e Y:) e o campo numerico em edicao
};

enum class TextInk : uint8_t {
    Normal = 0,  // pixel aceso sobre fundo apagado
    Inverse,     // fundo aceso, glifo apagado: marca o digito em edicao
};

class IDisplay {
public:
    virtual ~IDisplay() = default;
    IDisplay(const IDisplay&) = delete;
    IDisplay& operator=(const IDisplay&) = delete;

    virtual Status begin() = 0;
    virtual Status hardReset() = 0;

    // Geometria fisica, em pixels. Fonte unica para o layout do dominio.
    virtual uint16_t widthPx() const = 0;   // 256
    virtual uint16_t heightPx() const = 0;  // 64

    // Metrica de fonte, para o dominio centralizar e alinhar sem adivinhar.
    virtual uint8_t lineHeightPx(TextFont font) const = 0;
    virtual uint16_t textWidthPx(TextFont font, const char* text) const = 0;

    // --- protocolo de quadro: clear -> desenha -> present ---
    // Nada aparece antes de present(). present() e a UNICA chamada bloqueante
    // (8192 bytes a 4 MHz = 16,4 ms) e por isso e sempre a ULTIMA coisa do ciclo,
    // depois de avaliar limites e comandar reles.
    virtual Status clear() = 0;
    virtual Status drawText(int16_t x, int16_t y, const char* text, TextFont font,
                            TextInk ink) = 0;
    virtual Status fillRect(int16_t x, int16_t y, uint16_t w, uint16_t h, bool on) = 0;
    virtual Status drawFrame(int16_t x, int16_t y, uint16_t w, uint16_t h) = 0;
    virtual Status present() = 0;

    // Deslocamento global do conteudo, em pixels, contra desgaste do OLED com
    // layout estatico 24/7. Aplicado por present(); dx e dy em [-2, +2].
    virtual Status setOrigin(int8_t dx, int8_t dy) = 0;

    virtual Status setContrast(uint8_t value) = 0;
    virtual Status off() = 0;

    // Autoteste do item 5 do manual: padrao deterministico de aceitacao visual
    // (moldura fechada + regua + marcas em x=0/64/128/192/255). Discrimina painel
    // de 128 colunas e offset de coluna errado.
    virtual uint8_t patternCount() const = 0;
    virtual const char* patternDescription(uint8_t index) const = 0;
    virtual Status showPattern(uint8_t index) = 0;

    // Sempre false nesta placa: sem MISO nao ha como provar que o painel existe.
    virtual bool verifiable() const = 0;
    virtual const char* driverName() const = 0;

protected:
    IDisplay() = default;
};
