// RelayBankGpio: escrita dos quatro reles de limite da folha 2/2 (RL5, RL4, RL3 e RL2 sobre
// BC337, bornes CN1D..CN1K, jumpers J10/J9/J8/J2), por board::kRelayPins e board::kRelayMap.
// Do driver de fabrica validado em bancada (src/drivers/relays.cpp) sobra o que ele prova: o
// digitalWrite do nivel seguro ANTES do pinMode(OUTPUT), para que a habilitacao da saida nao
// produza pulso nas bases dos BC337. O que a porta exige a mais - escrita ATOMICA dos quatro
// canais e caminho executavel com a cache de instrucoes desligada - nao cabe no HAL Arduino e
// por isso o regime normal escreve REGISTRADOR (GPIO.out_w1ts/out_w1tc para IO25/IO26,
// GPIO.out1_w1ts/out1_w1tc para IO32/IO33). Erro por Status/Err, sem heap e sem impressao.
// Decisao A1: o nivel de bobina vem de domain::coilLevel(estado, polaridade) e a polaridade vem
// do construtor (urbase::kRelayFailSafePolarity); nao ha polaridade nem regra de limite aqui.
// Base comum, passo 2 da ordem de boot: begin() roda logo apos o watchdog, antes de NVS,
// display, botoes e RS-485 (orcamento de 0,2 ms; o pior caso medido deste arquivo e de dezenas
// de microssegundos, sem delay() e sem espera de barramento).
// Decisao 16: sem realimentacao de contato - mask_ e cache de COMANDO, nao medicao. A releitura
// dos registradores no begin() prova o LATCH DE SAIDA do ESP32, nada alem dele.
#include "adapters/relay_bank_gpio.h"

#include <Arduino.h>
#include <soc/gpio_struct.h>

#include "domain/limit_rule.h"

namespace {

// TERMINOLOGIA DE PRODUTO, PENDENCIA DE COORDENACAO: "X1, X2, Y1, Y2" e a nomenclatura do manual
// 5.9 L202 (limites 1 e 2 do eixo X, 3 e 4 do eixo Y), nao um fato de hardware - o hardware desta
// camada conhece IO32/IO26/IO25/IO33, RL5..RL2 e CN1D..CN1K, e nada mais. A mesma tabela existe
// hoje no dominio (src/domain/ui/menu_machine.cpp, kEtiquetaLimite) e a duplicacao e defeito
// aceito temporariamente: o destino e uma unica domain::limitChannelLabel() em
// src/domain/limit_rule.h, consumida pela IHM e por channelName(). limit_rule.h e menu_machine.cpp
// sao de OUTRO dono; enquanto a funcao nao existir la, esta tabela e a copia do adaptador e
// qualquer errata de rotulo do manual tem de mudar as DUAS - sob pena de a IHM e o roteiro de
// fabrica chamarem o mesmo rele por nomes diferentes.
constexpr const char* kChannelName[kLimitChannelCount] = {"X1", "X2", "Y1", "Y2"};
constexpr const char* kUnknownChannelName = "??";

// Sentinela de canal desconhecido, coerente com pin() -> board::kNoPin: info() nunca devolve a
// linha de OUTRO rele. Devolver kRelayMap[0] por engano manda o tecnico ao borne CN1D/CN1E e ao
// jumper J10 de um equipamento de seguranca portuaria. Duracao estatica, sem heap.
constexpr board::RelayMap kUnknownRelayMap{"??", board::kNoPin, "??", "??", "??", "??"};

constexpr RelayMask bitOf(uint8_t index) {
    return static_cast<RelayMask>(1u << index);
}

// IO34..IO39 do ESP32 NAO tem driver de saida: pinMode(OUTPUT) neles nao reprova e a escrita em
// GPIO.out1_w1ts nao move pino nenhum - um rele de seguranca ficaria mudo em silencio, e nem a
// releitura de GPIO.enable1 denunciaria. Por isso a faixa aceita e IO0..IO33, nao IO0..IO39.
constexpr bool relayPinsAreDrivable() {
    for (uint8_t i = 0; i < board::kRelayCount; ++i) {
        if (board::kRelayPins[i] < 0 || board::kRelayPins[i] > 33) {
            return false;
        }
    }
    return true;
}

}  // namespace

static_assert(board::kRelayCount == kLimitChannelCount,
              "board::kRelayPins tem de cobrir exatamente os canais de LimitChannel");
static_assert(relayPinsAreDrivable(),
              "pino de rele fora de IO0..IO33: sem driver de saida, a escrita nao move o rele");

