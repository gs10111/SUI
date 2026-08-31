// Preset (Referencia Angular): programacao do valor de referencia de cada eixo, efetivacao pelo
// gesto de PSET e a formula unica que transforma o angulo bruto da sensora na leitura exibida e
// comparada pelos quatro reles.
//
// Manual SUI-DI141388XY (docs/manual-cliente-sui-2026.txt, numero de linha do arquivo bruto;
// toda citacao deste modulo foi reconferida contra esse arquivo):
//   5.5  L139..L142 - faixa de medicao de +/-90,0 graus com resolucao de 0,1 grau, e o formato
//                "+/-XXX,X" de uma casa decimal fixa (L140) que o Preset herda.
//   5.6  L144 - "estabelecer a referencia angular da leitura dos eixos X e Y, ou seja, o valor,
//                em graus, que deve ser indicado no display quando o equipamento monitorado
//                estiver em uma posicao conhecida"; "A programacao e feita diretamente em
//                angulo, na faixa de -90,0 a +90,0, com resolucao de 0,1".
//   5.6  L146..L147 - os dois parametros: "Preset X: referencia angular do eixo X" e
//                "Preset Y: referencia angular do eixo Y".
//   5.6  L148 - submenu literal, o unico que o manual imprime: "Preset>Voltar   Preset X   Preset Y".
//   5.6  L149 - "A configuracao do Preset e realizada em duas etapas: programacao do valor de
//                referencia e ativacao do Preset". E a razao de existir deste modulo: gravar o
//                valor NAO desloca referencia nenhuma.
//   5.6  L151..L159 - procedimento de programacao (L155 MENU seleciona o digito, L156 UP altera
//                o digito, L157 DOWN altera o sinal); a edicao digito a digito e de
//                domain/digit_editor.h.
//   5.6  L161 - efetivacao: "posicione o equipamento monitorado na posicao correspondente a
//                esse valor e execute o comando de preset por meio de um duplo acionamento da
//                tecla UP (PSET). O display piscara, indicando que o comando foi aceito, e a
//                leitura passa a ser apresentada em relacao ao valor programado".
//   5.6  L162 - "O valor do Preset e gravado na memoria EEPROM e mantido apos o desligamento".
//   5.8  L188..L199 - Sentido do Sensor por eixo; L199: "A alteracao do sentido do sensor
//                inverte o sinal da leitura. Apos alterar este parametro, recomenda-se refazer o
//                Preset e conferir os valores programados nos Limites 1 a 4".
//
// REQ cobertos: PST-01 (programacao do valor de referencia por eixo, -90,0 a +90,0 com passo de
// 0,1 grau), PST-02 (efetivacao em duas etapas: o valor programado so vira offset quando o gesto
// de PSET chega), PST-03 (a leitura passa a ser relativa a referencia), PST-04 (o Preset e o
// offset em vigor sao campos persistidos de domain/parameters.h), DIR-01 (o Sentido do Sensor
// define o sinal de incremento da leitura, por eixo) e DIR-02 (a troca de sentido inverte o sinal
// e obriga reconferir Preset e Limites 1 a 4).
//
// DECISAO A9, APROVADA EM 2026-08-31, opcao A - a formula e UNICA em todo o produto:
//
//     leitura = clamp(dir * bruto + offset, -900, +900),  dir em {+1, -1}
//     no aceite do gesto:  offset := P - dir * bruto
//
// Consequencias que este modulo torna executaveis, e que nao podem ser reabertas aqui:
//   - a ordem e bruto -> Sentido -> Preset (D14 item 5). O offset nasce ja em coordenada de
//     display, e por isso a troca de Sentido invalida o offset em vez de converte-lo;
//   - so ha subtracao e troca de sinal, entao o erro de arredondamento introduzido pelo Preset e
//     exatamente 0,0 grau, e nenhum ponto flutuante aparece no caminho de rele;
//   - no instante do aceite a soma vale exatamente P (D1 item 14): nao existe saturacao no aceite,
//     e a leitura seguinte ao gesto e o proprio valor programado.
//
// AS QUATRO GUARDAS DE A9 (D1 itens 6, 9, 10 e 11), na ordem em que sao avaliadas:
//   1. ARMAMENTO (item 6, DESVIO DE L161): o duplo toque so vale se o operador tiver visitado a
//      tela Preset X ou Preset Y e saido do Modo Programacao, e por 120000 ms a partir dessa
//      saida. Fora da janela o gesto nao faz nada e NAO EXIBE NADA - por isso existe o desfecho
//      Ignored, distinto de qualquer recusa com tela. O ARMAMENTO E DE USO UNICO: o PSET aceito
//      (por requestPset ou por confirmPset) CONSOME o armamento, e um segundo PSET exige nova
//      visita a tela Preset. Isto e restricao adicional a D1 item 6, escolhida aqui e presa por
//      teste: L149 define o PSET como a segunda de DUAS etapas, uma por gesto, e um armamento
//      reutilizavel por 120000 ms transformaria cada toque duplo acidental restante da janela em
//      nova referencia dos quatro limites.
//   2. DADO VALIDO (item 9): sem leitura valida dos DOIS eixos o PSET e recusado com
//      "PSET recusado!". Um preset nao pode inventar referencia para um sensor mudo: a sensora
//      publica status 0x0011 sobre angulos CONGELADOS quando o SCL3300 nao responde, e presetar
//      sobre angulo congelado corrompe em silencio a referencia dos quatro limites. A idade da
//      transacao e o valor exato de 0x0001 no registrador 3 sao avaliados na camada de aplicacao,
//      que e quem tem o enlace; aqui a ausencia de leitura chega como Angle::invalid(), conforme
//      o contrato de domain/angle.h ("enquanto o enlace nao entrega quadro valido nao existe
//      leitura, e nenhuma operacao pode inventar uma").
//   3. ESTABILIDADE (item 10): janela circular de 8 amostras por eixo (400 ms a 50 ms de ciclo),
//      pico-a-pico maximo de 5 decimos de grau NOS DOIS EIXOS. Recusa com "Instavel, refaca!".
//   4. MAGNITUDE (item 11, DESVIO DE L161): deslocamento de offset acima de 50 decimos (5,0 graus)
//      em qualquer dos eixos nao e aplicado pelo duplo toque; a UR pede confirmacao por hold de
//      MENU de 3 s e cancela sozinha em 10000 ms. E o unico item que endereca o erro real, que e
//      o gesto executado com a estrutura FORA da posicao de referencia.
//
// TEXTO DE TELA. "PSET recusado!" e o texto de B5 e "Instavel, refaca!" o de B6, ambos de
// docs/ihm-estados.md secao 3.2, que e o contrato desta etapa; D1 item 10 propunha
// "Instavel! ppX:000,8 ppY:000,3", e o pico-a-pico medido continua disponivel em
// peakToPeakDeci() para quem quiser compor essa variante sem mexer aqui. "Novo PSET X:-012,0" e
// "Segure MENU 3s" sao de D1 item 11 e o indicador permanente "PSET X:-012,0" e de D1 item 17.
// Todos sao DESVIO DO MANUAL declarado - telas inventadas, escritas sem acentuacao, no padrao de
// "Alteracao bem sucedida!" (L183) e "RESET DE FABRICA" (L246).
//
// O aviso de DIR-02 NAO e lacuna: a Decisao 14 item 7, aprovada, fixa as duas linhas byte a
// byte e elas DEPENDEM DO EIXO, por isso sao formatadas e nao constantes:
//   linha 1: "Sentido X alterado!"            (e "Sentido Y alterado!")
//   linha 2: "Preset zerado - confira X1 X2"  (e "Preset zerado - confira Y1 Y2")
// Os nomes X1, X2, Y1 e Y2 sao os do proprio manual para os quatro limites (5.9, L202).
//
// DIVERGENCIA DE NUMEROS RESOLVIDA, para nao restar duvida: docs/ihm-estados.md descreve a guarda
// de estabilidade como "pico-a-pico <= 0,2 graus em 20 amostras" e marca a linha inteira como
// [PEND D1]. A pendencia aponta para a Decisao 1, que fixa 8 amostras e 5 decimos - e esses sao
// os numeros implementados.
//
// FRONTEIRA. Este modulo decide, calcula e escreve no agregado Parameters; ele nao desenha, nao
// grava em NVS, nao reconhece teclas e nao comanda rele. Quem reconhece o duplo toque e o hold e
// domain/ui/key_gesture.h; quem publica o par de offsets para a tarefa de controle dentro de um
// unico tick (D1 item 18) e quem grava logo depois (D1 item 19) e a camada de aplicacao; o piscar
// de 1200 ms do aceite (D1 item 16) e da tela principal. A deduplicacao de escrita e o intervalo
// minimo de 10000 ms entre gravacoes de PSET (D2 item 9) sao politica do armazenamento, nao
// entram aqui, e este modulo entrega o que elas precisam: o desfecho Applied e o offset resultante.
//
// UM EIXO OU OS DOIS: D1 item 8 e D2 item 9 divergiam. A9 fechou em D1 - o gesto aplica os DOIS
// eixos de uma vez, atomicamente, porque L161 manda posicionar o equipamento uma unica vez e usa
// um unico gesto. Se um dos dois offsets for recusado, NENHUM dos dois e escrito.
//
// TEMPO: todo prazo passa por elapsedMs()/deadlineReached() de ports/i_clock.h, nunca por "a > b",
// e NENHUM PRAZO E REAVALIADO DEPOIS DE VENCIDO. As tres janelas deste modulo (armamento de
// 120000 ms, confirmacao de 10000 ms e aviso de sentido de 3000 ms) sao FECHADAS por tick(), que
// a camada de aplicacao chama a cada ciclo de 50 ms; armed(), awaitingConfirm() e warningActive()
// apenas leem o flag ja latchado. Guardar um carimbo e recompara-lo para sempre violaria o
// contrato de ports/i_clock.h (a subtracao unsigned so vale para intervalos menores que 2^31 ms):
// depois de 2^32 ms de uptime continuo o elapsed volta a zero e a janela reabriria sozinha. Um
// supervisor portuario fica energizado meses, entao 49,7 dias e rotina, nao caso de borda.
#pragma once

