// Tela do Modo Normal (estados B1, B2, B3 e B7 de docs/ihm-estados.md): decide O QUE aparece
// no painel enquanto a UR esta medindo e traduz o gesto de tecla em pedido para a maquina de
// modos. E politica de produto, nao eletronica: compila em env:native, nao conhece SPI, nao
// conhece SSD1322 e nao decide nada de rele.
//
// CONVENCAO DE CITACAO, porque no repositorio existem DUAS e confundi-las quebra a auditoria
// byte a byte: "Lnnn" aqui e a linha do manual como docs/ihm-estados.md a numera, que e a linha
// do arquivo docs/manual-cliente-sui-2026.txt MENOS 9 - a mesma convencao de
// domain/ui/key_gesture.h ("L81 ... arquivo bruto L90"). DECISIONS.md, ao contrario, cita a
// linha BRUTA do arquivo (o "L204" de A5 e a linha 204 do arquivo, que nesta convencao e L195).
// Toda citacao deste cabecalho foi conferida linha a linha contra o arquivo.
//
// REQ cobertos (a matriz REQ->manual nao esta publicada no repositorio; a leitura de cada
// codigo esta escrita aqui para que a revisao possa contestar a leitura, e nao adivinha-la):
//   DSP-01 - manual 4 L64 e 2.1 L26: "o display indica o valor da medicao, inclusive para
//            valores negativos", em painel de 256x64. Aqui: os dois eixos aparecem sempre, e
//            quando nao ha leitura aparece o traco de Angle, nunca um zero.
//   DSP-02 - manual 5.5 L131: todo angulo em graus no formato +/-XXX,X, com uma casa decimal
//            fixa. Aqui: nenhuma string de angulo e montada a mao; toda ela sai de
//            Angle::format() ou de PresetWizard::formatDeci(), que sao os donos do formato.
//   NRM-01 - manual 5.2 L74..L78: no Modo Normal a UR exibe continuamente a inclinacao medida,
//            com a identificacao do eixo, e as saidas analogicas seguem atualizadas.
//   NRM-02 - manual 5.2 L83: a tecla BAIXO "alterna a indicacao do display".
//   NRM-03 - manual 5.2 L81: MENU mantida ~3 s abre o acesso ao Modo Programacao.
//   NRM-04 - manual 5.2 L85: a tecla CIMA "nao possui funcao neste modo" - com o desvio
//            declarado de que o DUPLO toque e o PSET (L152). O toque simples continua sem
//            funcao e e ignorado, nunca reinterpretado (invariante 6 de docs/ihm-estados.md).
//   COM-01 - docs/protocolo-rs485.md:629 e manual 7 L297: a perda de comunicacao com a sensora
//            e detectada E SINALIZADA. A sinalizacao no painel e esta tela.
//
// DECISAO D3 DO BRIEFING, que e o motivo de este modulo existir: a tela principal deixa de ser
// "um eixo por vez" e passa a mostrar X, Y, os quatro limites, o estado do enlace, o modo da
// saida analogica e a indicacao de Preset ativo. A tecla BAIXO nao alterna mais X/Y (5.2 L83):
// ela percorre B1 -> B2 (detalhe de X) -> B3 (detalhe de Y) -> B1, conforme T12, T13 e T14 de
// docs/ihm-estados.md. O manual nunca publicou o layout da tela principal (lacuna registrada em
// 5.4 de docs/ihm-estados.md), entao o layout inteiro e desvio declarado e entra na errata.
//
// DECISAO A5, APROVADA: na falha de enlace os QUATRO limites vao a alarme, inclusive os
// programados em Off. Esta tela nao reproduz essa regra - ela nao pode: quem decide rele e o
// LimitEvaluator, e o que chega aqui em NormalInput::limit e o estado JA APLICADO aos contatos.
// A consequencia visivel de A5 - os quatro canais lidos "AL" durante a falha - aparece na tela
// porque veio do avaliador, e nao porque a tela deduziu alguma coisa. Se um dia a tela e os
// contatos discordarem, o defeito e de quem preencheu o NormalInput, e o teste que prova isso
// esta em test/native/test_normal.
//
// DECISAO A7, APROVADA (opcao B com rearme operacional, mais o sub-item da opcao A): a falha de
// enlace tem QUATRO estados distintos, em duas linhas, com os textos sem acentuacao, e quando o
// latch pega - 5 entradas em falha dentro de 60 s - a SEGUNDA linha passa a ser
// "FALHA TRAVADA - REARMAR NO MENU". Esta tela nao conta entradas em falha e nao arma latch
// nenhum: ela recebe NormalInput::linkLatched pronto do supervisor de enlace, que e quem tem o
// relogio dessa contagem. Aqui so mora o texto - e a regra de que o LATCH, e nao so o estado
// corrente do enlace, manda na tela (ver NormalInput::linkLatched).
//
// Os textos de falha vem da decisao 7 item 6, que e a lista que A7 aprovou; "FALHA DE
// COMUNICACAO" e o unico deles que tres decisoes escrevem igual (D7, D8 e D12) e e o texto que
// entra na errata do item 7 do manual, que promete a mensagem em L297 e nao a publica. Nenhuma
// string desta tela leva acento: decisao 12 item 16 fixa fonte ASCII para toda a IHM, e e a
// mesma grafia que o manual ja imprime em "RESET DE FABRICA" e "Alteracao bem sucedida!".
//
// DECISAO 12 ITEM 11, APROVADA (recomendacao (a) da decisao humana 5 de D12): BATIMENTO DE DADO
// FRESCO. O modo de falha mais perigoso de uma IHM de seguranca e o painel congelado exibindo
// dado plausivel; o marcador rotativo de 4 posicoes numa caixa de 8x8 px no canto inferior
// direito e a cobertura desse modo. D12 fixa que o marcador e comandado por TRANSACAO VALIDA, e
// nao por volta de laco: por isso ele chega pronto em NormalInput::heartbeatPhase, publicado
// pela camada que conta as transacoes, e esta tela continua sem IClock. Marcador parado =
// nao ha dado novo, seja porque o enlace parou, seja porque o firmware parou - e por isso ele e
// desenhado nas TRES telas, inclusive na de falha.
//
// DECISAO 12 ITEM 8, APROVADA (recomendacao (a) da decisao humana 3 de D12): AUTOTESTE SOB
// DEMANDA. A tecla BAIXO mantida 3000 ms em Modo Normal reexecuta o padrao de verificacao do
// display, sem senha. E o unico gesto de hold desta tela alem do MENU, e a unica cobertura
// periodica do painel numa UR portuaria que quase nunca e reenergizada. Esta tela so PEDE
// (NormalRequest::SelfTest); quem desenha o padrao, cronometra os 30000 ms de timeout e sai ao
// primeiro toque e o modulo de autoteste do display, que e dono de IDisplay::showPattern().
// A tabela 1.2 de docs/ihm-estados.md ("CIMA e BAIXO com hold: sem funcao em todos os estados")
// e anterior a D12 e esta marcada [PEND D1]; D12 item 8, aprovada, prevalece. O hold de CIMA
// continua sem funcao e continua sendo IGNORADO, nunca reinterpretado (invariante 6).
//
// O QUE ESTA TELA NAO FAZ, e e deliberado:
//   - NAO aplica a guarda do PSET. O duplo toque em CIMA vira NormalRequest::Preset e ponto; a
//     recusa por status da sensora (B5) e por leitura instavel (B6) e do dono do PSET, que e
//     quem le o registrador 3 e mede o pico a pico. Uma tela que recusasse gesto por conta
//     propria esconderia do dono do PSET a borda que ele precisa ver.
//   - NAO cadencia o reconhecedor de gesto. Quem chama KeyGesture::update() e o laco de IHM,
//     uma vez por ciclo, para que todas as telas do produto enxerguem a mesma janela de tempo.
//   - NAO decide rele, nao escreve DAC e nao le NVS. O painel nao e canal de seguranca
//     (invariante 5 de docs/ihm-estados.md): sem MISO no CN4, ausencia de imagem nunca inibe a
//     atuacao dos reles. Uma falha de desenho e retida em lastStatus() e nada mais.
//   - NAO inclui domain/limit_rule.h, nem aqui nem no .cpp, enquanto a colisao de nome de
//     domain::LimitOp (struct de predicados em limit_rule.h:48, enum class em parameters.h:54)
//     nao for resolvida: os dois nao coexistem no mesmo TU. O .cpp inclui domain/ui/
//     preset_wizard.h - e portanto parameters.h - para reusar o formatador de +/-1800 decimos
//     do Preset; este cabecalho continua sem incluir nenhum dos dois, e a tela so precisa do
//     valor programado e do estado do contato, que vem em NormalLimitView.
//
// TEMPO: esta tela nao tem prazo nenhum e por isso nao recebe IClock. Todo prazo do Modo Normal
// - hold de 3 s, janela do duplo toque, pisca do PSET, permanencia minima da falha, cadencia do
// batimento - vive em KeyGesture, no supervisor de enlace ou no laco de IHM, ja com
// elapsedMs()/deadlineReached().
//
// SEM HEAP: as linhas sao montadas em buffer fixo de kLineCap bytes na pilha do metodo de
// desenho; nada de std::string, nada de snprintf.
#pragma once

