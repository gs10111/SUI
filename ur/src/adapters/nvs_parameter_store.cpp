// Implementacao do armazem de blob por slot sobre a NVS do U1 ESP32-WROOM-32D (folha 1/2).
// Regras de erro, todas vindas de ports/i_parameter_store.h e nenhuma inventada aqui:
//   - antes de begin(): Err::NotInit, como todo driver deste produto;
//   - seletor de slot fora da enumeracao, ponteiro nulo, cap zero, len zero ou len acima de
//     capacityBytes(): Err::Param, defeito de chamador;
//   - buffer do chamador menor que o blob gravado: Err::Param (a porta e explicita, e por isso
//     este ponto DIVERGE do Err::Range de src/platform/nvs_store.cpp:56 do firmware de teste);
//   - slot nunca escrito ou falha da midia: Err::Storage;
//   - releitura divergente do que se acabou de gravar: Err::Storage, e cabe ao chamador
//     restaurar o valor anterior e avisar o operador (decisao 2, item 4: "FALHA DE GRAVACAO").
// erase() e idempotente: apagar slot ja ausente devolve kOk, porque o estado observavel pelo
// chamador (exists() falso) e o mesmo, e porque o Reset Geral do manual 5.11 pode apagar um
// banco que a queda de energia anterior ja tinha deixado sem gravar.
//
// QUANTO erase() BLOQUEIA. erase() faz nvs_erase_key + nvs_commit (Preferences.cpp:90-105) e
// bloqueia sob o MESMO writeBudgetMs() de write(), com a mesma janela de cache-off e a mesma
// parada da tarefa ctrl de 50 ms; o Reset Geral do manual 5.11, que apaga tres slots em
// sequencia, tem de orcar 3 x writeBudgetMs(). ESCALADO AO DONO DA PORTA: a linha de erase()
// em ports/i_parameter_store.h precisa da mesma frase de bloqueio que a de write(), para que o
// fake e o dominio planejem a janela - i_parameter_store.h nao pode ser editado nesta etapa.
//
// EXISTENCIA VEM DA MIDIA, NAO DE CACHE. exists() e erase() consultam o comprimento gravado da
// chave (Preferences::getBytesLength -> nvs_get_blob com buffer nulo, o MESMO predicado que
// read() usa para decidir "slot nunca escrito"). Um cache em RAM atualizado so no caminho de
// sucesso mentiria exatamente no cenario da decisao 2: Preferences::putBytes faz nvs_set_blob
// (que ja escreveu na flash) e depois nvs_commit; se o commit falhar, putBytes devolve 0 e
// write() sai por Err::Storage com a chave JA presente na NVS. Com cache, a auto-cura de banco
// duplo veria "banco AUSENTE" em vez de "banco PRESENTE com CRC reprovado" e tomaria o ramo
// errado (decisao 2, itens 4 e 10 - falha latchada, nao carga silenciosa).
//
// Nada aqui interpreta o conteudo do blob: sem magic, sem versao de registro, sem CRC, sem
// numero de sequencia, sem comparacao de idade entre BankA e BankB - isso e dominio (decisao
// 8, e o cabecalho de src/domain/parameters.h).
#include "adapters/nvs_parameter_store.h"

#include <nvs_flash.h>
#include <string.h>

namespace {

constexpr const char* kSlotKey[kParamSlotCount] = {"par_a", "par_b", "cal_fab"};
constexpr const char* kUnknownSlot = "?";

}  // namespace

NvsParameterStore::NvsParameterStore() : prefs_(), verify_(), writeCount_(), ready_(false) {}

NvsParameterStore::~NvsParameterStore() {
    if (ready_) {
        prefs_.end();
        ready_ = false;
    }
}

bool NvsParameterStore::slotValid(ParamSlot slot) {
    return static_cast<uint8_t>(slot) < kParamSlotCount;
}

uint8_t NvsParameterStore::slotIndex(ParamSlot slot) {
    return static_cast<uint8_t>(slot);
}

size_t NvsParameterStore::storedLen(uint8_t index) const {
    return prefs_.getBytesLength(kSlotKey[index]);
}

