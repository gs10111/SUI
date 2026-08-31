// src/main.cpp
// COMPOSITION ROOT da Unidade Remota DE-PURI-DI261924 REV A (Supervisor de Inclinacao
// SUI-DI141388XY). Este e o UNICO arquivo do projeto que instancia classe concreta e o unico,
// junto com src/adapters/, autorizado a tocar hardware. Dominio e aplicacao recebem tudo por
// referencia; a guarda scripts/check_hexagonal.py reprova o build se um include de hardware ou
// um ponto flutuante aparecer em src/domain/, src/app/ ou src/ports/.
//
// ORDEM DE BOOT — DECISIONS.md 2.2, os treze passos na ordem escrita, com o orcamento de cada um:
//   1. WATCHDOG PRIMEIRO (0,5 ms). Stwd100Watchdog::begin() poe IO19 em nivel baixo, da um pulso
//      manual e arma a ISR de timer de HARDWARE em IRAM a 1 kHz, que chuta o WDI a cada 250 ms
//      por GPIO.out_w1ts/out_w1tc. NAO e esp_timer: a ESP_TIMER_TASK executa de flash e para
//      durante o apagamento de setor da NVS, quando a cache e desabilitada — exatamente a janela
//      em que o cachorro precisa continuar sendo alimentado. A ISR para de pulsar se a tarefa
//      ctrl nao renovar o token de liveness em 800 ms, para que o cachorro continue matando um
//      firmware travado.
//   2. RELES AO ESTADO DE BOOT (0,2 ms), antes de qualquer init lento. Nas duas polaridades o
//      nivel de boot e BAIXO, que e o estado de hardware do reset (pull-down de 1K na base do
//      BC337); com a polaridade fail-safe deste build isso ja e o estado de ALARME, correto
//      durante o boot.
//   3. LED LIG (IO2) EM NIVEL BAIXO (0,05 ms). Ainda nao acende: IO2 e strapping. Vira batimento
//      no passo 14.
//   4. CAPTURA DE BOOT (1 ms): esp_reset_reason() e amostragem crua de IO15/IO34/IO35 para a
//      guarda do gesto de Reset de Fabrica (decisao 1, itens 21 e 22).
//   5. DAC8562 + XTR300 (6 ms). O adaptador dirige OP_MODE (IO22) em nivel BAIXO = modo tensao,
//      abre o HSPI, reseta, liga a referencia interna com ganho 2 e escreve o CODIGO DE FALHA
//      3932 (-11,00 V) nos dois canais. NAO 0x0000, que vale -12,5 V e satura o XTR300, e NAO
//      0x8000, que vale 0,00 V e e a leitura legitima mais provavel de estrutura nivelada.
//   6. CONSOLE SERIAL 115200 (2 ms).
//   7. NVS (60 ms tipico, 800 ms no pior caso). Unico passo que desabilita a cache; sobrevive
//      porque o chute do passo 1 e ISR em IRAM.
//   8. PRE-RESERVA DO VSPI (0,2 ms): SPI.begin(kDispSclk, kDispMiso, kDispMosi, -1) com o
//      ANTES de qualquer begin() do display. E ESTE passo que impede o U8g2 de sequestrar o
//      MISO default do VSPI, que e o IO19 = WDI. Sem ele o display abre o barramento com quatro
//      pinos e o cachorro fica sem linha de alimentacao. Logo depois, drive reduzido em
//      IO18/IO23/IO4/IO5 (decisao 12 item 4) contra o degrau de terra no CN3-4.
//   9. DISPLAY (150 ms): u8g2.begin() e contraste 255.
//  10. rearmPin() (0,05 ms): cinto-e-suspensorio sobre o passo 8.
//  11. BOTOES (0,5 ms).
//  12. RS-485 (5 ms): UART2 em half-duplex a 19200 8N1. O g_link.begin() NAO roda aqui: ele e a
//      primeira instrucao de ctrlTask(), porque uart_driver_install() registra a ISR da UART2 NO
//      CORE DA TAREFA QUE CHAMA e o setup() e core 1 (loopTask do Arduino). A base comum
//      (DECISIONS.md 2.1 item 5) manda o driver ser instalado de dentro da tarefa ctrl, para a
//      ISR ficar afim ao core 0; o adaptador nao consegue detectar a chamada do core errado -
//      a afinidade some em silencio. Os 5 ms passam a ser custo do primeiro tick da ctrl.
//  13. TAREFA ctrl (1 ms): core 0, prioridade 5, stack 4096 B, vTaskDelayUntil de 50 ms. Daqui
//      em diante limites, reles e saida analogica estao VIVOS e cadenciados, independentemente
//      do que a IHM faca.
//  14. Fim do setup(): IO2 passa a batimento de 900/100 ms na mesma ISR do WDI, travado pelo
//      mesmo token de liveness (decisao 12 item 14), e o splash NAO BLOQUEANTE comeca no loop().
//
// DIVISAO DE TRABALHO. A tarefa ctrl e dona EXCLUSIVA da transacao Modbus, do filtro, dos quatro
// limites, dos quatro reles, das duas saidas analogicas e do token de liveness. O loop() fica com
// teclado, IHM, display e NVS e NUNCA escreve rele nem DAC: publica pedidos por seccao critica
// (portMUX) e a tarefa ctrl aplica no ciclo seguinte, em bloco (decisao 1 item 18, decisao 6
// item 2). A NVS e escrita SO pelo loop(), depois da publicacao, nunca antes: o que o operador
// acabou de ver entra em vigor em <= 50 ms mesmo que a flash demore.
//
// ARMAZENAMENTO. Uma escrita de NVS por passagem do loop(), NUNCA duas. A porta declara
// writeBudgetMs() = 250 ms por escrita e durante o apagamento de setor a cache de flash e
// desabilitada, o que para TODA tarefa que executa de flash, inclusive a ctrl no core 0. Gravar
// BankA e BankB na mesma passagem orcaria 500 ms de congelamento dos quatro reles, cinco vezes
// o que a base comum tolera sem fatiar (DECISIONS.md 2.1 item 5). Por isso publishAndPersist()
// so PUBLICA - o parametro entra em vigor em <= 50 ms de qualquer jeito - e marca o slot sujo;
// servicePersist() grava no maximo UM slot por passagem, e so o slot que mudou (Modo
// Programacao e PSET mexem em BankA, Auto Calibracao em BankB, Reset Geral nos dois).
// EXCECAO DE HEAP DECLARADA: write()/read()/erase() sao os UNICOS pontos de alocacao dinamica
// pos-setup() do produto, herdados do nvs_set_blob/nvs_get_blob do IDF por dentro do
// Preferences. A regra dura "sem heap depois do setup()" vale para o codigo do projeto, que nao
// tem um new/malloc/String sequer. O piso de heap e impresso no console apos cada gravacao para
// fechar a medicao 4 (mil ciclos de gravacao, aceitacao: minimum free heap estavel).
//
// BankA guarda o bloco de parametros do usuario (32 B) e BankB guarda o bloco de
// calibracao do usuario (20 B); FactoryCal e o registro do jig e a aplicacao NUNCA escreve nele —
// so o le, no Reset Geral, para devolver a calibracao de fabrica (manual 5.11 L240, decisao 1
// item 27). Sem bloco de parametros integro na energizacao o equipamento NAO carrega a Tabela 2
// em silencio (A8 / decisao 2 item 10): ele trava em falha, com os quatro reles em alarme e as
// duas saidas em 3932, e so o Reset Geral de 5.11 sai desse estado.
#include <Arduino.h>
#include <SPI.h>
#include <driver/gpio.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "adapters/esp_clock.h"
#include "adapters/gpio_keypad.h"
#include "adapters/modbus_sensor_link.h"
#include "adapters/nvs_parameter_store.h"
#include "adapters/relay_bank_gpio.h"
#include "adapters/ssd1322_display.h"
#include "adapters/stwd100_watchdog.h"
#include "adapters/xtr300_analog_output.h"
#include "app/application.h"
#include "app/boot_sequence.h"
#include "board_pins.h"
#include "domain/analog_calibration.h"
#include "domain/parameters.h"
#include "domain/password.h"
#include "domain/ui/calibration_wizard.h"
#include "domain/ui/key_gesture.h"
#include "domain/ui/menu_machine.h"
#include "domain/ui/normal_screen.h"
#include "domain/ui/preset_wizard.h"
#include "status.h"

