// Armazem de parametros de teste: tres slots em RAM, com injecao de falha de midia.
//
// Existe para uma coisa so: tornar auditavel no host o caminho que na placa exige corte de
// energia com osciloscopio. O banco duplo da decisao 2 promete que uma queda de energia no
// meio de uma gravacao destroi no maximo o banco que estava sendo escrito; sem os injetores
// abaixo essa promessa nunca e exercitada, e a etapa 8 ligaria o dominio na placa sem que
// nenhum ponto de corte tivesse sido testado.
//
// SUBSTITUIBILIDADE (LSP) COM NvsParameterStore - as respostas abaixo sao exatamente as do
// adaptador real (src/adapters/nvs_parameter_store.{h,cpp}), e test_fakes_parameter_store
// prende cada uma delas:
//   - antes de begin(): Err::NotInit em read/write/erase e false em exists(), mesmo com
//     argumento invalido (a checagem de prontidao vem ANTES da de parametro);
//   - slot fora da enumeracao, dst/src nulo, cap == 0, len == 0 ou len > capacityBytes():
//     Err::Param;
//   - slot nunca escrito: Err::Storage;
//   - buffer do chamador menor que o blob gravado: Err::Param (NAO Err::Range - a porta e
//     explicita, e o firmware de teste de fabrica divergia neste ponto);
//   - releitura divergente do que se acabou de gravar: Err::Storage;
//   - erase() de slot ausente: kOk (idempotente);
//   - capacityBytes() = 48 e writeBudgetMs() = 250, os MESMOS numeros da porta e do adaptador;
//   - writeCount() so incrementa DEPOIS que a releitura aprova.
//
// O QUE O FAKE NAO FAZ, porque o adaptador tambem nao faz: nao conhece layout de registro, nao
// calcula CRC, nao interpreta numero de sequencia e nao decide qual banco e o mais novo. Isso e
// ParamStoreLogic, dominio puro (cabecalho de ports/i_parameter_store.h).
//
// MODELO DE MIDIA DOS INJETORES - e aqui que o fake ganha o direito de chamar-se fake desta
// porta. Escrita truncada e corrupcao de byte atingem a MIDIA e deixam o slot PRESENTE com
// conteudo errado; a releitura obrigatoria da porta reprova e write() devolve Err::Storage,
// mas exists() continua true. E o retrato fiel do corte de energia depois do nvs_set_blob:
// a chave ja esta na flash. failNextWriteStorage() modela a falha ANTES de a midia mudar
// (nvs_set_blob recusado): o slot fica como estava.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ports/i_parameter_store.h"
#include "status.h"

namespace test {

class FakeParameterStore final : public IParameterStore {
public:
    // Os mesmos numeros do adaptador e da porta. Se um deles mudar, os tres mudam juntos.
    static constexpr uint16_t kCapacityBytes = 48;
    static constexpr uint32_t kWriteBudgetMs = 250;

    FakeParameterStore()
        : slot_(),
          writeCount_(),
          beginResult_(Err::Ok),
          truncateTo_(0),
          corruptAt_(0),
          corruptMask_(0),
          truncateArmed_(false),
          corruptArmed_(false),
          failWriteArmed_(false),
          failReadArmed_(false),
          ready_(false) {}

    // --- porta ---

    Status begin() override {
        if (beginResult_.failed()) {
            return beginResult_;
        }
        ready_ = true;
        return kOk;
    }

    bool ready() const override { return ready_; }

    uint16_t capacityBytes() const override { return kCapacityBytes; }
    uint32_t writeBudgetMs() const override { return kWriteBudgetMs; }

    bool exists(ParamSlot slot) const override {
        if (!ready_ || !slotValid(slot)) {
            return false;
        }
        return slot_[slotIndex(slot)].len != 0;
    }

    Status read(ParamSlot slot, void* dst, uint16_t cap, uint16_t& outLen) override {
        outLen = 0;
        if (!ready_) {
            return Err::NotInit;
        }
        if (!slotValid(slot) || dst == nullptr || cap == 0) {
            return Err::Param;
        }
        if (failReadArmed_) {
            failReadArmed_ = false;
            return Err::Storage;
        }
        const Cell& cell = slot_[slotIndex(slot)];
        if (cell.len == 0) {
            return Err::Storage;
        }
        if (cell.len > cap) {
            return Err::Param;
        }
        memcpy(dst, cell.data, cell.len);
        outLen = cell.len;
        return kOk;
    }