#include <stdint.h>

#include "domain/angle.h"
#include "domain/ui/key_gesture.h"
#include "ports/i_display.h"
#include "ports/i_relay_bank.h"

namespace domain {

// B1, B2 e B3. A selecao e volatil e nao vai para a NVS (decisao 12 item 10): tecla de painel
// nao escreve flash, e todo boot comeca num estado conhecido.
enum class NormalView : uint8_t {
    Main = 0,
    DetailX,
    DetailY,
};

// Os quatro estados de falha aprovados em A7, mais o estado saudavel. Quem os classifica e o
// supervisor de enlace; a tela so escolhe o par de linhas.
enum class NormalLinkState : uint8_t {
    Ok = 0,
    Awaiting,     // do boot ate o primeiro quadro valido
    CommFault,    // enlace mudo
    SensorFault,  // enlace vivo, sensora reprovando o quadro
    Unstable,     // quadro aceito, medicao sem credito para comandar rele
};

enum class NormalAnalogMode : uint8_t {
    Tracking = 0,  // a saida segue a inclinacao medida (manual 5.2 L78)
    Fault,         // codigo de falha de A2 (-11,00 V)
    Calibrating,   // o eixo esta sendo simulado pelo wizard de Auto Calibracao
};

// O que a tela pede a maquina de modos. Um pedido por chamada de update(): quando um gesto
// produz pedido, os gestos seguintes ficam na fila do KeyGesture e serao lidos no proximo
// ciclo, ja pela tela ou pelo modo que o pedido abriu.
enum class NormalRequest : uint8_t {
    None = 0,
    OpenLogin,  // NRM-03: MENU mantida 3 s (T25)
    Preset,     // MAN-5.6-L152: duplo toque em CIMA (T15)
    SelfTest,   // D12 item 8: BAIXO mantida 3000 ms, sem senha
};

constexpr uint8_t kNormalAxisCount = 2;
constexpr uint8_t kNormalAxisX = 0;
constexpr uint8_t kNormalAxisY = 1;

// Um canal de limite como o operador o ve: o contato que a UR esta entregando ao CLP e o valor
// programado. A operacao (Off, >=, <=, +) nao entra: ela se le no Modo Programacao, e o campo
// que importa na tela de medicao e "este contato esta acionado agora".
struct NormalLimitView {
    RelayState state;
    Angle value;
};

// Fotografia de um ciclo. Tudo ja resolvido por quem tem o dado: leitura em coordenada de
// display (Preset e Sentido do Sensor aplicados, manual 5.9 L214), contatos como aplicados aos
// reles, estado do enlace como classificado pelo supervisor.
struct NormalInput {
    Angle reading[kNormalAxisCount];            // [0] = X, [1] = Y

