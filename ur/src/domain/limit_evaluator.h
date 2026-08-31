// Comparador dos quatro limites: pega a leitura ja exibivel dos dois eixos e devolve, uma vez
// por ciclo, a mascara de reles que a UR deve aplicar.
//
// Manual SUI-DI141388XY secao 5.9 L200 a L208 (as quatro operacoes), L201 (Limites 1 e 2 do
// eixo X, 3 e 4 do eixo Y), L223 (o valor comparado e a leitura do display, com Preset e
// Sentido do Sensor ja aplicados) e secao 7 L296 a L306 (falha de comunicacao).
//
// ESTADO DE BOOT E O ESTADO SEGURO, NAO O ESTADO LIMPO. O avaliador nasce com os quatro canais
// em RelayState::Signalled, linkFaulted() verdadeiro e o contador de invalidade ja saturado.
// Sem isso o primeiro applyMask(mask()) do passo 13 do boot anunciaria "estrutura nivelada,
// nenhum limite atingido" ao CLP antes de existir qualquer leitura - a mesma mentira
// momentanea que A2 proibe na saida analogica ("a saida sai do trilho negativo direto para o
// nivel de falha, SEM PASSAR POR 0,00 V") e que a janela "AGUARDANDO SENSOR" cobre na tela. O
// criterio de 3 amostras de A4 governa a QUEDA em falha durante a operacao, nunca o estado
// inicial: a saida do alarme de boot e regida pelos 3000 ms normais de liberacao, contados a
// partir da primeira leitura valida.
//
// Decisao A3, aprovada: ataque confirmado em 100 ms, liberacao confirmada em 3000 ms, teto
// anti-chatter que estende a liberacao para 60000 ms enquanto houver 20 ataques ou mais nos
// ultimos 600 s e volta a 3000 ms quando cair a 5 ou menos. Alarme angular SEM latch: o rele
// acompanha a inclinacao real, como L205 a L207 definem ("acionado sempre que"). O teto so
// PROLONGA o estado de alarme, nunca o atrasa.
//
// PENDENCIA DE RASTREABILIDADE (bigboss): o numero de SAIDA do teto (kChatterCeilingExit = 5)
// nao aparece em DECISIONS.md, que so especifica a ENTRADA ("apos 20 ataques em 600 s"). O
// teste prova que o teto DESENGATA quando a janela esvazia; o valor exato do limiar de saida
// nao e afirmado por teste nenhum enquanto nao for registrado em A3.
//
// Decisao A4, aprovada: amostra saturada, sem quadro fresco ou fora de +/-90,0 nao e leitura.
// Tres ciclos invalidos consecutivos (150 ms na base de 50 ms) levam os quatro reles ao
// alarme. Antes disso o ciclo invalido CONGELA os prazos em contagem, e nao os zera nem os
// deixa correr: sem leitura nova nao ha evidencia nem para atacar nem para liberar.
//
// Decisao A5, aprovada: na falha, os QUATRO reles vao ao alarme, INCLUSIVE os programados em
// Off. Desvio declarado de L204: "independentemente do angulo medido" continua verdadeiro,
// porque falha de enlace nao e angulo, e um canal cabeado deixado em Off tem de denunciar que
// a UR nao esta saudavel.
//
// DOIS OBSERVADORES, COM NOMES HONESTOS. linkFaulted() e o estado do ENLACE visto por este
// modulo: cai no primeiro ciclo valido. alarmActive() e o estado EFETIVAMENTE APLICADO aos
// reles: so cai quando o ultimo canal forcado pela falha tiver cumprido a liberacao. Tudo que
// nao pode contradizer os contatos entregues ao CLP - o texto de tela de falha e o codigo de
// falha da saida analogica - segue alarmActive(); linkFaulted() serve ao diagnostico do
// enlace e e o que o supervisor de enlace (A7, latch e permanencia minima) consome. Um unico
// predicado chamado "faulted" fazia display, analogica e reles contarem tres historias
// diferentes durante os 3 s seguintes a recuperacao.
//
// Decisao A1: o dominio decide Signalled ou Clear e nada mais. Quem traduz isso em nivel de
// bobina e domain::coilLevel(), com a polaridade por parametro.
//
// O PRAZO E MEDIDO EM TEMPO DECORRIDO PELO IClock, NAO EM CONTAGEM DE CICLOS: um ciclo
// atrasado nao pode encurtar a confirmacao de ataque, e a regra fica identica se a base de
// tempo mudar. Na base comum de 50 ms isso da 3 ticks para os 100 ms de ataque. O prazo conta
// do PRIMEIRO tick em que a condicao foi observada, e qualquer tick em que ela deixe de valer
// zera a contagem: confirmacao exige condicao continua. Commit de regra (setRule) tambem zera:
// tempo acumulado sob o predicado antigo nao pode ser creditado ao predicado novo, senao um
// commit larga o alarme sem que a condicao nova tenha sido observada pelo prazo aprovado
// (criterio de aceitacao da medicao M9: zero comutacoes em 100 commits).
//
// TODA comparacao de prazo usa elapsedMs()/deadlineReached() de ports/i_clock.h, imunes ao
// wrap de 49,7 dias, INCLUSIVE a janela do teto anti-chatter: em vez dos 12 baldes indexados
// por nowMs/50000 do rascunho - unico numero do arquivo derivado de uma divisao de nowMs, e
// que no wrap zerava o historico e desengatava o teto sem indicio nenhum - cada canal guarda
// um anel de carimbos de tempo dos ultimos kChatterCeilingEnter ataques. O anel implementa a
// janela deslizante de 600 s de A3 de forma exata (o esboco de baldes dava 600 a 650 s), custa
// 80 bytes por canal, nao usa heap e nao tem nenhuma divisao de tempo.
//
// Sem heap, sem float, sem estado global: quatro canais em armazenamento fixo, um objeto por
// UR, relogio injetado.
#pragma once

