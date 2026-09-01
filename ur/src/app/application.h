// src/app/application.h
// Ciclo de controle da Unidade Remota DE-PURI-DI261924 REV A: o dono UNICO da transacao com a
// placa sensora, do filtro, da avaliacao dos quatro limites, da escrita dos quatro reles, da
// escrita das duas saidas analogicas e do token de liveness do watchdog externo. E a camada de
// aplicacao pura da arquitetura hexagonal: recebe TODAS as portas por referencia, nao instancia
// nada, nao conhece FreeRTOS, nao inclui Arduino.h e nao usa ponto flutuante. Quem cria a tarefa
// e main.cpp; quem mede o tempo e o IClock injetado.
//
// BASE DE TEMPO (DECISIONS.md 2.1, ur_base.h): periodo de 50 ms, timeout de enlace de 35 ms
// (imposto pela propria porta), 3 transacoes invalidas consecutivas (150 ms) declaram falha,
// 5 boas consecutivas mais 2000 ms de permanencia minima saem dela, e o dado que comanda rele
// nao pode ter mais de 72 ms de idade (kPollPeriodMs + 22).
//
// O CICLO E TRES CHAMADAS, e nao uma, porque a espera pela resposta e do escalonador e nao do
// dominio: a tarefa ctrl chama startCycle() (que aplica os parametros publicados pela IHM e
// dispara a transacao), depois pollCycle() ate ele devolver true (cedendo a CPU entre as
// chamadas, o que uma funcao pura nao pode fazer sozinha sem virar espera ocupada), e por fim
// finishCycle(), que executa filtro, limites, reles, DAC e batimento do watchdog. O guarda de
// kCycleGuardMs existe para que um adaptador travado em Busy nunca prenda a tarefa: estourado o
// prazo a transacao e abortada e o ciclo conta como invalida.
//
// CADEIA DE MEDICAO (decisao 4 item 7, ordem canonica): bruto da sensora -> EMA/recarga ->
// Sentido do Sensor -> Preset -> grampo em +/-900 decimos -> UNICO int16 por eixo, que alimenta
// display, saida analogica e comparacao dos quatro limites, sem copia paralela. O filtro fica a
// MONTANTE de Sentido e Preset, que sao lineares e comutam com o EMA: por isso o commit de um
// Preset novo produz degrau exato, sem transitorio de filtro e sem pulso de rele. O passo do
// filtro e dirigido por TEMPO (item 10): todo ciclo executa exatamente um passo, com retencao de
// ordem zero da ultima amostra valida quando a transacao falha.
//
// CRITERIO DE AMOSTRA VALIDA (decisao 4 item 3, REQ-MEA-02): registrador 3 igual a 0x0001
// EXATO - kStsAcceptedExact - e nada mais. Isso neutraliza o defeito conhecido da sensora, que
// publica kStsDataValid junto com kStsSclNotResponding sobre angulo velho. Angulo fora de
// +/-90,0 graus e amostra invalida (A4, opcao A, criterio estrito): Angle::fromDeciDegrees
// devolve invalido e a amostra nao entra. Recarga do filtro (item 9, lista fechada): primeira
// amostra valida do boot, primeira apos sair da falha, e reinicio da sensora detectado pela
// queda do registrador 7.
//
// ESTADO SEGURO: os reles vao a Signalled nos quatro canais quando o LimitEvaluator declara
// falha, INCLUSIVE nos canais programados em 'Off' (A5, opcao A: 'Off' significa "sem criterio
// angular", e falha de enlace nao e angulo). A saida analogica vai ao codigo de falha 3932
// (-11,00 V, fora de banda) do boot ate o primeiro quadro valido e em toda falha de enlace ou de
// sensor (2.4). Nunca 0,00 V, que e a leitura legitima mais provavel E a assinatura fisica da UR
// sem energia. Se applyMask() reprovar, o ciclo cai em signalAll(): um caminho de rele que
// devolve sucesso sem ter escrito e defeito de seguranca.
//
// A IHM (loop(), core 1) NUNCA escreve rele nem DAC. Ela publica: publishParameters() troca o
// conjunto ativo inteiro no inicio do ciclo seguinte - atomico por construcao, nunca um eixo na
// referencia nova e o outro na velha (decisao 1 item 18) - e requestAnalogOverride() entrega ao
// wizard de Auto Calibracao o codigo cru do eixo em calibracao (decisao 6 item 7). O eixo nao
// calibrado continua rastreando o angulo real, e os reles continuam vivos durante todo o
// procedimento (decisao 6 item 2). main.cpp e quem envolve essas chamadas em seccao critica.
//
// AS DUAS TRAVESSIAS DE NUCLEO SAO EXPLICITAS, E ESTA E A RAZAO DE applyPublished() E
// latchSnapshot() SEREM PUBLICAS. Um spinlock tomado de um lado so nao exclui nada: se o
// loop() (core 1) entra em taskENTER_CRITICAL para publicar e a tarefa ctrl (core 0) le sem
// tomar o MESMO portMUX, a copia de domain::Parameters pode sair rasgada e a flag pendingValid_
// pode ser apagada por cima de uma publicacao que o operador acabou de confirmar. Por isso a
// aplicacao do conjunto publicado NAO acontece mais dentro de startCycle(): ela e
// applyPublished(), que a tarefa ctrl chama SOZINHA dentro da seccao critica, antes de abrir o
// ciclo - a seccao cobre uma copia de struct e nada de I/O. Simetricamente, snapshot() nao
// monta mais nada: quem monta e latchSnapshot(), ultima acao da tarefa ctrl no ciclo, tambem
// sob a seccao critica, e snapshot() so devolve o buffer ja pronto. Ver ctrlTask() em
// src/main.cpp.
//
// GUARDA DURA DE IDADE (decisao 5 item 30): se a idade da ultima amostra ACEITA passar de
// kHardStaleMs = 250 ms, os quatro reles vao a alarme e as duas saidas ao codigo de falha,
// INDEPENDENTEMENTE do contador de 3 transacoes invalidas. Ela existe porque, durante um
// bloqueio da tarefa ctrl (janela de cache-off da NVS, inversao de prioridade, overrun do
// vTaskDelayUntil), NENHUMA transacao e tentada: badRun_ nao avanca, link_state_ segue Ok e o
// dado envelhece em silencio enquanto os GPIOs de rele e o DAC8562, que sao latches de
// hardware, seguram o ultimo estado permissivo. A idade e medida no ciclo que a OBSERVA, antes
// do carimbo da amostra nova - se fosse depois, uma amostra boa na volta do bloqueio zeraria a
// idade e a guarda nunca dispararia em lugar nenhum, que e exatamente o defeito que ela
// existe para cobrir.
#pragma once