namespace {

// Emenda 1 a A13: neste build de bancada o Modo Programacao abre sem senha. O BINARIO DE
// PRODUCAO USA true, e a troca e esta unica linha.
constexpr bool kRequirePassword = false;

// A1 / DECISIONS.md 2.3, recomendacao condicionada as medicoes 6, 7 e 12: true = fail-safe
// (bobina energizada e o estado saudavel; queda de energia sinaliza alarme). Uma unica constante
// inverte os quatro reles e os quatro LEDs do painel.
constexpr bool kRelayFailSafePolarity = true;

constexpr uint32_t kHmiPeriodMs = 50;
constexpr uint32_t kLoopSliceMs = 2;
constexpr uint32_t kMessageMs = 2000;
constexpr uint16_t kBlobCap = NvsParameterStore::kCapacityBytes;
constexpr int16_t kMessageY = 40;
constexpr int16_t kEditLineY = 40;
constexpr uint8_t kLineCap = 48;

// Brilho do painel: politica de IHM, nao do adaptador (ssd1322_display.h, decisao 12 item 6).
// O adaptador nao aplica contraste por conta propria; quem decide e este composition root.
constexpr uint8_t kDisplayContrast = 255;

constexpr const char* kMsgConfigLost = "CONFIG PERDIDA - REPROGRAMAR";
constexpr const char* kMsgConfigLostHint = "Reset Geral: 5.11";
constexpr const char* kMsgSaveFailed = "Falha de gravacao!";
constexpr const char* kMsgPsetOk = "PSET aplicado!";
constexpr const char* kMsgFactoryReset = "RESET DE FABRICA";

adapters::EspClock g_clock;
Stwd100Watchdog g_wdt;
RelayBankGpio g_relays(kRelayFailSafePolarity);
Xtr300AnalogOutput g_analog;
NvsParameterStore g_store;
adapters::ModbusSensorLink g_link(g_clock);
GpioKeypad g_keypad(g_clock);

void rearmWatchdogPin() {
    g_wdt.rearmPin();
}

adapters::Ssd1322Display g_display(&rearmWatchdogPin);

domain::Parameters g_params;
domain::Password g_password(g_clock);
domain::AnalogCalibration g_calibration;
domain::KeyGesture g_gesture(g_keypad, g_clock);
domain::NormalScreen g_normal(g_display, g_gesture);
domain::MenuMachine g_menu(g_display, g_clock, g_password, g_params, kRequirePassword);
domain::CalibrationWizard g_calWizard(g_calibration, g_clock);
domain::ui::PresetWizard g_preset(g_clock);

app::Application g_app(g_clock, g_link, g_relays, g_analog, g_wdt);
app::BootSequence g_boot(g_display, g_keypad, g_clock, FW_VERSION);

portMUX_TYPE g_pubMux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t g_ctrlTask = nullptr;

// Deslocamento anti-queima do painel (decisao 12 item 17): quatro fases de 1 px a cada 900000
// ms. O equipamento roda 24/7 com contraste 255 e a tela principal e praticamente estatica.
constexpr uint32_t kOriginShiftMs = 900000;
uint32_t g_originMs = 0;
uint8_t g_originPhase = 0;

// Slots sujos de NVS. Uma escrita por passagem do loop() (ver o bloco ARMAZENAMENTO).
constexpr uint8_t kDirtyParams = 1u << 0;
constexpr uint8_t kDirtyCal = 1u << 1;
uint8_t g_dirtySlots = 0;

// Status do g_link.begin() executado dentro da tarefa ctrl (passo 12), publicado para o
// console do core 1 poder imprimi-lo uma unica vez.
volatile Err g_linkBeginErr = Err::Ok;
volatile bool g_linkBeginDone = false;
bool g_linkReported = false;

uint8_t g_bootKeyMask = 0;
bool g_configLost = false;
bool g_configLostDrawn = false;
bool g_calActive = false;
bool g_presetEditing = false;
bool g_wasProgramming = false;
domain::Axis g_calAxis = domain::Axis::X;
domain::Axis g_presetAxis = domain::Axis::X;
uint32_t g_hmiMs = 0;
uint32_t g_messageUntilMs = 0;
const char* g_messageText = nullptr;

uint8_t sampleBootKeys() {
    pinMode(static_cast<uint8_t>(board::kBtnUp), INPUT_PULLUP);
    pinMode(static_cast<uint8_t>(board::kBtnDown), INPUT);
    pinMode(static_cast<uint8_t>(board::kBtnMenu), INPUT);
    uint8_t mask = 0;
    if (digitalRead(static_cast<uint8_t>(board::kBtnUp)) == LOW) {
        mask = static_cast<uint8_t>(mask | app::BootSequence::kMaskUp);
    }
    if (digitalRead(static_cast<uint8_t>(board::kBtnDown)) == LOW) {
        mask = static_cast<uint8_t>(mask | app::BootSequence::kMaskDown);
    }
    if (digitalRead(static_cast<uint8_t>(board::kBtnMenu)) == LOW) {
        mask = static_cast<uint8_t>(mask | app::BootSequence::kMaskMenu);
    }
    return mask;
}

void softenDisplayBus() {
    gpio_set_drive_capability(static_cast<gpio_num_t>(board::kDispSclk), GPIO_DRIVE_CAP_0);
    gpio_set_drive_capability(static_cast<gpio_num_t>(board::kDispMosi), GPIO_DRIVE_CAP_0);
    gpio_set_drive_capability(static_cast<gpio_num_t>(board::kDispDc), GPIO_DRIVE_CAP_0);
    gpio_set_drive_capability(static_cast<gpio_num_t>(board::kDispCs), GPIO_DRIVE_CAP_0);
}

bool loadParameters() {
    uint8_t blob[kBlobCap];
    uint16_t len = 0;
    bool paramsOk = false;
    if (g_store.exists(ParamSlot::BankA) &&
        g_store.read(ParamSlot::BankA, blob, kBlobCap, len).ok()) {
        paramsOk = g_params.loadParams(blob, len).ok();
    }
    if (!paramsOk) {
        return false;
    }
    len = 0;
    if (g_store.exists(ParamSlot::BankB) &&
        g_store.read(ParamSlot::BankB, blob, kBlobCap, len).ok()) {
        if (!g_params.loadCal(blob, len).ok()) {
            return false;
        }
        return true;
    }
    len = 0;
    if (g_store.exists(ParamSlot::FactoryCal) &&
        g_store.read(ParamSlot::FactoryCal, blob, kBlobCap, len).ok()) {
        return g_params.loadCal(blob, len).ok();
    }
    return false;
}

// Piso de heap depois de cada gravacao: e o criterio da medicao 4. A NVS do IDF aloca e libera
// por dentro a cada nvs_set_blob; se o minimum free heap descer sem parar, a saida e pre-abrir o
// handle e trocar Preferences por nvs_set_blob direto sobre o buffer estatico do adaptador.
void reportHeap(const char* what) {
    Serial.print(F("nvs "));
    Serial.print(what);
    Serial.print(F(" heap="));
    Serial.print(esp_get_free_heap_size());
    Serial.print(F(" min="));
    Serial.println(esp_get_minimum_free_heap_size());
}

bool persistParams() {
    uint8_t blob[kBlobCap];
    uint16_t len = 0;
    if (g_params.serializeParams(blob, kBlobCap, len).failed()) {
        return false;
    }
    if (g_store.write(ParamSlot::BankA, blob, len).failed()) {
        return false;
    }
    reportHeap("A");
    return true;
}

bool persistCal() {
    uint8_t blob[kBlobCap];
    uint16_t len = 0;
    if (g_params.serializeCal(blob, kBlobCap, len).failed()) {
        return false;
    }
    if (g_store.write(ParamSlot::BankB, blob, len).failed()) {
        return false;
    }
    reportHeap("B");
    return true;
}

void publishParameters() {
    taskENTER_CRITICAL(&g_pubMux);
    g_app.publishParameters(g_params);
    taskEXIT_CRITICAL(&g_pubMux);
}

void publishOverride(domain::Axis axis, uint16_t code) {
    taskENTER_CRITICAL(&g_pubMux);
    g_app.requestAnalogOverride(axis, code);
    taskEXIT_CRITICAL(&g_pubMux);
}

void publishOverrideClear(domain::Axis axis) {
    taskENTER_CRITICAL(&g_pubMux);
    g_app.clearAnalogOverride(axis);
    taskEXIT_CRITICAL(&g_pubMux);
}

void publishConfigLatch(bool latched) {
    taskENTER_CRITICAL(&g_pubMux);
    g_app.setConfigLatched(latched);
    taskEXIT_CRITICAL(&g_pubMux);
}

app::Application::Snapshot takeSnapshot() {
    taskENTER_CRITICAL(&g_pubMux);
    const app::Application::Snapshot snap = g_app.snapshot();
    taskEXIT_CRITICAL(&g_pubMux);
    return snap;
}

void showMessage(const char* text) {
    g_messageText = text;
    g_messageUntilMs = g_clock.nowMs() + kMessageMs;
}

// Publica AGORA (a tarefa ctrl aplica no ciclo seguinte, <= 50 ms) e so marca a flash como
// suja. Quem grava e servicePersist(), no maximo um slot por passagem do loop().
void publishAndPersist(uint8_t slots) {
    publishParameters();
    g_dirtySlots = static_cast<uint8_t>(g_dirtySlots | slots);
}

void servicePersist() {
    if (g_dirtySlots == 0u) {
        return;
    }
    bool ok = true;
    if ((g_dirtySlots & kDirtyParams) != 0u) {
        g_dirtySlots = static_cast<uint8_t>(g_dirtySlots & ~kDirtyParams);
        ok = persistParams();
    } else {
        g_dirtySlots = static_cast<uint8_t>(g_dirtySlots & ~kDirtyCal);
        ok = persistCal();
    }
    if (!ok) {
        g_dirtySlots = 0;
        showMessage(kMsgSaveFailed);
    }
}

void applyFactoryReset() {
    domain::Parameters defaults = domain::Parameters::factoryDefaults();
    uint8_t blob[kBlobCap];
    uint16_t len = 0;
    bool calRestored = false;
    if (g_store.exists(ParamSlot::FactoryCal) &&
        g_store.read(ParamSlot::FactoryCal, blob, kBlobCap, len).ok()) {
        calRestored = defaults.loadCal(blob, len).ok();
    }
    if (!calRestored && !g_configLost) {
        for (uint8_t i = 0; i < domain::Parameters::kAxisCount; ++i) {
            const domain::Axis axis = static_cast<domain::Axis>(i);
            defaults.setCalPair(axis, g_params.calZeroCode(axis), g_params.calFullScaleCode(axis));
            defaults.setCalFullScale(axis, g_params.calFullScale(axis));
        }
    }
    g_params = defaults;
    g_password.load(g_params.password());
    g_configLost = false;
    publishConfigLatch(false);
    // Reset Geral e o unico caminho que suja os dois slots. Fatiado em duas passagens do
    // loop(), nunca as tres escritas de 250 ms em sequencia que encostariam nos 800 ms do
    // token de liveness.
    publishAndPersist(kDirtyParams | kDirtyCal);
    g_configLostDrawn = false;
    showMessage(kMsgFactoryReset);
}

void drawCenteredLine(int16_t y, const char* text, TextFont font) {
    const uint16_t width = g_display.textWidthPx(font, text);
    const uint16_t screen = g_display.widthPx();
    const int16_t x = (width < screen) ? static_cast<int16_t>((screen - width) / 2u) : 0;
    g_display.drawText(x, y, text, font, TextInk::Normal);
}

void renderMessage(const char* text) {
    g_display.clear();
    drawCenteredLine(kMessageY, text, TextFont::Large);
    g_display.present();
}

void renderConfigLost() {
    g_display.clear();
    drawCenteredLine(24, kMsgConfigLost, TextFont::Small);
    drawCenteredLine(48, kMsgConfigLostHint, TextFont::Small);
    g_display.present();
}

domain::NormalLinkState mapLink(app::LinkHealth health, bool stale) {
    // Guarda dura de idade ligada = os quatro reles ja estao em alarme e as duas saidas em
    // 3932. O painel nao pode dizer "Ok" por cima disso: e a mesma mentira que a base rejeitou
    // ao proibir 0,00 V como nivel de falha da analogica.
    if (stale) {
        return domain::NormalLinkState::CommFault;
    }
    switch (health) {
        case app::LinkHealth::Ok: return domain::NormalLinkState::Ok;
        case app::LinkHealth::Awaiting: return domain::NormalLinkState::Awaiting;
        case app::LinkHealth::SensorFault: return domain::NormalLinkState::SensorFault;
        case app::LinkHealth::CommFault: break;
    }
    return domain::NormalLinkState::CommFault;
}

domain::NormalInput buildNormalInput(const app::Application::Snapshot& snap) {
    domain::NormalInput in{};
    for (uint8_t i = 0; i < domain::kNormalAxisCount; ++i) {
        const domain::Axis axis = static_cast<domain::Axis>(i);
        in.reading[i] = snap.reading[i];
        in.presetOffsetDeci[i] = g_params.presetOffsetDeci(axis);
        in.presetActive[i] = in.presetOffsetDeci[i] != 0;
        if (snap.overriding[i]) {
            in.analog[i] = domain::NormalAnalogMode::Calibrating;
        } else if (snap.link == app::LinkHealth::Ok && !snap.stale) {
            in.analog[i] = domain::NormalAnalogMode::Tracking;
        } else {
            in.analog[i] = domain::NormalAnalogMode::Fault;
        }
    }
    for (uint8_t i = 0; i < kLimitChannelCount; ++i) {
        in.limit[i].state = snap.limitState[i];
        in.limit[i].value = g_params.limitValue(static_cast<domain::LimitId>(i));
    }
    in.link = mapLink(snap.link, snap.stale);
    in.linkLatched = snap.configLatched;
    in.heartbeatPhase = static_cast<uint8_t>((snap.cycles / 10u) % domain::NormalScreen::kHeartbeatPhases);
    return in;
}

void startAssistant(domain::MenuAction action, const app::Application::Snapshot& snap) {
    switch (action) {
        case domain::MenuAction::AjustaPresetX:
        case domain::MenuAction::AjustaPresetY:
            g_presetAxis = (action == domain::MenuAction::AjustaPresetX) ? domain::Axis::X
                                                                        : domain::Axis::Y;
            g_presetEditing = g_preset.beginEdit(g_presetAxis, g_params);
            if (!g_presetEditing) {
                g_menu.reclaimDisplay();
            }
            break;
        case domain::MenuAction::AutoCalibracaoX:
        case domain::MenuAction::AutoCalibracaoY: {
            g_calAxis = (action == domain::MenuAction::AutoCalibracaoX) ? domain::Axis::X
                                                                       : domain::Axis::Y;
            const bool signalled = snap.relayMask != kRelayMaskAllClear;
            if (g_calWizard.begin(g_calAxis, signalled).failed()) {
                g_menu.reclaimDisplay();
                break;
            }
            g_calActive = true;
            g_gesture.flush();
            break;
        }
        case domain::MenuAction::None: break;
    }
}

void finishCalibration() {
    const domain::AnalogScaler& scaler = g_calibration.scaler();
    if (g_calWizard.step() == domain::CalibrationWizard::Step::Done) {
        g_params.setCalPair(g_calAxis, scaler.zeroCode(), scaler.fullScaleCode());
        g_params.setCalFullScale(g_calAxis,
                                 domain::Angle::fromDeciDegrees(scaler.fullScaleAngleDeci()));
        publishAndPersist(kDirtyCal);
    }
    publishOverrideClear(g_calAxis);
    g_calActive = false;
    g_menu.reclaimDisplay();
}

void serviceCalibration() {
    domain::Gesture gesture{};
    while (g_gesture.takeGesture(gesture)) {
        g_calWizard.onGesture(gesture);
    }
    g_calWizard.tick();
    if (g_calWizard.consumeFlushRequest()) {
        g_gesture.flush();
        g_keypad.flush();
    }
    if (g_calWizard.overriding()) {
        publishOverride(g_calAxis, g_calWizard.outputCode());
    } else {
        publishOverrideClear(g_calAxis);
    }
    g_calWizard.render(g_display);
    if (!g_calWizard.active()) {
        finishCalibration();
    }
}

void renderPresetEdit() {
    char field[domain::DigitEditor::kTextCap];
    if (!g_preset.formatEdit(field, domain::DigitEditor::kTextCap)) {
        return;
    }
    char line[kLineCap];
    const char* prefix = (g_presetAxis == domain::Axis::X) ? "Preset X:" : "Preset Y:";
    uint8_t used = 0;
    for (; prefix[used] != '\0' && used < (kLineCap - 1u); ++used) {
        line[used] = prefix[used];
    }
    const uint8_t prefixLen = used;
    for (uint8_t i = 0; field[i] != '\0' && used < (kLineCap - 1u); ++i, ++used) {
        line[used] = field[i];
    }
    line[used] = '\0';
    g_display.clear();
    g_display.drawText(0, kEditLineY, line, TextFont::Small, TextInk::Normal);
    const uint8_t cursor = static_cast<uint8_t>(prefixLen + g_preset.editCursorTextIndex());
    if (cursor < used) {
        char head[kLineCap];
        for (uint8_t i = 0; i < cursor; ++i) {
            head[i] = line[i];
        }
        head[cursor] = '\0';
        const char glyph[2] = {line[cursor], '\0'};
        g_display.drawText(static_cast<int16_t>(g_display.textWidthPx(TextFont::Small, head)),
                           kEditLineY, glyph, TextFont::Small, TextInk::Inverse);
    }
    g_display.present();
}

void servicePresetEdit() {
    domain::Gesture gesture{};
    while (g_gesture.takeGesture(gesture)) {
        if (gesture.kind == domain::GestureKind::Hold && gesture.key == Key::Menu) {
            if (g_preset.editConfirm() != domain::ConfirmResult::Ok) {
                showMessage(g_preset.editOutOfRangeMessage());
                continue;
            }
            const Status st = g_preset.commitEdit(g_params);
            g_presetEditing = false;
            if (st.ok()) {
                publishAndPersist(kDirtyParams);
            } else {
                showMessage(kMsgSaveFailed);
            }
            g_menu.reclaimDisplay();
            return;
        }
        if (gesture.kind == domain::GestureKind::Hold && gesture.key == Key::Down) {
            g_preset.cancelEdit();
            g_presetEditing = false;
            g_menu.reclaimDisplay();
            return;
        }
        if (gesture.kind != domain::GestureKind::ShortTap) {
            continue;
        }
        switch (gesture.key) {
            case Key::Menu: g_preset.editMenu(); break;
            case Key::Up: g_preset.editUp(); break;
            case Key::Down: g_preset.editDown(); break;
        }
    }
    renderPresetEdit();
}

void servicePsetConfirm() {
    domain::Gesture gesture{};
    while (g_gesture.takeGesture(gesture)) {
        if (gesture.kind == domain::GestureKind::Hold && gesture.key == Key::Menu) {
            if (g_preset.confirmPset(g_params) == domain::ui::PsetOutcome::Applied) {
                publishAndPersist(kDirtyParams);
                showMessage(kMsgPsetOk);
            }
            return;
        }
        if (gesture.kind == domain::GestureKind::ShortTap && gesture.key == Key::Down) {
            g_preset.cancelPset();
            return;
        }
    }
    char line[kLineCap];
    if (g_preset.formatPendingConfirm(g_presetAxis, line, kLineCap)) {
        g_display.clear();
        g_display.drawText(0, kEditLineY, line, TextFont::Small, TextInk::Normal);
        drawCenteredLine(60, domain::ui::PresetWizard::kConfirmHintText, TextFont::Small);
        g_display.present();
    }
}

void requestPset() {
    const domain::ui::PsetOutcome outcome = g_preset.requestPset(g_params);
    switch (outcome) {
        case domain::ui::PsetOutcome::Applied:
            publishAndPersist(kDirtyParams);
            showMessage(kMsgPsetOk);
            break;
        case domain::ui::PsetOutcome::RefusedNoData:
            showMessage(domain::ui::PresetWizard::kRefusedNoDataText);
            break;
        case domain::ui::PsetOutcome::RefusedUnstable:
            showMessage(domain::ui::PresetWizard::kRefusedUnstableText);
            break;
        case domain::ui::PsetOutcome::NeedsConfirm:
        case domain::ui::PsetOutcome::Ignored: break;
    }
}

void serviceHmi() {
    const app::Application::Snapshot snap = takeSnapshot();
    g_preset.sample(snap.raw[0], snap.raw[1]);
    g_preset.tick();

    if (g_calActive) {
        serviceCalibration();
        return;
    }
    if (g_presetEditing) {
        servicePresetEdit();
        return;
    }

    const bool programming = g_menu.state() != domain::MenuState::Normal;
    if (g_menu.ownsDisplay()) {
        domain::Gesture gesture{};
        while (g_gesture.takeGesture(gesture)) {
            g_menu.onGesture(gesture);
        }
        g_menu.update();
        domain::MenuAction action = domain::MenuAction::None;
        if (g_menu.takeAction(action)) {
            startAssistant(action, snap);
        }
        g_wasProgramming = programming;
        return;
    }
    g_menu.update();
    if (g_wasProgramming && g_menu.state() == domain::MenuState::Normal) {
        g_preset.onProgrammingExit();
        publishAndPersist(kDirtyParams);
    }
    g_wasProgramming = false;

    if (g_messageText != nullptr) {
        if (static_cast<int32_t>(g_clock.nowMs() - g_messageUntilMs) < 0) {
            renderMessage(g_messageText);
            return;
        }
        g_messageText = nullptr;
    }
    if (g_preset.awaitingConfirm()) {
        servicePsetConfirm();
        return;
    }

    const domain::NormalInput in = buildNormalInput(snap);
    switch (g_normal.update(in)) {
        case domain::NormalRequest::OpenLogin: {
            const domain::Gesture open{domain::GestureKind::Hold, Key::Menu, g_clock.nowMs()};
            g_menu.onGesture(open);
            break;
        }
        case domain::NormalRequest::Preset: requestPset(); break;
        case domain::NormalRequest::SelfTest: g_boot.beginOnDemand(); break;
        case domain::NormalRequest::None: break;
    }
}

void ctrlTask(void* argument) {
    (void)argument;

    // PASSO 12 DA ORDEM DE BOOT, EXECUTADO AQUI E NAO NO setup(): uart_driver_install()
    // registra a ISR da UART2 no core da tarefa que chama, e a ctrl vive no core 0. Chamado do
    // setup() (loopTask, core 1) o begin() NAO devolve erro - a afinidade some em silencio e a
    // ISR de RX do RS-485 passa a disputar o core que empurra 2048 B de framebuffer e escreve
    // NVS, justamente as cargas que a base comum tirou do caminho de seguranca. Se o begin()
    // reprovar, request() devolve NotInit, o ciclo conta transacao invalida e os quatro reles
    // vao a alarme em 150 ms - que e o comportamento correto.
    g_linkBeginErr = g_link.begin().err;
    g_linkBeginDone = true;

    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(app::Application::kCyclePeriodMs);
    for (;;) {
        // Metade de entrada da travessia de nucleo: a copia do conjunto publicado pela IHM
        // acontece SOB O MESMO portMUX que o loop() toma para publicar. A seccao cobre a copia
        // do Parameters e o recalculo das quatro regras e das duas escalas - aritmetica
        // inteira, alguns microssegundos. Envolver startCycle()/pollCycle() desabilitaria a
        // interrupcao do core 0 por milissegundos, com a transacao Modbus inteira dentro.
        taskENTER_CRITICAL(&g_pubMux);
        g_app.applyPublished();
        taskEXIT_CRITICAL(&g_pubMux);

        g_app.startCycle();
        while (!g_app.pollCycle()) {
            vTaskDelay(1);
        }
        g_app.finishCycle();

        // Metade de saida: o quadro que a IHM le e latchado aqui, inteiro, sob o mesmo mux.
        taskENTER_CRITICAL(&g_pubMux);
        g_app.latchSnapshot();
        taskEXIT_CRITICAL(&g_pubMux);

        // REANCORAGEM CONTRA A RAJADA DE RECUPERACAO. xTaskDelayUntil, quando o instante de
        // acordar ja passou, NAO bloqueia: ele soma um incremento a 'last' e retorna na hora.
        // Depois de um bloqueio de 500 ms (a janela de cache-off da NVS orcada pela decisao 2
        // item 16) isso dispara dez ciclos colados, dez transacoes Modbus sem o silencio de
        // 28 ms que a base derivou como propriedade do enlace, e o enquadramento de 3,5
        // caracteres do escravo passa a depender de sorte - transacoes reprovadas em sequencia
        // sao o gatilho de kFailsToFault e levam os quatro reles a alarme por artefato de
        // escalonamento. Reancorando, um atraso isolado custa UM tick, que e o que a base
        // manda tolerar. noteStall() e quem decide se o atraso passou do orcamento do commit.
        const TickType_t nowTick = xTaskGetTickCount();
        if (static_cast<uint32_t>(nowTick - last) > static_cast<uint32_t>(2u * period)) {
            g_app.noteStall(static_cast<uint32_t>(nowTick - last) * portTICK_PERIOD_MS);
            vTaskDelay(period);
            last = xTaskGetTickCount();  // reancora no instante real de acordar
            continue;
        }
        vTaskDelayUntil(&last, period);
    }
}

}  // namespace

