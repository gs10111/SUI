// Maquina de estados da IHM do assistente de Auto Calibracao de um eixo (manual 5.7). A
// MATEMATICA NAO MORA AQUI: os dois pontos, o gate de plausibilidade e a serializacao sao do
// AnalogCalibration e do AnalogScaler, que ja existem em src/domain/. Este modulo e so a
// sequencia de telas, os prazos e o que a saida analogica do eixo emite em cada passo.
//
// Manual SUI-DI141388XY (docs/manual-cliente-sui-2026.txt, numeracao do arquivo bruto):
//   L164 - "Durante o procedimento, a Unidade Remota simula internamente a inclinacao
//          informada, permitindo calibrar a saida sem movimentar o equipamento monitorado".
//   L165 - "A saida analogica e bipolar e simetrica em relacao ao zero. Por exemplo,
//          ajustando-se o fundo de escala em 45,0 graus para +10,00 Vcc, a inclinacao de
//          -45,0 graus resultara em -10,00 Vcc". E o que o passo F4 mede.
//   L166 - "A calibracao e realizada individualmente para cada eixo, por meio dos parametros
//          Auto Calibracao X e Auto Calibracao Y" - de onde sai o titulo da tela de aviso.
//   L167 - voltimetro de precisao conectado ANTES de iniciar o procedimento.
//   L172 - tela do zero, "Ajuste 0Vcc:0000" (F1 CAL_ZERO).
//   L173 - "Utilize a tecla MENU para selecionar o digito desejado e as teclas UP e DOWN para
//          alterar seu valor, acompanhando a leitura do voltimetro ate que a tensao de saida
//          seja de 0,00 Vcc".
//   L174 - "Mantenha a tecla MENU pressionada por aproximadamente 3 segundos para confirmar o
//          ajuste do zero" (hold de 3 s, GestureKind::Hold de KeyGesture).
//   L177 - tela do fundo de escala, "Angulo fim de escala X(graus):+045,0" (F2 CAL_FS). O
//          manual imprime o simbolo de grau; o painel e ASCII e escreve "(graus)", como
//          docs/ihm-estados.md ja registra para as demais telas.
//   L180 - tela do ganho, "Ajuste 10Vcc:0000" (F3 CAL_GANHO).
//   L182 e L183 - hold de 3 s grava e o display confirma "Alteracao bem sucedida!" (F5).
//   L184 - "Concluida a calibracao, o equipamento retorna automaticamente ao Modo Normal de
//          Operacao" - o unico procedimento do manual que devolve o operador ao Modo Normal.
//   L187 - "O ajuste do zero deve ser sempre realizado antes do ajuste do ganho" (REQ-CAL-08).
//   L136 - saida por timeout de inatividade de aproximadamente 2 minutos.
// REQ-CAL-01 a REQ-CAL-08.
//
// A SEQUENCIA E ESTADO, NAO CONVENCAO DE CHAMADA (REQ-CAL-03). Nao existe entrada publica que
// leve ao ajuste do ganho sem passar pelo hold que confirma o zero e pelo hold que confirma o
// angulo de fundo de escala: begin() sempre abre no aviso de saida simulada, e o unico gesto
// que avanca um passo e o Hold da tecla MENU no passo corrente. Um segundo begin() com o
// assistente vivo devolve Err::Busy, e as telas F2 e F3 nao tem caminho de volta para F1
// (docs/ihm-estados.md 3.6).
//
// A CADEIA COMPLETA DE TELAS, com a fonte de cada uma:
//   Blocked        "CALIBRACAO BLOQUEADA"                 D6 item 13 (DECISIONS.md L1639)
//   Warning        "Auto Calibracao X"/"Y" + as duas linhas de aviso   A14 (L412)
//   Zero           "Ajuste 0Vcc:NNNN"                     L172, F1
//   FullScaleAngle "Angulo fim de escala X(graus):+045,0" L177, F2
//   Gain           "Ajuste 10Vcc:NNNN"                    L180, F3
//   VerifyNegative "Verifique -10Vcc"                     A14 (L412), F4 CAL_VERIF_NEG
//   Done           "Alteracao bem sucedida!"              L183, F5
//   Rejected       "CALIBRACAO REJEITADA"                 D6 item 12 (DECISIONS.md L1638)
//   ExitMarker     sem tela: a IHM chamadora ja tem a dela
//
// DECISAO A14 (DECISIONS.md L412, opcao A, APROVADA em 2026-08-31), itens adotados aqui:
//  - as duas telas de trim abrem no VALOR CORRENTE, que com o par de fabrica e 5000 e nao
//    0000. Abrir em 0000 saltaria a saida em -1,907 V no instante em que a tela aparece, na
//    frente do voltimetro do tecnico. Desvio de manual ja aprovado: errata das figuras de
//    L172 e L180;
//  - AVISO CONFIRMADO NA ENTRADA: "aviso 'SAIDA SIMULADA / Bloqueie o CLP' confirmado por hold
//    de 3 s na entrada". Step::Warning e esse ponto, e e a UNICA porta para Step::Zero. Sem
//    ele nao existiria instante em que o firmware soubesse que o tecnico VIU o aviso antes de
//    a saida do eixo passar a mentir para o CLP. As mesmas duas linhas continuam desenhadas em
//    todas as telas do procedimento, porque um aviso confirmado e esquecido nao adverte quem
//    chegar depois no painel;
//  - VERIFICACAO OBRIGATORIA DO RAMO NEGATIVO: "passo obrigatorio de verificacao em -10 V com
//    portao de 10,0 mV". E Step::VerifyNegative, entre o commit e a conclusao, com a saida no
//    codigo espelhado 2*zero - fundo (AnalogScaler::mirrorCode()). L165 AFIRMA a simetria e o
//    procedimento de L169..L183 nunca a mede: sem este passo o ramo negativo sai de fabrica
//    sem nenhuma medida. O portao de 10,0 mV e criterio do TECNICO no voltimetro de L167 - a
//    placa nao tem via de leitura da propria saida, entao o firmware entrega o codigo espelhado
//    e a janela de 20 s de F4/T60 de docs/ihm-estados.md, e quem reprova a placa e a bancada.
//    O teto de 30 s da proposta original (D10 item 9) e respeitado por construcao: 20 s < 30 s;
//  - COMMIT DO PAR, NUNCA DE MEIA CALIBRACAO. O hold de L174 confirma o zero apenas no buffer
//    do AnalogCalibration; nada e efetivado. O unico instante em que o par existe e o commit()
//    do hold de L182. Zero novo com ganho velho produz saida plausivel e errada, sem
//    assinatura observavel, e e pior que nenhuma calibracao;
//  - EXCECAO DE A14 A L136: o timeout NAO grava o buffer do assistente. Vale para os dois
//    prazos - a inatividade de 120 s de L136 e o teto absoluto de override de 300 s da
//    decisao 6 item 8 - e para abort(). Todos devolvem o par anterior integro;
//  - gate de entrada da decisao 6 item 13: com qualquer limite sinalizado o assistente e
//    recusado, exibe "CALIBRACAO BLOQUEADA" por 2000 ms e devolve o tecnico ao Modo
//    Programacao SEM nunca ter tomado a saida analogica (overriding() e falso em Blocked).
//
// AS DUAS TELAS NOVAS SEGUEM A DECISAO APROVADA, NAO O RASCUNHO DE docs/ihm-estados.md. O
// estado F6 de docs/ihm-estados.md 3.6 traz "Calibracao recusada!" por 3000 ms com volta a F3
// e esta marcado ali mesmo com [PEND D9]/[PEND D10], isto e, pendente das decisoes que
// DECISIONS.md fechou depois: D6 item 12, APROVADO, manda "CALIBRACAO REJEITADA" por 2000 ms e
// define "Recusa = aborto sem gravar + marcador de saida". A secao "Contradicoes residuais"
// (DECISIONS.md L2679) registra as tres grafias concorrentes e a folha de aprovacao escolheu a
// de D6; e ela que esta implementada. O mesmo vale para "CALIBRACAO BLOQUEADA" (D6 item 13),
// que docs/ihm-estados.md nao chegou a desenhar.
//
// AVISO PERMANENTE DE SAIDA SIMULADA (A6/decisao 6 itens 1 e 7). Enquanto o override existe, a
// saida analogica do eixo em calibracao NAO representa a inclinacao real, e o manual nao
// adverte sobre isso. Por isso as duas linhas de aviso sao desenhadas em TODAS as telas em que
// overriding() e verdadeiro, inclusive na de conclusao. Em Step::Blocked elas NAO aparecem: ali
// nao ha override, e escrever "SAIDA SIMULADA" sobre uma saida que esta rastreando o angulo
// real seria mentira na direcao perigosa. Os quatro reles e o outro eixo continuam no angulo
// REAL: a simulacao termina no codigo escrito no DAC do eixo em calibracao.
//
// A IHM NAO ESCREVE O DAC (decisao 6 item 2). outputCode() e o codigo que a tarefa de
// controle deve escrever no eixo enquanto overriding() for verdadeiro; nenhuma porta de
// hardware entra por aqui. O mapa vem da decisao 6 item 7, mais a linha de A14:
//   marcador de entrada (tela de aviso, >= 1000 ms)  -> 3932 (-11,00 V, decisao A2)
//   tela "Ajuste 0Vcc"                               -> 32768 + (campo - 5000)
//   tela "Angulo fim de escala"                      -> 3932
//   tela "Ajuste 10Vcc"                              -> zero confirmado + 26214 + (campo - 5000)
//   tela "Verifique -10Vcc"                          -> 2*zero gravado - fundo gravado (A14)
//   conclusao, recusa, aborto, timeout e marcador de saida -> 3932
// Todo codigo entregue e grampeado em 6554..61342, os mesmos limites que AnalogScaler usa como
// gate. O grampo INFERIOR so e alcancavel pela tela de verificacao: o gate de make() aceita
// espelho ate 5243 (piso de A2, para que o marcador de -11,00 V nunca caia dentro da faixa
// util), e um par legitimo com espelho entre 5243 e 6553 tem de sair em 6554, porque -10,00 V
// e o fim da faixa util de L185. As duas telas de trim nunca chegam la: o campo de zero so
// emite 27768..37767 e o de ganho 48982..61342.
//
// MARCADOR DE ENTRADA (decisao 6 item 6, DECISIONS.md L1626): 3932 por 1000 ms "antes de
// qualquer tela de medicao". Aqui isso e ESTRUTURA e nao contagem paralela: a tela de aviso de
// A14 nao e tela de medicao, emite 3932, e o hold que a encerra so e aceito depois de
// kEntryMarkerMs desde begin(). Nao existe janela em que o tecnico veja um campo de digitos
// respondendo as teclas enquanto o voltimetro mostra -11,00 V.
//
// MARCADOR DE SAIDA (decisao 6 item 7): 3932 por 1000 ms antes de a saida voltar a rastrear o
// angulo real. A conclusao NAO precisa de estado proprio para isso - "Alteracao bem sucedida!"
// ja fica 1500 ms em 3932 (F5/T61 de docs/ihm-estados.md), que cobre os 1000 ms exigidos. Os
// caminhos que nao tem tela de permanencia (aborto, timeout, teto) e a recusa, que D6 item 12
// manda terminar em "aborto sem gravar + marcador de saida", passam por Step::ExitMarker.
//
// TEMPO: todo prazo passa por elapsedMs()/deadlineReached() de ports/i_clock.h. A inatividade
// e rearmada pelo CARIMBO do gesto, nunca pelo instante em que ele foi drenado, para que um
// ciclo atrasado por gravacao de NVS nao encurte a janela do tecnico. E so gesto COM FUNCAO
// no passo corrente rearma: invariante 6 de docs/ihm-estados.md ("um gesto sem funcao
// declarada e ignorado, nunca reinterpretado"). Como IO34/IO35 nao tem pull-up interno
// (docs/ihm-estados.md 1.1), cabo de IHM solto gera tecla fantasma, e tecla fantasma que
// rearmasse a inatividade prenderia o override mentindo para o CLP ate o teto de 300 s.
//
// FLUSH DA FILA DE GESTOS, CONTRATO DO CHAMADOR. src/domain/ui/key_gesture.h define flush()
// como "a troca de tela ou de modo": ele drena a fila, joga fora o gesto pela metade e marca
// como consumida a tecla prensada agora. Este modulo troca de tela oito vezes e troca de MODO
// na saida, entao o chamador TEM de chamar KeyGesture::flush() sempre que
// consumeFlushRequest() devolver true - que e em begin() e em toda troca de Step, inclusive a
// entrada em Idle. Sem isso, um duplo toque de UP iniciado na tela do ganho e entregue ate
// 400 ms depois, cai no Modo Normal e executa um PSET que ninguem pediu, deslocando os quatro
// pontos de atuacao. O sinalizador e de leitura destrutiva: quem le, consome.
//
// LAYOUT: nenhuma coordenada de linha e chutada, pela mesma regra escrita em
// domain/ui/normal_screen.cpp. As tres linhas saem de IDisplay::lineHeightPx() e de
// IDisplay::heightPx() em render(): texto na linha 0, aviso ancorado no rodape. Nao ha
// escolha de fonte como o fittedFont() do NormalScreen porque nao existe fonte menor que
// Small e a linha mais larga do assistente ja cabe nela: "Angulo fim de escala
// X(graus):+045,0" tem 36 glifos, 252 px na metrica da porta, contra 256 px de painel. Um
// ramo de recuo por largura seria codigo que nenhuma entrada alcanca, e ramo inalcancavel em
// modulo de seguranca e cobertura falsa; quem prende a invariante e o teste de geometria, que
// mede CADA desenho das telas com textWidthPx() e lineHeightPx() da propria porta. O
// deslocamento anti-burn-in (setOrigin) e o contraste ficam com quem for dono do ciclo de
// display.
#pragma once

