// lib_shared/depuri_ota/include/esp_firmware_store.h
// A particao de firmware ociosa, de verdade: esp_ota_begin/write/end da IDF 4.4.7.
//
// POR QUE NAO Update.h. Updater.cpp:213-217 do core apaga blocos de 64 KiB numa unica chamada -
// ate 2000 ms parado. Isso estoura o token de liveness da UR (800 ms) e o tWD minimo do STWD100
// (1120 ms): a placa reseta no meio da gravacao. Aqui o tamanho e passado como
// OTA_WITH_SEQUENTIAL_WRITES, que faz a IDF apagar setor a setor (4 KiB) durante a escrita, e a
// escrita e fatiada em kPedacoBytes para que cada chamada volte rapido e o chamador possa bater
// o watchdog entre uma e outra.
//
// maiorEscritaMs() e MEDIDO. O esp_ota_ops.c vem pre-compilado no framework e o tempo de
// apagamento depende do chip de flash soldado na placa: nao da para derivar, so da para medir.
// A bancada le esse numero e compara com o prazo do watchdog em vez de acreditar numa estimativa.
#pragma once

#if defined(HOST_BUILD)
#error "esp_firmware_store.h fala com a IDF: no host use o FakeFirmwareStore"
#endif

#include <stdint.h>

#include <esp_ota_ops.h>

#include "ota_firmware_store.h"

class EspFirmwareStore : public IFirmwareStore {
public:
    // Um setor de flash. Escrever mais que isto por chamada junta varios apagamentos num
    // bloqueio so, que e exatamente o que este adaptador existe para evitar.
    static constexpr uint32_t kPedacoBytes = 4096;

    EspFirmwareStore();

    uint32_t capacidadeBytes() const override;
    Status begin(uint32_t tamanhoImagem) override;
    Status write(const uint8_t* dados, uint32_t n) override;
    Status finish() override;
    Status activate() override;
    void abort() override;
    uint32_t maiorEscritaMs() const override { return maiorEscritaMs_; }
    bool aberta() const override { return aberta_; }

    // Diagnostico: ultimo erro da IDF, para o console de bancada.
    int ultimoErro() const { return ultimoErro_; }

private:
    const esp_partition_t* ociosa_;
    esp_ota_handle_t handle_;
    bool aberta_;
    uint32_t maiorEscritaMs_;
    int ultimoErro_;
};