RelayBankGpio::RelayBankGpio(bool failSafePolarity)
    : bank0Bit_{0, 0, 0, 0},
      bank1Bit_{0, 0, 0, 0},
      mask_(kRelayMaskAllSignalled),
      failSafePolarity_(failSafePolarity),
      signalledLevel_(domain::coilLevel(RelayState::Signalled, failSafePolarity)),
      configured_(false),
      ready_(false) {
    // Uma unica leitura de board::kRelayPins (constexpr, .rodata, ou seja flash) em toda a vida
    // do objeto, aqui no construtor: dai em diante o caminho de escrita usa so estes membros e
    // pode rodar de IRAM com a cache desligada.
    for (uint8_t i = 0; i < kLimitChannelCount; ++i) {
        const uint8_t p = static_cast<uint8_t>(board::kRelayPins[i]);
        if (p < 32u) {
            bank0Bit_[i] = 1u << p;
        } else {
            bank1Bit_[i] = 1u << (p - 32u);
        }
    }
}

bool RelayBankGpio::indexOf(LimitChannel channel, uint8_t& index) {
    const uint8_t raw = static_cast<uint8_t>(channel);
    if (raw >= kLimitChannelCount) {
        return false;
    }
    index = raw;
    return true;
}

// Escrita ATOMICA dos quatro canais, conforme IRelayBank::applyMask. Dois bancos de GPIO, ate
// quatro stores, skew de dezenas de nanossegundos - varias ordens de grandeza abaixo do tempo de
// operacao do AX1RC-5V. O nivel de ALARME e escrito PRIMEIRO, de modo que o instante intermediario
// entre os stores erre sempre para o lado sinalizado (decisao 8 item H), qualquer que seja a
// polaridade fechada em A1.
void IRAM_ATTR RelayBankGpio::driveMask(RelayMask wanted) const {
    uint32_t high0 = 0, low0 = 0, high1 = 0, low1 = 0;
    for (uint8_t i = 0; i < kLimitChannelCount; ++i) {
        const bool signalled = ((wanted & bitOf(i)) != 0);
        const bool level = signalled ? signalledLevel_ : !signalledLevel_;
        if (level) {
            high0 |= bank0Bit_[i];
            high1 |= bank1Bit_[i];
        } else {
            low0 |= bank0Bit_[i];
            low1 |= bank1Bit_[i];
        }
    }
    if (signalledLevel_) {
        GPIO.out_w1ts = high0;
        GPIO.out1_w1ts.val = high1;
        GPIO.out_w1tc = low0;
        GPIO.out1_w1tc.val = low1;
    } else {
        GPIO.out_w1tc = low0;
        GPIO.out1_w1tc.val = low1;
        GPIO.out_w1ts = high0;
        GPIO.out1_w1ts.val = high1;
    }
}

// Releitura do que o ESP32 sabe dizer de volta: (a) o driver de saida dos quatro pinos esta
// HABILITADO (GPIO_ENABLE), ou seja, o pinMode(OUTPUT) valeu, e (b) o registrador de saida
// (GPIO_OUT) guarda o nivel escrito. NAO e realimentacao de contato (decisao 16): bobina, BC337 e
// contato continuam indetectaveis. E o maximo que begin() consegue PROVAR antes de devolver kOk.
bool RelayBankGpio::latchMatches(RelayMask wanted) const {
    uint32_t care0 = 0, care1 = 0, want0 = 0, want1 = 0;
    for (uint8_t i = 0; i < kLimitChannelCount; ++i) {
        const bool signalled = ((wanted & bitOf(i)) != 0);
        const bool level = signalled ? signalledLevel_ : !signalledLevel_;
        care0 |= bank0Bit_[i];
        care1 |= bank1Bit_[i];
        if (level) {
            want0 |= bank0Bit_[i];
            want1 |= bank1Bit_[i];
        }
    }
    if ((GPIO.enable & care0) != care0 || (GPIO.enable1.val & care1) != care1) {
        return false;
    }
    return ((GPIO.out & care0) == want0) && ((GPIO.out1.val & care1) == want1);
}