void setup() {
    const Status wdtStatus = g_wdt.begin();

    // RelayBankGpio::begin() e o UNICO ponto do produto que consegue PROVAR alguma coisa sobre
    // os quatro reles (releitura de GPIO_ENABLE e GPIO_OUT). Descartar esse Status era jogar
    // fora a unica prova que existe.
    const Status relayStatus = g_relays.begin();

    pinMode(static_cast<uint8_t>(board::kLedTest), OUTPUT);
    digitalWrite(static_cast<uint8_t>(board::kLedTest), LOW);

    const esp_reset_reason_t resetReason = esp_reset_reason();
    // O carimbo sai JUNTO com a mascara, e nao no fim do setup(): o item 23 da decisao 1 conta
    // os 3000 ms do gesto de Reset de Fabrica (e os 13000 ms do aborto por tecla presa) da
    // ENTRADA do setup(), e o setup orça 231 ms tipicos e 971 ms com NVS virgem. Carimbando no
    // fim, o operador teria de segurar UP por ate 3,97 s e a classe mediria uma janela cuja
    // origem nao e a origem da amostragem que a arma.
    const uint32_t bootAtMs = g_clock.nowMs();
    g_bootKeyMask = sampleBootKeys();

    const Status analogStatus = g_analog.begin();

    Serial.begin(board::kConsoleBaud);
    Serial.println();
    Serial.print(F("DE-PURI-DI261924 REV "));
    Serial.print(F(BOARD_REV));
    Serial.print(F(" FW "));
    Serial.println(F(FW_VERSION));
    Serial.print(F("reset="));
    Serial.print(static_cast<int>(resetReason));
    Serial.print(F(" wdt="));
    Serial.print(errName(wdtStatus.err));
    Serial.print(F(" rly="));
    Serial.print(errName(relayStatus.err));
    Serial.print(F(" dac="));
    Serial.println(errName(analogStatus.err));
    if (relayStatus.failed()) {
        Serial.println(F("BANCO DE RELES NAO COMANDAVEL - escritas vao reprovar e o STWD100 "
                         "reseta a placa"));
    }

    const Status storeStatus = g_store.begin();
    g_configLost = storeStatus.failed() || !loadParameters();
    if (g_configLost) {
        Serial.println(F("CONFIG PERDIDA - reles em alarme, saidas em 3932"));
    }
    g_password.load(g_params.password());

    // MISO = board::kDispMiso (IO39), NUNCA -1. Passar -1 nao desliga o MISO: o core do
    // ESP32 substitui pelo default do barramento, que no VSPI e o IO19 - o WDI do STWD100 -
    // e faz pinMode(IO19, INPUT). A partir dai os out_w1ts/out_w1tc da ISR escrevem num pino
    // de entrada e o chute some, ate o rearmPin() do passo 10. O IO39 e input-only e NC, e
    // absorve o MISO sem custo. Isto e o passo 8 da ordem de boot da Parte 2 do DECISIONS.md.
    SPI.begin(board::kDispSclk, board::kDispMiso, board::kDispMosi, board::kNoPin);
    softenDisplayBus();

    g_display.begin();
    g_display.setContrast(kDisplayContrast);

    g_wdt.rearmPin();

    g_keypad.begin();
    g_keypad.poll();

    g_app.begin(g_params);
    g_app.setConfigLatched(g_configLost);
    g_menu.begin();

    // Sem esta tarefa NENHUM heartbeat() e emitido, a carencia de boot de 3000 ms do
    // Stwd100Watchdog vence e a placa entra em reset ciclico - direcao segura, mas muda. A
    // linha abaixo e a unica chance de dizer POR QUE a placa reinicia a cada 3 s. Nao se chuta
    // o cachorro neste ramo de proposito: e o reset que tem de acontecer.
    const BaseType_t created =
        xTaskCreatePinnedToCore(&ctrlTask, "ctrl", app::kCtrlTaskStackBytes, nullptr,
                                app::kCtrlTaskPriority, &g_ctrlTask, app::kCtrlTaskCore);
    if (created != pdPASS) {
        g_ctrlTask = nullptr;
        Serial.println(F("FALHA AO CRIAR A TAREFA ctrl - a placa vai resetar pelo STWD100"));
    }

    g_wdt.enablePowerLed();
    g_boot.begin(bootAtMs, g_bootKeyMask);
    g_hmiMs = g_clock.nowMs();
    g_originMs = g_clock.nowMs();
}