#include <stdint.h>

#include "domain/angle.h"
#include "domain/limit_rule.h"
#include "ports/i_clock.h"
#include "ports/i_relay_bank.h"

namespace domain {

constexpr uint32_t kAttackConfirmMs = 100;
constexpr uint32_t kReleaseConfirmMs = 3000;
constexpr uint32_t kReleaseCeilingMs = 60000;
constexpr uint32_t kChatterWindowMs = 600000;
constexpr uint16_t kChatterCeilingEnter = 20;
constexpr uint16_t kChatterCeilingExit = 5;
constexpr uint8_t kInvalidCyclesToFault = 3;

// Profundidade do anel de carimbos por canal. Basta guardar os kChatterCeilingEnter ataques
// mais recentes: a entrada do teto pergunta se ha 20 ou mais na janela, e a saida pergunta se
// ha 5 ou menos - as duas respostas ficam exatas com os 20 ultimos.
constexpr uint8_t kChatterMemory = static_cast<uint8_t>(kChatterCeilingEnter);

// Uma leitura de ciclo, ja em coordenada de display (L223). 'fresh' e false quando o enlace
// nao entregou quadro integro e aceito neste ciclo; um Angle invalido cobre o resto da
// decisao A4 (saturado ou fora de +/-90,0 graus).
struct LimitInput {
    Angle x;
    Angle y;
    bool fresh;
};

class LimitEvaluator {
public:
    explicit LimitEvaluator(const IClock& clockRef);
    LimitEvaluator(const LimitEvaluator&) = delete;
    LimitEvaluator& operator=(const LimitEvaluator&) = delete;

    void setRule(LimitChannel channel, const LimitRule& newRule);

    RelayMask update(const LimitInput& in);

    RelayMask mask() const;
    RelayState state(LimitChannel channel) const;

    // Enlace: verdadeiro do boot ate a primeira leitura valida e sempre que kInvalidCyclesToFault
    // ciclos invalidos consecutivos se completarem. Nao fala pelos reles.
    bool linkFaulted() const { return linkFaulted_; }

    // Reles: verdadeiro enquanto qualquer canal estiver sinalizado, por angulo ou por falha.
    bool alarmActive() const { return mask() != kRelayMaskAllClear; }

    // 3000 ms, ou 60000 ms enquanto o teto anti-chatter da decisao A3 estiver engatado.
    uint32_t releaseConfirmMs(LimitChannel channel) const;

private:
    struct Channel {
        LimitRule rule;
        RelayState relayState;
        bool timing;
        bool ceiling;
        uint32_t sinceMs;
        uint32_t attackMs[kChatterMemory];
        uint8_t attackHead;
        uint8_t attackCount;
    };

    static uint8_t indexOf(LimitChannel channel);

    void freeze(uint32_t nowMs);
    void enterFault();
    void evaluate(Channel& channel, int16_t angleDeci, uint32_t nowMs);
    void registerAttack(Channel& channel, uint32_t nowMs);
    void refreshCeiling(Channel& channel, uint32_t nowMs);
    uint16_t attacksInWindow(const Channel& channel, uint32_t nowMs) const;

    const IClock& clock_;
    Channel channels_[kLimitChannelCount];
    uint32_t lastUpdateMs_;
    uint8_t invalidRun_;
    bool linkFaulted_;
};

}  // namespace domain
