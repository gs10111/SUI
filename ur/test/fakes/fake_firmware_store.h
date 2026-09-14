// FakeFirmwareStore - a particao ociosa, de mentira, mas com os modos de falha de verdade.
//
// Guarda o que foi escrito para que o teste possa conferir que a imagem gravada e IGUAL a que
// entrou - o caso "gravou, disse que deu certo, e gravou errado" nao aparece em teste nenhum que
// so olhe codigo de retorno.
#pragma once

#include <stdint.h>
#include <string.h>

#include "ota_firmware_store.h"

class FakeFirmwareStore : public IFirmwareStore {
public:
    static constexpr uint32_t kCapacidade = 1310720u;  // app0/app1 de particoes_sui_4mb.csv
    static constexpr uint32_t kMaxGuardado = 64u * 1024u;

    FakeFirmwareStore()
        : falharBeginEm_(false), falharWriteApos_(0xFFFFFFFFu), falharFinish_(false),
          falharActivate_(false), aberta_(false), escritos_(0), guardados_(0), begins_(0),
          aborts_(0), finishes_(0), activates_(0), maiorEscritaMs_(0),
          capacidade_(kCapacidade), tentativasDeEscrita_(0) {}

    // --- controles do teste ---
    void falharBegin(bool v) { falharBeginEm_ = v; }
    void falharWriteAposBytes(uint32_t n) { falharWriteApos_ = n; }
    void falharFinish(bool v) { falharFinish_ = v; }
    void falharActivate(bool v) { falharActivate_ = v; }
    void setCapacidade(uint32_t n) { capacidade_ = n; }
    void setMaiorEscritaMs(uint32_t n) { maiorEscritaMs_ = n; }

    // --- o que o teste observa ---
    uint32_t escritos() const { return escritos_; }
    // Conta TODA chamada a write(), inclusive as recusadas. Existe para provar que um pedaco
    // fora de hora nem chega a bater na porta da flash - devolver erro depois nao e o mesmo que
    // nao tentar.
    uint32_t tentativasDeEscrita() const { return tentativasDeEscrita_; }
    uint32_t begins() const { return begins_; }
    uint32_t aborts() const { return aborts_; }
    uint32_t finishes() const { return finishes_; }
    uint32_t activates() const { return activates_; }
    const uint8_t* gravado() const { return buffer_; }
    uint32_t guardados() const { return guardados_; }

    // --- IFirmwareStore ---
    uint32_t capacidadeBytes() const override { return capacidade_; }

    Status begin(uint32_t) override {
        ++begins_;
        if (falharBeginEm_) {
            return Status(Err::Storage);
        }
        aberta_ = true;
        escritos_ = 0;
        guardados_ = 0;
        return kOk;
    }

    Status write(const uint8_t* dados, uint32_t n) override {
        ++tentativasDeEscrita_;
        if (!aberta_) {
            return Status(Err::NotInit);
        }
        if (escritos_ + n > falharWriteApos_) {
            return Status(Err::Io);
        }
        for (uint32_t i = 0; i < n && guardados_ < kMaxGuardado; ++i) {
            buffer_[guardados_++] = dados[i];
        }
        escritos_ += n;
        return kOk;
    }

    Status finish() override {
        ++finishes_;
        if (!aberta_) {
            return Status(Err::NotInit);
        }
        return falharFinish_ ? Status(Err::Crc) : kOk;
    }

    Status activate() override {
        ++activates_;
        if (falharActivate_) {
            return Status(Err::Storage);
        }
        aberta_ = false;
        return kOk;
    }

    void abort() override {
        ++aborts_;
        aberta_ = false;
    }

    uint32_t maiorEscritaMs() const override { return maiorEscritaMs_; }
    bool aberta() const override { return aberta_; }

private:
    uint8_t buffer_[kMaxGuardado];
    bool falharBeginEm_;
    uint32_t falharWriteApos_;
    bool falharFinish_;
    bool falharActivate_;
    bool aberta_;
    uint32_t escritos_;
    uint32_t guardados_;
    uint32_t begins_;
    uint32_t aborts_;
    uint32_t finishes_;
    uint32_t activates_;
    uint32_t maiorEscritaMs_;
    uint32_t capacidade_;
    uint32_t tentativasDeEscrita_;
};
