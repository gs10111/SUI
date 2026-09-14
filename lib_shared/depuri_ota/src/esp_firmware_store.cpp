// Compila SO na placa: a IDF nao existe no host. O que e testavel no host mora em
// ota_service.cpp e nos cabecalhos puros.
#if !defined(HOST_BUILD)

#include "esp_firmware_store.h"

#include <Arduino.h>

EspFirmwareStore::EspFirmwareStore()
    : ociosa_(nullptr), handle_(0), aberta_(false), maiorEscritaMs_(0), ultimoErro_(ESP_OK) {}

uint32_t EspFirmwareStore::capacidadeBytes() const {
    const esp_partition_t* p = esp_ota_get_next_update_partition(nullptr);
    return p == nullptr ? 0u : static_cast<uint32_t>(p->size);
}

Status EspFirmwareStore::begin(uint32_t) {
    if (aberta_) {
        return Status(Err::Busy);
    }
    ociosa_ = esp_ota_get_next_update_partition(nullptr);
    if (ociosa_ == nullptr) {
        return Status(Err::Storage);
    }

    // OTA_WITH_SEQUENTIAL_WRITES e a diferenca entre apagar 1280 KiB agora (segundos parado, com
    // o watchdog correndo) e apagar 4 KiB por vez, dentro de cada write().
    ultimoErro_ = esp_ota_begin(ociosa_, OTA_WITH_SEQUENTIAL_WRITES, &handle_);
    if (ultimoErro_ != ESP_OK) {
        return Status(Err::Storage);
    }
    aberta_ = true;
    maiorEscritaMs_ = 0;
    return kOk;
}

Status EspFirmwareStore::write(const uint8_t* dados, uint32_t n) {
    if (!aberta_) {
        return Status(Err::NotInit);
    }
    uint32_t pos = 0;
    while (pos < n) {
        uint32_t pedaco = n - pos;
        if (pedaco > kPedacoBytes) {
            pedaco = kPedacoBytes;
        }
        const uint32_t t0 = millis();
        ultimoErro_ = esp_ota_write(handle_, dados + pos, pedaco);
        const uint32_t dt = millis() - t0;
        if (dt > maiorEscritaMs_) {
            maiorEscritaMs_ = dt;
        }
        if (ultimoErro_ != ESP_OK) {
            return Status(Err::Io);
        }
        pos += pedaco;
    }
    return kOk;
}

// esp_ota_end() e onde a IDF confere a imagem gravada (cabecalho, tamanho, resumo proprio da
// imagem). Ela NAO troca a particao de boot: isso e activate(), e separar os dois e o que impede
// uma imagem reprovada de virar a particao do proximo boot.
Status EspFirmwareStore::finish() {
    if (!aberta_) {
        return Status(Err::NotInit);
    }
    ultimoErro_ = esp_ota_end(handle_);
    if (ultimoErro_ != ESP_OK) {
        // esp_ota_end() ja liberou o handle mesmo falhando: chamar abort() depois seria usar
        // handle morto.
        aberta_ = false;
        return Status(Err::Crc);
    }
    return kOk;
}

Status EspFirmwareStore::activate() {
    if (!aberta_ || ociosa_ == nullptr) {
        return Status(Err::NotInit);
    }
    ultimoErro_ = esp_ota_set_boot_partition(ociosa_);
    aberta_ = false;
    return ultimoErro_ == ESP_OK ? kOk : Status(Err::Storage);
}

void EspFirmwareStore::abort() {
    if (!aberta_) {
        return;
    }
    esp_ota_abort(handle_);
    aberta_ = false;
}

#endif  // !HOST_BUILD