#include <stdint.h>

#include "domain/analog_scaler.h"
#include "domain/angle.h"
#include "domain/limit_evaluator.h"
#include "domain/limit_rule.h"
#include "domain/low_pass_filter.h"
#include "domain/parameters.h"
#include "ports/i_analog_output.h"
#include "ports/i_clock.h"
#include "ports/i_relay_bank.h"
#include "ports/i_sensor_link.h"
#include "ports/i_watchdog.h"
#include "status.h"

#include "domain/ui/normal_screen.h"
#include "domain/ui/preset_wizard.h"
#include "ports/i_display.h"

namespace app {

constexpr uint8_t kAppAxisCount = 2;

// Parametros da tarefa que main.cpp cria: DECISIONS.md 2.1, kCtrlTaskPriority/Core/StackBytes.
// A loopTask do Arduino tem prioridade 1; o ciclo de seguranca corre acima dela e na outra CPU.
constexpr uint32_t kCtrlTaskStackBytes = 4096;
constexpr uint8_t kCtrlTaskPriority = 5;
constexpr uint8_t kCtrlTaskCore = 0;

enum class LinkHealth : uint8_t {
    Awaiting = 0,  // do boot ate o primeiro quadro valido: analogica em 3932, reles em alarme
    Ok,            // quadro aceito e dentro da idade maxima
    CommFault,     // 3 transacoes mudas ou mal enquadradas consecutivas
    SensorFault,   // quadro integro, status reprovado pela sensora
};

// Traducao de saude do enlace em estado de TELA. Vive aqui, e nao no composition root, porque e
// a funcao que decide o que o operador le quando algo reprova - e no main.cpp ela era invisivel
// ao env native, ou seja, sem teste nenhum. Foi ali que "sensora respondendo e se declarando
// doente" virou "falha de comunicacao" na tela, mandando procurar defeito num cabo perfeito.
//
// stale so impede o "Ok": quando updateHealth() ja classificou a falha pelo verdict_ do enlace,
// quem manda e a classificacao, nao a idade.
domain::NormalLinkState mapLinkToScreen(LinkHealth health, bool stale);



class Application {
public:
    static constexpr uint16_t kCyclePeriodMs = 50;
    static constexpr uint16_t kCycleGuardMs = 45;
    static constexpr uint8_t kFailsToFault = 3;
    static constexpr uint8_t kGoodsToRecover = 5;
    static constexpr uint32_t kFaultMinDwellMs = 2000;
    static constexpr uint32_t kDataMaxAgeMs = 72;
    static constexpr uint16_t kAcceptedStatus = kStsAcceptedExact;
    // Decisao 5 item 30. 45 % de margem sobre o pior caso de 172 ms do item 27 e 4,5x abaixo
    // do tWD minimo de 1120 ms do STWD100.
    static constexpr uint32_t kHardStaleMs = 250;
    // Teto do override da Auto Calibracao (decisao 6 item 7 lido junto com 2.4). A IHM
    // republica o override a cada passagem de 50 ms, entao 2000 ms sao 40 republicacoes de
    // folga; passado o prazo o eixo volta sozinho ao codigo de falha. Sem prazo, uma IHM
    // travada com o assistente aberto deixaria o DAC preso num valor SIMULADO para sempre,
    // com a tarefa ctrl viva e o cachorro sendo alimentado normalmente.
    static constexpr uint32_t kOverrideMaxAgeMs = 2000;
    // Teto de bloqueio do commit de NVS (decisao 2 item 16). Atraso ate aqui e tick perdido
    // tolerado; acima disto a ctrl declara falha de enlace na hora.
    static constexpr uint32_t kNvsCommitBudgetMs = 500;