#include <stdint.h>

#include "domain/analog_calibration.h"
#include "domain/digit_editor.h"
#include "domain/parameters.h"
#include "domain/ui/key_gesture.h"
#include "ports/i_clock.h"
#include "ports/i_display.h"
#include "status.h"

namespace domain {

class CalibrationWizard {
public:
    enum class Step : uint8_t {
        Idle = 0,
        Blocked,
        Warning,
        Zero,
        FullScaleAngle,
        Gain,
        VerifyNegative,
        Done,
        Rejected,
        ExitMarker,
    };

    static constexpr uint32_t kEntryMarkerMs = 1000;
    static constexpr uint32_t kExitMarkerMs = 1000;
    static constexpr uint32_t kBlockedMs = 2000;
    static constexpr uint32_t kVerifyNegativeMs = 20000;
    static constexpr uint32_t kDoneMs = 1500;
    static constexpr uint32_t kRejectedMs = 2000;
    static constexpr uint32_t kInactivityMs = 120000;
    static constexpr uint32_t kOverrideCeilingMs = 300000;

    static constexpr uint16_t kFaultCode = AnalogScaler::kFaultCode;
    static constexpr uint16_t kCodeClampMin = AnalogScaler::kMinus10VCode;
    static constexpr uint16_t kCodeClampMax = static_cast<uint16_t>(AnalogScaler::kCodeMax);