    // EMENDA 2 (aprovada 2026-09-01). Leitura medida e passada pela cadeia, mas SEM credito para
    // comandar rele ou saida analogica: o quadro chegou integro e o conteudo foi recusado. A tela
    // de falha mostra este numero MARCADO, porque esconder um dado que existe nao protege
    // ninguem - e mostra-lo igual a uma leitura boa seria pior, que e o numero plausivel sem
    // aviso. Invalido quando nao houve quadro: ai nao ha o que mostrar.
    Angle unqualified[kNormalAxisCount];
    NormalLimitView limit[kLimitChannelCount];  // ordem de LimitChannel: X1, X2, Y1, Y2
    NormalLinkState link;

    // A7: latch de flapping ja armado pelo supervisor de enlace. A combinacao
    // link == Ok && linkLatched == true e LEGITIMA e nao e transitoria: o enlace se recuperou,
    // o latch continua armado e, por A5, os quatro reles continuam em alarme ate o rearme
    // manual no menu. Nessa combinacao a tela continua sendo a de falha, com a primeira linha
    // "FALHA TRAVADA - REARMAR NO MENU" e a leitura corrente no campo de valor - o operador
    // precisa ver contato em alarme com o caminho de acao junto, e nunca "ENLACE OK" ao lado de
    // quatro canais lendo "AL".
    bool linkLatched;

    // Modo da saida analogica POR EIXO. Nao e um campo unico: durante a Auto Calibracao o
    // wizard escreve codigo de DAC direto e SO no eixo em calibracao (invariante 2 de
    // docs/ihm-estados.md); o outro eixo continua rastreando o angulo real, e rotular os dois
    // de "CALIB" mentiria sobre o unico eixo cuja saida ainda vale.
    NormalAnalogMode analog[kNormalAxisCount];

    bool presetActive[kNormalAxisCount];         // offset do eixo diferente de zero
    int16_t presetOffsetDeci[kNormalAxisCount];  // A9: faixa +/-1800, o DOBRO da de medicao