    // A7, APROVADA E FECHADA (opcao B com rearme operacional): 5 ENTRADAS em falha dentro de
    // 60000 ms travam o estado de falha, a tela mostra "FALHA TRAVADA - REARMAR NO MENU" e o
    // rearme e feito no painel, atras da senha, sem cortar energia.
    // O QUE EXATAMENTE FICA TRAVADO: os QUATRO RELES (em alarme) e as DUAS SAIDAS analogicas (no
    // codigo de falha). NAO a maquina de saude do enlace: link() continua medindo e publicando a
    // verdade, e o painel mostra a leitura corrente com o texto de rearme por cima. E a
    // combinacao "link == Ok com linkLatched armado" que NormalScreen ja desenha e que
    // docs/ihm-estados.md B7 descreve; travar tambem a saude do enlace esconderia do operador o
    // unico fato que ele precisa para decidir rearmar - que o cabo voltou. Sem o latch, um enlace
    // intermitente - conector oxidado no cais, cabo prensado, terminacao ruim - recupera sozinho
    // para sempre e o operador so ve o painel piscando entre normal e falha, sem que nada
    // registre a gravidade. O que conta e ENTRADA em falha, nao ciclo em falha: uma falha longa
    // e um evento, cinco curtas em um minuto sao cinco.
    // NAO CONFUNDIR COM O LATCH DE A8 (configuracao perdida), que e configLatched_ e vem de
    // fora, do composition root, por outra causa e com outra saida (Reset Geral).
    static constexpr uint8_t kFaultsToLatch = 5;
    static constexpr uint32_t kFlapWindowMs = 60000;

    struct Snapshot {
        domain::Angle reading[kAppAxisCount];
        domain::Angle raw[kAppAxisCount];
        // EMENDA 2 (aprovada 2026-09-01). Leitura passada pela cadeia de medicao mas SEM credito
        // para comandar nada: existe quando o quadro chegou integro e o conteudo foi recusado -
        // sensora viva que se declara doente. Serve so ao display, marcada. Nunca entra em
        // driveRelays() nem em driveAnalog(): o campo e de diagnostico, nao de decisao.
        domain::Angle unqualified[kAppAxisCount];
        RelayState limitState[kLimitChannelCount];
        RelayMask relayMask;
        LinkHealth link;
        bool overriding[kAppAxisCount];
        uint16_t analogCode[kAppAxisCount];
        uint16_t sensorStatus;
        uint32_t cycles;
        uint32_t faultEvents;
        uint32_t relayWriteErrors;
        uint32_t analogWriteErrors;
        bool configLatched;
        // A7: latch de flapping do ENLACE. E o sinal que a tela principal le em
        // NormalInput::linkLatched - nunca configLatched, que e outra decisao (A8) com outra
        // causa e outra saida.
        bool linkLatched;
        // A saida analogica reprovou a escrita e o adaptador deixou de estar pronto: latch, do
        // mesmo jeito que relayBankDead. A IHM nao pode dizer "rastreando" sobre um DAC que nao
        // aceita escrita.
        bool analogDead;
        // Guarda dura de idade em vigor neste ciclo (decisao 5 item 30). A IHM mostra falha de
        // enlace quando ela esta ligada: os reles ja estao em alarme e o painel nao pode dizer
        // "Ok" por cima disso.
        bool stale;
        // applyMask() E signalAll() reprovaram no mesmo ciclo: o banco de reles esta mudo e o
        // token de liveness deixou de ser renovado - a placa vai ser resetada pelo STWD100.
        bool relayBankDead;
    };