void loop() {
    g_keypad.poll();
    g_gesture.update();

    const uint32_t nowMs = g_clock.nowMs();
    if (!deadlineReached(g_hmiMs, nowMs, kHmiPeriodMs)) {
        delay(kLoopSliceMs);
        return;
    }
    g_hmiMs = nowMs;

    // DESLOCAMENTO ANTI-QUEIMA (decisao 12 item 17, medicao 15): quatro fases de 1 px a cada
    // 900000 ms. O caso pior nao e a tela principal e sim CONFIG PERDIDA, que fica no ar por
    // semanas com dois textos fixos ate o Reset Geral - por isso o deslocamento tambem
    // reabilita o redesenho da tela estatica.
    if (deadlineReached(g_originMs, nowMs, kOriginShiftMs)) {
        g_originMs = nowMs;
        g_originPhase = static_cast<uint8_t>((g_originPhase + 1u) & 3u);
        const int8_t dx = static_cast<int8_t>((g_originPhase == 1u || g_originPhase == 2u) ? 1 : 0);
        const int8_t dy = static_cast<int8_t>((g_originPhase >= 2u) ? 1 : 0);
        g_display.setOrigin(dx, dy);
        g_configLostDrawn = false;
    }

    // Uma escrita de NVS por passagem, no maximo. Fica antes dos desvios de tela para que o
    // slot sujo nao fique pendurado no estado CONFIG PERDIDA nem durante o splash.
    servicePersist();

    // O passo 12 roda dentro da tarefa ctrl, entao o resultado dele so pode ser impresso aqui.
    if (!g_linkReported && g_linkBeginDone) {
        g_linkReported = true;
        Serial.print(F("link="));
        Serial.println(errName(g_linkBeginErr));
    }

    if (g_boot.ownsDisplay()) {
        g_boot.tick();
        if (g_boot.finished()) {
            g_gesture.flush();
            g_keypad.flush();
            if (g_boot.takeFactoryReset()) {
                applyFactoryReset();
            }
        }
        return;
    }

    if (g_configLost) {
        // Tela estatica: reenviar o MESMO quadro de 2 KB a 20 quadros por segundo por semanas
        // e desgaste de painel sem nenhum ganho de informacao.
        if (!g_configLostDrawn) {
            renderConfigLost();
            g_configLostDrawn = true;
        }
        return;
    }
    g_configLostDrawn = false;

    serviceHmi();
}