// Passo 7 do boot, ao pe da letra: nvs_flash_init() PRIMEIRO, e a recuperacao da particao
// virgem/corrompida DENTRO do setup(), onde a ISR de timer de hardware do passo 1 ja esta
// armada. Preferences::begin(nome, false) com partition_label nulo faz apenas nvs_open
// (Preferences.cpp:36-56) e NUNCA chamaria nvs_flash_init. Sem esta chamada explicita, a unica
// recuperacao seria a do initArduino() do core, que roda antes do setup() e portanto sem
// nenhum chute de WDI - ver a secao "JANELA SEM GUARDA DE WATCHDOG" do cabecalho .h.
// Bloqueio: 60 ms tipico; ate 800 ms se a particao exigir apagamento (previsao, A_MEDIR M3).
Status NvsParameterStore::begin() {
    if (ready_) {
        return kOk;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        if (nvs_flash_erase() != ESP_OK) {
            return Err::Storage;
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return Err::Storage;
    }

    if (!prefs_.begin(kNamespace, false)) {
        return Err::Storage;
    }
    for (uint8_t i = 0; i < kParamSlotCount; ++i) {
        writeCount_[i] = 0;
    }
    ready_ = true;
    return kOk;
}

bool NvsParameterStore::exists(ParamSlot slot) const {
    if (!ready_ || !slotValid(slot)) {
        return false;
    }
    return storedLen(slotIndex(slot)) != 0;
}

Status NvsParameterStore::read(ParamSlot slot, void* dst, uint16_t cap, uint16_t& outLen) {
    outLen = 0;
    if (!ready_) {
        return Err::NotInit;
    }
    if (!slotValid(slot) || dst == nullptr || cap == 0) {
        return Err::Param;
    }
    const uint8_t index = slotIndex(slot);
    const char* key = kSlotKey[index];
    const size_t stored = storedLen(index);
    if (stored == 0) {
        return Err::Storage;
    }
    if (stored > static_cast<size_t>(cap)) {
        return Err::Param;
    }
    if (prefs_.getBytes(key, dst, stored) != stored) {
        return Err::Storage;
    }
    outLen = static_cast<uint16_t>(stored);
    return kOk;
}

Status NvsParameterStore::write(ParamSlot slot, const void* src, uint16_t len) {
    if (!ready_) {
        return Err::NotInit;
    }
    if (!slotValid(slot) || src == nullptr || len == 0 || len > kCapacityBytes) {
        return Err::Param;
    }
    const uint8_t index = slotIndex(slot);
    const char* key = kSlotKey[index];

    if (prefs_.putBytes(key, src, static_cast<size_t>(len)) != static_cast<size_t>(len)) {
        return Err::Storage;
    }
    if (storedLen(index) != static_cast<size_t>(len)) {
        return Err::Storage;
    }
    if (prefs_.getBytes(key, verify_, static_cast<size_t>(len)) != static_cast<size_t>(len)) {
        return Err::Storage;
    }
    if (memcmp(verify_, src, static_cast<size_t>(len)) != 0) {
        return Err::Storage;
    }

    ++writeCount_[index];
    return kOk;
}

Status NvsParameterStore::erase(ParamSlot slot) {
    if (!ready_) {
        return Err::NotInit;
    }
    if (!slotValid(slot)) {
        return Err::Param;
    }
    const uint8_t index = slotIndex(slot);
    // storedLen() e o predicado de exists(); o isKey() cobre a chave que exista com outro tipo
    // (residuo do firmware de fabrica no mesmo namespace), que exists() ja reporta ausente mas
    // que continua ocupando entradas da pagina e por isso tem de sair no Reset Geral.
    if (storedLen(index) == 0 && !prefs_.isKey(kSlotKey[index])) {
        return kOk;
    }
    if (!prefs_.remove(kSlotKey[index])) {
        return Err::Storage;
    }
    return kOk;
}

uint32_t NvsParameterStore::writeCount(ParamSlot slot) const {
    if (!slotValid(slot)) {
        return 0;
    }
    return writeCount_[slotIndex(slot)];
}

const char* NvsParameterStore::slotName(ParamSlot slot) const {
    if (!slotValid(slot)) {
        return kUnknownSlot;
    }
    return kSlotKey[slotIndex(slot)];
}
