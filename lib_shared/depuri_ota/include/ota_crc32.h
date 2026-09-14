// CRC-32 (IEEE 802.3, refletido, polinomio 0xEDB88320) em forma de FLUXO.
//
// POR QUE EM FLUXO E NAO DE UMA VEZ. A imagem tem ate 1280 KiB e a placa tem ~300 KiB de RAM: o
// arquivo nunca existe inteiro em lugar nenhum. O unico jeito de conferir integridade e conferir
// enquanto passa, e o unico jeito de testar isso no host e a mesma classe que roda na placa.
//
// POR QUE A TABELA E DE 16 ENTRADAS. A tabela classica de 256 entradas custa 1 KiB de .rodata nas
// duas placas para ganhar um passo por byte num caminho que ja e limitado pela escrita em flash.
// A de nibble custa 64 bytes e da o mesmo resultado - isto e verificavel: os testes prendem o
// valor de vetores conhecidos, nao a implementacao.
//
// ISTO NAO E AUTENTICACAO. CRC-32 detecta corrupcao de transporte; nao detecta adulteracao, e nao
// pretende. Quem passar pela senha WPA2 do ponto de acesso grava a placa. Ver docs/ota.md.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ota {

class Crc32 {
public:
    Crc32() : estado_(0xFFFFFFFFu) {}

    void reset() { estado_ = 0xFFFFFFFFu; }

    void update(const uint8_t* dados, size_t n) {
        static const uint32_t kNibble[16] = {
            0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u,
            0x4DB26158u, 0x5005713Cu, 0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
            0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
        uint32_t c = estado_;
        for (size_t i = 0; i < n; ++i) {
            c ^= dados[i];
            c = kNibble[c & 0x0Fu] ^ (c >> 4);
            c = kNibble[c & 0x0Fu] ^ (c >> 4);
        }
        estado_ = c;
    }

    uint32_t digest() const { return estado_ ^ 0xFFFFFFFFu; }

private:
    uint32_t estado_;
};

inline uint32_t crc32(const uint8_t* dados, size_t n) {
    Crc32 c;
    c.update(dados, n);
    return c.digest();
}

}  // namespace ota