#include <stdint.h>

#include "domain/angle.h"
#include "domain/digit_editor.h"
#include "domain/parameters.h"
#include "ports/i_clock.h"
#include "status.h"

namespace domain {
namespace ui {

// Submenu D2, literal de L148. "Voltar" volta ao "Menu>" e nao ao Modo Normal.
enum class PresetMenuItem : uint8_t { Back = 0, PresetX = 1, PresetY = 2 };

// Desfecho do gesto de PSET. Ignored NAO e recusa: e o gesto fora da janela de armamento, que
// pelo item 6 nao faz nada e nao exibe nada.
enum class PsetOutcome : uint8_t {
    Ignored = 0,
    Applied,
    NeedsConfirm,
    RefusedNoData,
    RefusedUnstable,
};

class PresetWizard {
public:
    static constexpr uint32_t kArmValidityMs = 120000;
    static constexpr uint32_t kConfirmWindowMs = 10000;
    static constexpr uint32_t kDirWarningMs = 3000;

    static constexpr int16_t kConfirmThresholdDeci = 50;
    static constexpr uint8_t kStabilityWindow = 8;
    static constexpr int16_t kStabilityPeakToPeakDeci = 5;

    static constexpr uint8_t kItemCount = 3;

    // "+XXX,X" mais o terminador. O offset vai a +/-1800 decimos (180,0 graus) e por isso NAO e
    // um Angle, mas continua cabendo no formato de tres inteiros e um decimo de L131.
    static constexpr uint8_t kValueTextCap = 7;
    static constexpr uint8_t kIndicatorTextCap = 8 + kValueTextCap;   // "PSET X:" + valor
    static constexpr uint8_t kConfirmTextCap = 13 + kValueTextCap;    // "Novo PSET X:" + valor