    // D12 item 11: indice do marcador rotativo, publicado por quem conta as TRANSACOES VALIDAS
    // (`contadorDeTransacoesValidas / 5`). A tela so usa o resto por kHeartbeatPhases; um valor
    // preso significa marcador parado, que e exatamente o que o operador tem de ver quando o
    // dado parou de chegar.
    uint8_t heartbeatPhase;
};

class NormalScreen {
public:
    // Textos de falha, byte a byte. Decisao 7 item 6, aprovada dentro de A7; sem acentuacao
    // por decisao 12 item 16. "FALHA DE COMUNICACAO" e a mensagem que o item 7 do manual
    // (L297) exige e nao publica.
    static constexpr char kTextAwaiting[] = "AGUARDANDO SENSOR";
    static constexpr char kTextCommFault[] = "FALHA DE COMUNICACAO";
    static constexpr char kTextCommFaultHint[] = "Verifique cabo RS485 e +5V";
    static constexpr char kTextSensorFault[] = "FALHA DO SENSOR";
    static constexpr char kTextSensorFaultHint[] = "Sensor de inclinacao em falha";
    static constexpr char kTextUnstable[] = "MEDICAO INSTAVEL";
    // A7, aprovada: a segunda linha quando o latch pega, e a PRIMEIRA linha quando o enlace ja
    // se recuperou e so o latch continua armado.
    static constexpr char kTextLatched[] = "FALHA TRAVADA - REARMAR NO MENU";

    // Cabe a maior linha do produto ("FALHA TRAVADA - REARMAR NO MENU", 31 bytes) e a maior
    // linha montada ("SAIDA X:MEDICAO SAIDA Y:MEDICAO", 31 bytes) com folga, e cabe no
    // kTextCap do IDisplay de teste.
    static constexpr uint8_t kLineCap = 40;

    // D12 item 11: caixa de 8x8 px em (247,55)-(254,62) num painel de 256x64, com a marca de
    // 4x4 px em quatro posicoes. As coordenadas nao sao escritas a mao: saem de widthPx() e
    // heightPx() menos a caixa e a margem de 1 px, o que devolve exatamente 247 e 55.
    static constexpr uint8_t kHeartbeatBoxPx = 8;
    static constexpr uint8_t kHeartbeatMarkPx = 4;
    static constexpr uint8_t kHeartbeatPhases = 4;

    NormalScreen(IDisplay& display, KeyGesture& gesture);

    NormalScreen(const NormalScreen&) = delete;
    NormalScreen& operator=(const NormalScreen&) = delete;

    // Um ciclo de IHM: consome gesto, atualiza a selecao de tela e desenha o quadro inteiro
    // (clear -> desenha -> present). Devolve o pedido para a maquina de modos, ou None.
    NormalRequest update(const NormalInput& in);

    NormalView view() const { return view_; }

    // Entrada (ou reentrada) no Modo Normal: volta a tela principal e nao deixa gesto pela
    // metade atravessar a troca de modo - a soltura da tecla que confirmou no menu nao pode
    // virar toque curto aqui.
    void reset();

    // Primeira falha de desenho do ultimo quadro. O painel nao e canal de seguranca: uma falha
    // aqui e diagnostico, nunca motivo para mexer em rele. A PRIMEIRA falha e a que fica: uma
    // falha posterior nao apaga a origem.
    Status lastStatus() const { return lastStatus_; }

private:
    NormalRequest apply(const Gesture& gesture);
    void advanceView();

    void render(const NormalInput& in);
    void renderMain(const NormalInput& in);
    void renderDetail(const NormalInput& in, uint8_t axis);
    void renderFault(const NormalInput& in);
    void renderHeartbeat(const NormalInput& in);
    void renderPresetMark(const NormalInput& in, int16_t x, int16_t y, TextFont font);

    void drawAt(int16_t x, int16_t y, const char* text, TextFont font);
    int16_t smallRowHeight() const;
    int16_t rowHeight(TextFont font) const;
    uint8_t statusRowCapacity() const;
    uint8_t rowCapacity(TextFont font) const;
    // Onde a coluna de estado comeca: logo depois da area de medicao, MEDIDA na fonte grande.
    int16_t statusColumnX() const;
    // Fonte da coluna de estado NESTE quadro: Medium quando toda linha necessaria cabe em
    // largura e em altura, Small quando nao cabe. Nunca esconde linha para caber fonte maior.
    TextFont statusFont(const NormalInput& in) const;
    // Tela dedicada ao eixo: onde comeca o numero grande, e a fonte das linhas de texto.
    int16_t detailValueX() const;
    TextFont detailFont(const NormalInput& in, uint8_t eixo) const;
    uint16_t maiorLarguraDaColuna(TextFont font, bool mesmoModo) const;

    void keep(Status status);

    static bool sameAnalogMode(const NormalInput& in);
    static const char* limitLabel(uint8_t index);
    static const char* analogText(NormalAnalogMode mode);

    IDisplay& display_;
    KeyGesture& gesture_;
    NormalView view_;
    Status lastStatus_;
};

}  // namespace domain