    Application(const IClock& clockRef, ISensorLink& linkRef, IRelayBank& relayRef,
                IAnalogOutput& analogRef, IWatchdog& watchdogRef);
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    Status begin(const domain::Parameters& params);
    void publishParameters(const domain::Parameters& params);
    void requestAnalogOverride(domain::Axis axis, uint16_t code);
    void clearAnalogOverride(domain::Axis axis);
    void setFilterTimeConstant(uint16_t timeConstantMs);
    // PUBLICADO, nao escrito direto. configLatched_ e linkLatched_ sao lidos por finishCycle()
    // FORA do portMUX, e linkLatched_ tambem e ESCRITO la, por updateHealth(). Um clear vindo do
    // loop() ao mesmo tempo que um set vindo da tarefa ctrl e update perdido: ou o rearme some, ou
    // o latch que acabou de armar e apagado. Por isso os dois viram pedido pendente aqui e sao
    // consumidos por applyPublished(), que roda sob o portMUX na tarefa ctrl - dai em diante o
    // dono unico dos dois campos e a ctrl.
    void setConfigLatched(bool latched);
    bool configLatched() const { return configLatched_; }

    // So antes de a tarefa ctrl existir. Depois disso, use setConfigLatched().
    void initConfigLatched(bool latched);

    // JANELA DE COMMIT DE NVS ANUNCIADA. kWriteBudgetMs do adaptador de NVS e 250 ms e
    // kHardStaleMs tambem: margem zero. Sem isto, gravar um setpoint congela a tarefa ctrl por
    // exatamente o tempo que dispara a guarda dura de idade, e salvar um parametro leva os quatro
    // reles a alarme - alarme falso a cada gravacao. A janela e ANUNCIADA pelo loop() depois da
    // escrita e vale UM ciclo: a decisao 6 ja congela os reles nos modos em que a gravacao
    // acontece (Programacao, Auto Calibracao, Reset Geral), entao o envelhecimento dessa janela e
    // esperado e nao e evidencia de enlace ruim. Estouro do orcamento NAO e creditado: ai a guarda
    // trabalha, que e o que se quer de um bloqueio que passou do que foi declarado.
    void noteCommitWindow(uint32_t elapsedMs);

    // REARME DE A7. A IHM publica isto sob o MESMO portMUX das outras travessias, depois de o
    // operador passar pelo gate de senha (a mesma de A13). Liberar um latch nao e atuar rele,
    // entao este ponto sobrevive a retirada dos comandos de atuacao das decisoes de engenharia.
    // O rearme limpa tambem a janela de contagem: cinco eventos novos sao necessarios para
    // travar de novo, e nao um.
    void clearLinkLatch();
    bool linkLatched() const { return linkLatched_; }
    bool analogDead() const { return analogDead_; }

    // As duas metades que a tarefa ctrl executa SOB O MESMO portMUX da IHM. applyPublished()
    // vem antes de startCycle(); latchSnapshot() depois de finishCycle(). Nenhuma das duas faz
    // I/O: sao copias de struct, sub-microssegundo, muito abaixo do tique de 1 ms da ISR do WDI.
    void applyPublished();
    void latchSnapshot();

    // A tarefa ctrl viu um atraso de escalonamento maior que dois periodos e reancorou o
    // relogio (decisao 2 item 16). Ate kNvsCommitBudgetMs o tick perdido e tolerado e NAO conta
    // como transacao invalida; acima disso a falha de enlace e declarada na hora.
    void noteStall(uint32_t delayMs);