    static constexpr uint8_t kTextCap = 40;

    CalibrationWizard(AnalogCalibration& calibration, const IClock& clock);

    CalibrationWizard(const CalibrationWizard&) = delete;
    CalibrationWizard& operator=(const CalibrationWizard&) = delete;

    // Err::Busy: ja ha assistente vivo, incluindo a tela de bloqueio - o chamador nao precisa
    // fazer nada, a tela dele ja esta no ar. Err::Aborted: gate de entrada da decisao 6 item
    // 13, limite sinalizado; o assistente NAO abriu, mas assumiu a tela "CALIBRACAO BLOQUEADA"
    // por 2000 ms e a devolve sozinho ao Modo Programacao. Os dois codigos sao distintos de
    // proposito: "ignore, ja esta aberto" e "recusei, e a explicacao esta na tela" pedem
    // reacoes diferentes do chamador.
    Status begin(Axis axis, bool anyLimitSignalled);

    void onGesture(const Gesture& gesture);

    // Avalia os prazos. Chamada uma vez por ciclo de IHM.
    void tick();

    // Desenha o quadro inteiro do assistente. Err::NotInit quando nao ha tela a desenhar
    // (assistente parado ou no marcador de saida), e nesse caso o display nao e tocado.
    Status render(IDisplay& display) const;

