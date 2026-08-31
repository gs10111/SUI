// src/adapters/gpio_keypad.cpp
// Implementacao do adaptador de teclado do CN3 (DE-PURI-DI261924 REV A, folha 1/2).
//
// pinMode: INPUT_PULLUP so em UP = IO15. DOWN = IO34 e MENU = IO35 recebem INPUT porque sao
// INPUT-ONLY e ignoram o pull-up interno em silencio; o pull-up desses dois vem da placa de
// IHM. Chamar INPUT_PULLUP neles daria a falsa impressao de linha garantida.
//
// begin() NAO BLOQUEIA E NAO ENFILEIRA. Ele faz pinMode e UMA amostragem por tecla, que e o
// que o passo 11 da ordem de boot da base comum orca em 0,5 ms, e semeia so o NIVEL: down_,
// changeMs_ e sinceMs_. A fila sai de begin() VAZIA, exatamente como a do FakeKeypad, para
// que o mesmo dominio testado no host valha na placa. A acomodacao eletrica da linha fica
// com a maquina de debounce de 20 ms de poll(), que roda na tarefa "btn" a 5 ms (decisao 1
// item 5) e nao custa boot. Consequencia querida: bounces_ tambem sai de begin() em ZERO -
// o unico produtor de repique passa a ser poll(), com o criterio de "voltou ao nivel
// estavel", que e o que a porta define como "repiques rejeitados pelo debounce".
//
// SECAO CRITICA DE TAREFA: a fila e escrita por poll() (tarefa "btn") e lida por takeEvent()
// (loop()) - duas tarefas. count_ sofre read-modify-write dos dois lados e o Xtensa nao o faz
// atomicamente (l8ui/addi/s8i), entao uma preempcao perderia ou ressuscitaria bordas com
// droppedEvents() em zero. g_queueMux serializa fila, contadores e nivel debounced.
// portENTER_CRITICAL (nao a variante _ISR) porque os dois lados sao TAREFA; a secao e de
// dezenas de instrucoes, sem leitura de GPIO e sem chamada de relogio dentro dela.
//
// CARIMBO: atMs e o instante da transicao eletrica que sobreviveu ao debounce, nao o instante
// em que os 20 ms venceram. Assim heldMs e o intervalo real entre as duas bordas e o erro de
// carimbo cobrado pela medicao 15 da decisao 1 (<= 10 ms) nao carrega o atraso do filtro.
// heldMs so e preenchido em KeyEdge::Release e satura em 65535 ms.
//
// FILA CHEIA: a borda NOVA e descartada e droppedEvents() cresce - o mesmo que o
// FakeKeypad faz. Descartar a mais antiga, como fazia src/drivers/buttons.cpp, trocaria a
// ordem do gesto em silencio. flush() esvazia a fila sem mexer nos contadores;
// resetCounters() zera contadores sem esvaziar a fila.
#include "adapters/gpio_keypad.h"

#include <Arduino.h>

namespace {

struct KeyDesc {
    const char* label;
    board::Pin pin;
    bool internalPullup;
};

constexpr KeyDesc kKeys[kKeyCount] = {
    {"MENU", board::kBtnMenu, false},
    {"UP", board::kBtnUp, true},
    {"DOWN", board::kBtnDown, false},
};

constexpr uint16_t kHeldMsMax = 65535u;

// Serializa fila, contadores e nivel debounced entre a tarefa "btn" (poll) e o loop()
// (takeEvent/flush/resetCounters). Instancia unica de teclado no produto.
portMUX_TYPE g_queueMux = portMUX_INITIALIZER_UNLOCKED;

bool readDown(uint8_t i) {
    return digitalRead(static_cast<uint8_t>(kKeys[i].pin)) == LOW;
}

}  // namespace

GpioKeypad::GpioKeypad(const IClock& clock)
    : clock_(clock),
      queue_{},
      head_(0),
      count_(0),
      dropped_(0),
      down_{},
      raw_{},
      changeMs_{},
      sinceMs_{},
      bounces_{},
      ready_(false) {
    for (uint8_t i = 0; i < kKeyCount; ++i) {
        down_[i] = false;
        raw_[i] = false;
        changeMs_[i] = 0;
        sinceMs_[i] = 0;
        bounces_[i] = 0;
    }
}

uint8_t GpioKeypad::index(Key key) {
    return static_cast<uint8_t>(key);
}

bool GpioKeypad::indexOk(uint8_t i) {
    return i < kKeyCount;
}

void GpioKeypad::pushLocked(const KeyEvent& event) {
    if (count_ >= kQueueCap) {
        dropped_ = dropped_ + 1u;
        return;
    }
    queue_[(head_ + count_) % kQueueCap] = event;
    count_ = static_cast<uint8_t>(count_ + 1u);
}