    void startCycle();
    bool pollCycle();
    void finishCycle();

    Snapshot snapshot() const;
    LinkHealth link() const { return link_state_; }
    bool stale() const { return stale_; }
    bool relayBankDead() const { return relayBankDead_; }
    uint32_t cycles() const { return cycles_; }
    const domain::Parameters& active() const { return active_; }

private:
    static uint8_t axisIndex(domain::Axis axis);
    static const domain::LimitOps& opsFor(domain::LimitOp op);
    void applyRules();
    void applyScalers();
    bool accept(const SensorSample& sample, uint32_t nowMs) const;
    void updateHealth(bool good, uint32_t nowMs);
    // Registra UMA entrada em falha e avalia o latch de A7. Chamada dos dois unicos pontos que
    // declaram falha: updateHealth() e noteStall().
    void noteFaultEntry(uint32_t nowMs);
    void driveRelays(bool fresh);
    void driveAnalog(uint32_t nowMs);

    const IClock& clock_;
    ISensorLink& link_;
    IRelayBank& relays_;
    IAnalogOutput& analog_;
    IWatchdog& watchdog_;

    domain::Parameters active_;
    domain::Parameters pending_;
    domain::LimitEvaluator evaluator_;
    domain::LowPassFilter filter_[kAppAxisCount];
    domain::AnalogScaler scaler_[kAppAxisCount];
    domain::Angle raw_[kAppAxisCount];
    domain::Angle reading_[kAppAxisCount];
    Snapshot pub_;
    SensorSample sample_;
    uint32_t cycleStartMs_;
    uint32_t faultSinceMs_;
    uint32_t lastGoodMs_;
    uint32_t pendingCommitMs_;
    domain::Angle unqualified_[kAppAxisCount];
    uint32_t overrideSinceMs_[kAppAxisCount];
    // Anel de carimbos das ultimas kFaultsToLatch entradas em falha (mesmo padrao do anel
    // anti-chatter do LimitEvaluator). Anel e nao contador porque a janela desliza: o que
    // importa e se a QUINTA entrada mais recente caiu dentro dos ultimos 60000 ms.
    uint32_t faultStampMs_[kFaultsToLatch];
    uint8_t faultStampCount_;
    uint8_t faultStampHead_;
    uint32_t cycles_;
    uint32_t faultEvents_;
    uint32_t relayErrors_;
    uint32_t analogErrors_;
    uint16_t overrideCode_[kAppAxisCount];
    uint16_t analogCode_[kAppAxisCount];
    uint16_t sensorStatus_;
    uint16_t lastBeat_;
    LinkPoll verdict_;
    LinkHealth link_state_;
    uint8_t badRun_;
    uint8_t goodRun_;
    bool overrideActive_[kAppAxisCount];
    bool pendingValid_;
    bool pendingConfigLatched_;
    bool pendingConfigLatchedValid_;
    bool pendingLinkLatchClear_;
    bool pendingCommitValid_;
    bool commitCredit_;
    bool haveBeat_;
    bool reloadPending_;
    bool cycleOpen_;
    bool configLatched_;
    bool linkLatched_;
    bool analogDead_;
    bool stale_;
    bool relayBankDead_;
};

// Monta a entrada da tela principal a partir do snapshot e dos parametros ativos. Vive aqui, e
// nao no composition root, pelo mesmo motivo de mapLinkToScreen: no main.cpp ela nao compila no
// env native e nenhum teste a alcanca. O custo disso ja apareceu - a Emenda 2 acrescentou o campo
// unqualified em Snapshot e em NormalInput, os testes de tela continuaram verdes porque montam o
// NormalInput a mao, e a ligacao entre os dois ficou faltando: a leitura marcada nao chegava a
// tela nenhuma. Aqui, um teste consegue exigir que cada campo atravesse.
domain::NormalInput buildNormalInput(const Application::Snapshot& snap,
                                     const domain::Parameters& params);

// Tela de edicao do Preset. Estava dentro de src/main.cpp, que nao compila no env native: a
// unica tela que grava o offset dos quatro pontos de atuacao de um eixo nao tinha teste nenhum.
// Mesma armadilha de buildNormalInput, mesma correcao - desce para uma camada que a suite
// alcanca, e o composition root vira encaminhamento de uma linha.
void renderPresetEdit(IDisplay& display, const domain::ui::PresetWizard& preset,
                      domain::Axis axis);

}  // namespace app