    // D14 item 7: "Sentido X alterado!" (19) e "Preset zerado - confira X1 X2" (29), com o
    // terminador. Dependem do eixo, e por isso sao formatadas em vez de constantes.
    static constexpr uint8_t kDirWarnLine1Cap = 20;
    static constexpr uint8_t kDirWarnLine2Cap = 30;

    static constexpr const char* kMenuTitle = "Preset>Voltar   Preset X   Preset Y";
    static constexpr const char* kRefusedNoDataText = "PSET recusado!";
    static constexpr const char* kRefusedUnstableText = "Instavel, refaca!";
    static constexpr const char* kConfirmHintText = "Segure MENU 3s";
    static constexpr const char* kOutOfRangeText = "FORA DA FAIXA +/-090,0";

    explicit PresetWizard(const IClock& clock);

    PresetWizard(const PresetWizard&) = delete;
    PresetWizard& operator=(const PresetWizard&) = delete;

    // --- A formula unica de A9, pura e sem estado ---

    // leitura = clamp(dir * bruto + offset, -900, +900). Leitura bruta invalida continua
    // invalida: deslocar o que nao existe nao produz medicao.
    static Angle reading(Angle raw, SensorDir dir, int16_t offsetDeci);

    // offset := P - dir * bruto. false quando falta o valor programado ou a leitura bruta -
    // e a recusa que impede o preset de inventar referencia para um sensor mudo.
    static bool offsetFor(Angle target, Angle raw, SensorDir dir, int16_t& outOffsetDeci);