Status RelayBankGpio::begin() {
    // PASSO 2 DA ORDEM DE BOOT: o nivel escrito aqui e o NIVEL DE BOOT da base comum
    // (urbase::kRelayBootLevel = false), que e LOW NAS DUAS POLARIDADES porque e o estado de
    // hardware do reset - o pull-down de 1K na base do BC337 ja mantem a bobina desenergizada.
    // NAO e o nivel de "Signalled": os dois so coincidem quando failSafePolarity_ e true. Com a
    // opcao A de A1 (fidelidade ao manual, failSafePolarity_ = false) signalledLevel_ vira HIGH
    // e escrever "Signalled" no boot ENERGIZARIA as quatro bobinas em toda energizacao - 144 mA
    // de surto somados aos ~226 ms mais criticos do boot num orcamento de fonte de 5 W ja
    // apertado, quatro reles chaveando por ciclo de energia, e os quatro contatos de alarme
    // fechando sem que exista alarme angular, disparando o intertravamento do CLP. DECISIONS.md
    // 2.3 fecha a questao: "Em ambas as opcoes, o boot mantem os quatro reles desenergizados".
    // A direcao segura nao se perde: o LimitEvaluator nasce todo Signalled e o primeiro ciclo
    // da tarefa ctrl, <= 50 ms depois, leva os reles ao estado que ele quer.
    const bool kBootLevel = false;  // urbase::kRelayBootLevel
    // Mascara equivalente ao nivel de boot NA POLARIDADE VIGENTE, para que latchMatches()
    // confira contra o nivel realmente escrito.
    const RelayMask bootMask = signalledLevel_ ? kRelayMaskAllClear : kRelayMaskAllSignalled;

    // Antes do pinMode: e o par validado em bancada contra o glitch de habilitacao da saida.
    // Aqui a simultaneidade nao tem papel nenhum (o driver de saida ainda esta desligado), so o
    // valor do latch, entao vale o HAL do driver de fabrica.
    for (uint8_t i = 0; i < kLimitChannelCount; ++i) {
        digitalWrite(static_cast<uint8_t>(board::kRelayPins[i]), kBootLevel ? HIGH : LOW);
    }
    for (uint8_t i = 0; i < kLimitChannelCount; ++i) {
        pinMode(static_cast<uint8_t>(board::kRelayPins[i]), OUTPUT);
    }
    configured_ = true;
    driveMask(bootMask);
    mask_ = bootMask;
    if (!latchMatches(bootMask)) {
        // Nao ha como provar que este adaptador comanda os quatro pinos. O hardware fica no
        // nivel de boot que acabou de ser escrito, ready_ segue false e toda escrita posterior
        // devolve NotInit; a Application enxerga applyMask() e signalAll() reprovando no mesmo
        // ciclo, para de renovar o token de liveness e deixa o STWD100 resetar a placa.
        ready_ = false;
        return Status(Err::HwFault);
    }
    ready_ = true;
    return kOk;
}

uint8_t RelayBankGpio::count() const {
    return kLimitChannelCount;
}

Status RelayBankGpio::applyMask(RelayMask wanted) {
    if ((wanted & static_cast<RelayMask>(~kRelayMaskAllSignalled)) != 0) {
        return Status(Err::Param);
    }
    if (!ready_) {
        return Status(Err::NotInit);
    }
    driveMask(wanted);
    mask_ = wanted;
    return kOk;
}

Status RelayBankGpio::set(LimitChannel channel, RelayState relayState) {
    uint8_t index = 0;
    if (!indexOf(channel, index)) {
        return Status(Err::Param);
    }
    // Estado fora da enumeracao e REPROVADO, nunca interpretado: set() e a via de fabrica e de
    // diagnostico, ou seja, a via que recebe valor vindo do console. Sem esta recusa, pino e
    // cache decidiriam por criterios opostos - a bobina iria ao nivel de alarme e state()/mask()
    // reportariam Clear, que e a direcao insegura no OBSERVADOR (o CLP ve alarme e a UR se
    // declara sem alarme).
    if (relayState != RelayState::Clear && relayState != RelayState::Signalled) {
        return Status(Err::Param);
    }
    if (!ready_) {
        return Status(Err::NotInit);
    }
    // Um unico caminho de escrita: o pino e o cache leem a MESMA mascara. Os outros tres canais
    // sao reescritos no nivel que ja tinham comandado, o que nao produz transicao.
    const RelayMask wanted = (relayState == RelayState::Signalled)
                                 ? static_cast<RelayMask>(mask_ | bitOf(index))
                                 : static_cast<RelayMask>(mask_ & static_cast<RelayMask>(~bitOf(index)));
    driveMask(wanted);
    mask_ = wanted;
    return kOk;
}

RelayState RelayBankGpio::state(LimitChannel channel) const {
    uint8_t index = 0;
    if (!indexOf(channel, index)) {
        return RelayState::Signalled;
    }
    return ((mask_ & bitOf(index)) != 0) ? RelayState::Signalled : RelayState::Clear;
}

RelayMask RelayBankGpio::mask() const {
    return mask_;
}

Status RelayBankGpio::signalAll() {
    return applyMask(kRelayMaskAllSignalled);
}

void IRAM_ATTR RelayBankGpio::signalAllFromIsr() {
    if (!configured_) {
        return;
    }
    driveMask(kRelayMaskAllSignalled);
    mask_ = kRelayMaskAllSignalled;
}

bool RelayBankGpio::failSafeCoil() const {
    return failSafePolarity_;
}

bool RelayBankGpio::feedbackAvailable() const {
    return false;
}

const char* RelayBankGpio::channelName(LimitChannel channel) const {
    uint8_t index = 0;
    if (!indexOf(channel, index)) {
        return kUnknownChannelName;
    }
    return kChannelName[index];
}

board::Pin RelayBankGpio::pin(LimitChannel channel) const {
    uint8_t index = 0;
    if (!indexOf(channel, index)) {
        return board::kNoPin;
    }
    return board::kRelayPins[index];
}

const board::RelayMap& RelayBankGpio::info(LimitChannel channel) const {
    uint8_t index = 0;
    if (!indexOf(channel, index)) {
        return kUnknownRelayMap;
    }
    return board::kRelayMap[index];
}