    Status write(ParamSlot slot, const void* src, uint16_t len) override {
        if (!ready_) {
            return Err::NotInit;
        }
        if (!slotValid(slot) || src == nullptr || len == 0 || len > kCapacityBytes) {
            return Err::Param;
        }
        const uint8_t i = slotIndex(slot);

        if (failWriteArmed_) {
            failWriteArmed_ = false;
            return Err::Storage;  // midia recusou antes de mudar: o slot fica como estava
        }

        // 1) a midia recebe os bytes, com o dano injetado, se houver.
        Cell& cell = slot_[i];
        uint16_t written = len;
        if (truncateArmed_) {
            truncateArmed_ = false;
            written = (truncateTo_ < len) ? truncateTo_ : len;
        }
        memcpy(cell.data, src, written);
        cell.len = written;
        if (corruptArmed_) {
            corruptArmed_ = false;
            if (corruptAt_ < cell.len) {
                cell.data[corruptAt_] = static_cast<uint8_t>(cell.data[corruptAt_] ^ corruptMask_);
            }
        }

        // 2) releitura obrigatoria da porta, byte a byte, antes de qualquer kOk.
        if (cell.len != len) {
            return Err::Storage;
        }
        if (memcmp(cell.data, src, len) != 0) {
            return Err::Storage;
        }

        ++writeCount_[i];
        return kOk;
    }

    Status erase(ParamSlot slot) override {
        if (!ready_) {
            return Err::NotInit;
        }
        if (!slotValid(slot)) {
            return Err::Param;
        }
        slot_[slotIndex(slot)].len = 0;  // idempotente: apagar slot ausente e kOk
        return kOk;
    }

    uint32_t writeCount(ParamSlot slot) const override {
        if (!slotValid(slot)) {
            return 0;
        }
        return writeCount_[slotIndex(slot)];
    }

    const char* slotName(ParamSlot slot) const override {
        if (!slotValid(slot)) {
            return "?";
        }
        return kSlotName[slotIndex(slot)];
    }

    // --- injecao de falha (so o teste chama) ---

    // begin() passa a devolver este erro, para exercitar "store nao pronto" no boot.
    void setBeginResult(Status result) { beginResult_ = result; }

    // Proxima gravacao para no meio: a midia guarda so os primeiros keepBytes.
    void truncateNextWrite(uint16_t keepBytes) {
        truncateArmed_ = true;
        truncateTo_ = keepBytes;
    }

    // Proxima gravacao chega inteira, mas com um byte corrompido na midia.
    void corruptNextWrite(uint16_t offset, uint8_t xorMask) {
        corruptArmed_ = true;
        corruptAt_ = offset;
        corruptMask_ = (xorMask == 0) ? 0xFFu : xorMask;
    }

    // Proxima gravacao e recusada pela midia antes de mudar coisa alguma.
    void failNextWriteStorage() { failWriteArmed_ = true; }

    // Proxima leitura falha na midia.
    void failNextReadStorage() { failReadArmed_ = true; }

    // Estado bruto da midia, para o teste conferir o que sobrou apos um corte.
    uint16_t rawLen(ParamSlot slot) const {
        return slotValid(slot) ? slot_[slotIndex(slot)].len : 0;
    }
    const uint8_t* rawData(ParamSlot slot) const {
        return slotValid(slot) ? slot_[slotIndex(slot)].data : nullptr;
    }

private:
    struct Cell {
        uint8_t data[kCapacityBytes];
        uint16_t len;
    };

    static bool slotValid(ParamSlot slot) {
        return static_cast<uint8_t>(slot) < kParamSlotCount;
    }
    static uint8_t slotIndex(ParamSlot slot) { return static_cast<uint8_t>(slot); }

    static constexpr const char* kSlotName[kParamSlotCount] = {"par_a", "par_b", "cal_fab"};

    Cell slot_[kParamSlotCount];
    uint32_t writeCount_[kParamSlotCount];
    Status beginResult_;
    uint16_t truncateTo_;
    uint16_t corruptAt_;
    uint8_t corruptMask_;
    bool truncateArmed_;
    bool corruptArmed_;
    bool failWriteArmed_;
    bool failReadArmed_;
    bool ready_;
};

}  // namespace test