    // Leitura corrente do eixo, ja com Sentido e Preset aplicados, a partir do agregado vigente.
    static Angle reading(Axis axis, Angle raw, const Parameters& params);

    // --- Submenu D2 (L148) ---
    PresetMenuItem item() const { return item_; }
    void nextItem();
    void prevItem();
    static const char* itemLabel(PresetMenuItem which);

    // --- PST-01: programacao do valor de referencia ---

    // Abre a edicao no valor VIGENTE do eixo, faixa -900 a +900 decimos, passo de 1 decimo.
    // Abrir a tela tambem e a "visita" que o armamento do item 6 exige.
    bool beginEdit(Axis axis, const Parameters& params);
    bool editing() const { return editing_; }
    Axis editAxis() const { return editAxis_; }
    void editMenu();
    void editUp();
    void editDown();
    bool formatEdit(char* out, uint8_t cap) const;
    uint8_t editCursorTextIndex() const;

    // Consulta pura, como manda A13: informa a recusa e nao toca no valor nem no cursor.
    ConfirmResult editConfirm() const;
    const char* editOutOfRangeMessage() const;

    // PST-02, primeira etapa: grava SO o valor programado. O offset em vigor nao e tocado, e
    // por isso a leitura nao se move ao gravar. Err::Range quando o valor nao cabe na faixa.
    Status commitEdit(Parameters& params);
    void cancelEdit();

    // --- Prazos (D1 itens 6 e 11, D14 item 7) ---

    // Chamada a cada ciclo de 50 ms pela camada de aplicacao. E QUEM FECHA as tres janelas:
    // o cancelamento da confirmacao de D1 item 11 tem de ACONTECER, nao ser apenas consultavel.
    // Depois de fechada, nenhuma janela reabre - nem no wrap de 2^32 ms do relogio.
    void tick();

    // --- Armamento (D1 item 6) ---
    bool visited() const { return visited_; }
    void onProgrammingExit();
    bool armed() const;
    void disarm();

    // --- Amostragem e guardas de dado e estabilidade (D1 itens 9 e 10) ---

    // Uma amostra de angulo BRUTO por ciclo de controle. Amostra invalida em qualquer eixo
    // esvazia a janela: nao existe media de leitura que nao houve.
    void sample(Angle rawX, Angle rawY);
    bool dataValid() const;
    bool stable() const;
    int16_t peakToPeakDeci(Axis axis) const;
    Angle lastRaw(Axis axis) const;
    void clearSamples();