    // Texto LITERAL da tela corrente, sem as linhas de aviso. false quando nao ha tela.
    bool screenText(char* out, uint8_t cap) const;

    Step step() const { return step_; }
    Axis axis() const { return axis_; }
    bool active() const { return step_ != Step::Idle; }
    bool overriding() const { return step_ != Step::Idle && step_ != Step::Blocked; }

    // true uma unica vez por troca de tela ou de modo: o chamador deve responder com
    // KeyGesture::flush(). Ler consome.
    bool consumeFlushRequest();

    // Codigo que a tarefa de controle escreve no eixo em calibracao. So tem significado
    // enquanto overriding(); parado devolve o nivel de falha, que e o valor seguro.
    uint16_t outputCode() const;

    // Saida por tecla ou por saida do Modo Programacao: nada e gravado.
    void abort();

private:
    static uint16_t clampCode(int32_t code);

    bool editing() const;
    bool watched() const;
    uint8_t prefixLength() const;
    const char* prefix() const;
    const char* fixedText() const;

    void openTrimEditor(uint16_t field);
    void openAngleEditor(int16_t deci);
    void pushEditorValue();
    void enterStep(Step next, uint32_t atMs);
    void abortAt(uint32_t atMs);

    AnalogCalibration& cal_;
    const IClock& clock_;

    DigitEditor editor_;
    Step step_;
    Axis axis_;
    uint32_t beganMs_;
    uint32_t stepSinceMs_;
    uint32_t lastKeyMs_;
    bool flushRequested_;
};

}  // namespace domain