Status GpioKeypad::begin() {
    // Passo 11 do boot: roda antes da criacao da tarefa "btn" (passo 13) e antes do loop(),
    // portanto sem concorrencia - nao ha secao critica aqui de proposito.
    ready_ = false;
    head_ = 0;
    count_ = 0;
    dropped_ = 0;

    const uint32_t startMs = clock_.nowMs();
    for (uint8_t i = 0; i < kKeyCount; ++i) {
        const uint8_t pinNum = static_cast<uint8_t>(kKeys[i].pin);
        pinMode(pinNum, kKeys[i].internalPullup ? INPUT_PULLUP : INPUT);
        const bool level = readDown(i);
        raw_[i] = level;
        down_[i] = level;
        changeMs_[i] = startMs;
        sinceMs_[i] = level ? startMs : 0u;
        bounces_[i] = 0;
    }

    ready_ = true;
    return kOk;
}

void GpioKeypad::poll() {
    if (!ready_) {
        return;
    }
    const uint32_t nowMs = clock_.nowMs();
    for (uint8_t i = 0; i < kKeyCount; ++i) {
        const bool rawDown = readDown(i);
        const bool stable = down_[i];
        if (rawDown != raw_[i]) {
            raw_[i] = rawDown;
            changeMs_[i] = nowMs;
            if (rawDown == stable) {
                // Voltou ao nivel estavel dentro da janela: repique rejeitado pelo debounce.
                portENTER_CRITICAL(&g_queueMux);
                bounces_[i] = bounces_[i] + 1u;
                portEXIT_CRITICAL(&g_queueMux);
            }
            continue;
        }
        if (rawDown == stable || !deadlineReached(changeMs_[i], nowMs, kDebounceMs)) {
            continue;
        }
        if (rawDown) {
            // Nivel e borda publicados na MESMA secao critica: o consumidor nunca ve um
            // Press sem pressed() ja verdadeiro, nem o contrario.
            portENTER_CRITICAL(&g_queueMux);
            down_[i] = true;
            sinceMs_[i] = changeMs_[i];
            pushLocked(KeyEvent{static_cast<Key>(i), KeyEdge::Press, changeMs_[i], 0});
            portEXIT_CRITICAL(&g_queueMux);
            continue;
        }
        const uint32_t heldMs = elapsedMs(sinceMs_[i], changeMs_[i]);
        const uint16_t held = static_cast<uint16_t>(heldMs > kHeldMsMax ? kHeldMsMax : heldMs);
        portENTER_CRITICAL(&g_queueMux);
        down_[i] = false;
        sinceMs_[i] = 0;
        pushLocked(KeyEvent{static_cast<Key>(i), KeyEdge::Release, changeMs_[i], held});
        portEXIT_CRITICAL(&g_queueMux);
    }
}

bool GpioKeypad::takeEvent(KeyEvent& out) {
    bool got = false;
    portENTER_CRITICAL(&g_queueMux);
    if (count_ != 0) {
        out = queue_[head_];
        head_ = static_cast<uint8_t>((head_ + 1) % kQueueCap);
        count_ = static_cast<uint8_t>(count_ - 1u);
        got = true;
    }
    portEXIT_CRITICAL(&g_queueMux);
    return got;
}

bool GpioKeypad::pressed(Key key) const {
    const uint8_t i = index(key);
    return indexOk(i) && down_[i];
}

uint8_t GpioKeypad::pressedMask() const {
    uint8_t mask = 0;
    for (uint8_t i = 0; i < kKeyCount; ++i) {
        if (down_[i]) {
            mask = static_cast<uint8_t>(mask | (1u << i));
        }
    }
    return mask;
}

uint32_t GpioKeypad::pressedForMs(Key key) const {
    const uint8_t i = index(key);
    if (!indexOk(i) || !down_[i]) {
        return 0;
    }
    return elapsedMs(sinceMs_[i], clock_.nowMs());
}

void GpioKeypad::flush() {
    portENTER_CRITICAL(&g_queueMux);
    head_ = 0;
    count_ = 0;
    portEXIT_CRITICAL(&g_queueMux);
}

uint16_t GpioKeypad::debounceMs() const {
    return kDebounceMs;
}

bool GpioKeypad::hasInternalPullup(Key key) const {
    const uint8_t i = index(key);
    return indexOk(i) && kKeys[i].internalPullup;
}

uint32_t GpioKeypad::bounceCount(Key key) const {
    const uint8_t i = index(key);
    return indexOk(i) ? bounces_[i] : 0u;
}

uint32_t GpioKeypad::droppedEvents() const {
    return dropped_;
}

void GpioKeypad::resetCounters() {
    portENTER_CRITICAL(&g_queueMux);
    dropped_ = 0;
    for (uint8_t i = 0; i < kKeyCount; ++i) {
        bounces_[i] = 0;
    }
    portEXIT_CRITICAL(&g_queueMux);
}

const char* GpioKeypad::keyName(Key key) const {
    const uint8_t i = index(key);
    return indexOk(i) ? kKeys[i].label : "?";
}

board::Pin GpioKeypad::pin(Key key) const {
    const uint8_t i = index(key);
    return indexOk(i) ? kKeys[i].pin : board::kNoPin;
}