    // --- PST-02, segunda etapa: o gesto de PSET ---

    // Duplo toque de UP ja reconhecido por KeyGesture. O aceite CONSOME o armamento: um segundo
    // PSET exige nova visita a tela Preset.
    PsetOutcome requestPset(Parameters& params);
    bool awaitingConfirm() const;
    int16_t pendingOffsetDeci(Axis axis) const;

    // Hold de MENU de 3 s do item 11, ja reconhecido por KeyGesture. Reavalia dado e
    // estabilidade e recalcula os offsets sobre a amostra CORRENTE: o que o operador confirma e
    // presetar aqui e agora, nao aplicar um numero de dez segundos atras. O aceite tambem
    // consome o armamento.
    PsetOutcome confirmPset(Parameters& params);
    void cancelPset();

    // D1 item 8, atomicidade VISIVEL: os dois offsets sao validados ANTES de qualquer escrita,
    // e por isso nao existe rollback. Publica para que a promessa seja testavel: pela algebra de
    // faixas (|P| <= 900 e |bruto| <= 900) o gesto nunca produz offset fora de [-1800, +1800],
    // entao a recusa e invariante de defesa, nao caminho alcancavel pelo operador.
    static bool offsetsWritable(int16_t offsetXDeci, int16_t offsetYDeci);

    // --- DIR-02: troca do Sentido do Sensor ---

    // Grava o sentido e, quando ele MUDA, zera o offset daquele eixo, desarma o gesto e levanta
    // o aviso obrigatorio de 3 s. Zerar e deterministico e visivel, porque o indicador
    // permanente some no mesmo instante; manter um offset calculado contra o sinal oposto
    // deslocaria os dois pontos de atuacao do eixo em ate 180,0 graus sem nenhum indicio.
    Status applySensorDir(Axis axis, SensorDir dir, Parameters& params);
    bool warningActive() const;

    // D14 item 7, byte a byte e por eixo: "Sentido X alterado!" e
    // "Preset zerado - confira X1 X2" (analogas para Y, com "Y1 Y2").
    static bool formatDirWarningLine1(Axis axis, char* out, uint8_t cap);
    static bool formatDirWarningLine2(Axis axis, char* out, uint8_t cap);

    // --- Telas ---

    // "+XXX,X" com largura constante, para decimos em [-1800, +1800].
    static bool formatDeci(int16_t deci, char* out, uint8_t cap);

    // "PSET X:-012,0" enquanto o offset do eixo for diferente de zero (D1 item 17). false
    // quando o offset e zero: sem offset nao ha indicador, e essa ausencia e informacao.
    static bool formatIndicator(Axis axis, const Parameters& params, char* out, uint8_t cap);

    // "Novo PSET X:-012,0", primeira linha da confirmacao de magnitude; a segunda e
    // kConfirmHintText. false assim que a janela de 10000 ms fecha - a tela SAI do display, e
    // nao continua montada anunciando um gesto que ja nao existe.
    bool formatPendingConfirm(Axis axis, char* out, uint8_t cap) const;

private:
    static uint8_t idx(Axis axis) { return static_cast<uint8_t>(axis); }
    static int16_t directed(Angle raw, SensorDir dir);
    void clearPending();

    bool computeOffsets(const Parameters& params, int16_t (&outOffsets)[Parameters::kAxisCount]) const;
    bool applyOffsets(Parameters& params, const int16_t (&offsets)[Parameters::kAxisCount]) const;

    const IClock& clock_;
    DigitEditor editor_;

    PresetMenuItem item_;
    bool editing_;
    Axis editAxis_;

    bool visited_;
    bool armWindowOpen_;
    uint32_t armedAtMs_;

    int16_t sample_[Parameters::kAxisCount][kStabilityWindow];
    uint8_t sampleHead_;
    uint8_t sampleCount_;

    bool pending_;
    uint32_t pendingSinceMs_;
    int16_t pendingOffsetDeci_[Parameters::kAxisCount];

    bool warning_;
    uint32_t warningSinceMs_;
};

}  // namespace ui
}  // namespace domain
