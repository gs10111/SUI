// lib_shared/depuri_ota/include/ota_firmware_store.h
// A particao de firmware OCIOSA, vista pelo dominio. Mesma porta nas duas placas.
// Alvo: EspFirmwareStore (lib_shared/depuri_ota/src/esp_firmware_store.cpp) - esp_ota_begin/write/end com
//       OTA_WITH_SEQUENTIAL_WRITES; escrita em setores de 4 KiB, uma por chamada.
// Fake: FakeFirmwareStore (test/fakes) - guarda os bytes, simula falha em qualquer ponto.
// REQ:  decisao 17 (OTA), decisao 7 item 12 (laco travado tem de virar reset).
//
// POR QUE ESTA PORTA E FATIADA ASSIM. Update.h do core Arduino NAO serve: Updater.cpp:213-217
// apaga blocos de 64 KiB numa unica chamada, que chega a 2000 ms. Isso estoura o token de
// liveness da UR (800 ms) e o tWD minimo do STWD100 da sensora (1120 ms) - a placa reseta no
// meio da gravacao, com a particao ociosa pela metade. Por isso a escrita e fatiada aqui, em
// pedacos pequenos, e quem chama bate o heartbeat entre um e outro.
//
// maiorEscritaMs() existe porque o tempo de apagamento de um setor NAO e derivavel: o
// esp_ota_ops.c vem pre-compilado e o tempo depende do chip de flash soldado. A placa mede e
// informa; a bancada le o numero em vez de acreditar numa estimativa.
#pragma once

#include <stdint.h>

#include "status.h"

class IFirmwareStore {
public:
    virtual ~IFirmwareStore() = default;
    IFirmwareStore(const IFirmwareStore&) = delete;
    IFirmwareStore& operator=(const IFirmwareStore&) = delete;

    // Tamanho da particao ociosa, em bytes. O cabecalho do pacote e conferido contra isto ANTES
    // de qualquer apagamento.
    virtual uint32_t capacidadeBytes() const = 0;

    // Abre a particao ociosa para escrita sequencial. A que esta rodando nao e tocada.
    virtual Status begin(uint32_t tamanhoImagem) = 0;

    // Escreve o proximo pedaco. Tem de ser chamada em sequencia, sem buracos.
    virtual Status write(const uint8_t* dados, uint32_t n) = 0;

    // Fecha a imagem e deixa a IDF conferir o que foi gravado. NAO troca a particao de boot.
    virtual Status finish() = 0;

    // O UNICO passo irreversivel: o proximo boot passa a ser a particao nova.
    virtual Status activate() = 0;

    // Desiste. A particao ociosa fica com lixo, que e inofensivo - o proximo begin() a reabre.
    virtual void abort() = 0;

    // Maior duracao observada de uma unica chamada a write(), em ms. MEDIDO, nao estimado.
    virtual uint32_t maiorEscritaMs() const = 0;

    virtual bool aberta() const = 0;

protected:
    IFirmwareStore() = default;
};
