// SHA-256 (FIPS 180-4) em forma de FLUXO, puro.
//
// POR QUE EXISTE, JA QUE O CRC-32 JA ESTA AQUI. Sao perguntas diferentes:
//   - CRC-32 responde "o arquivo chegou inteiro?" - transporte.
//   - SHA-256 responde "este arquivo e AQUELE arquivo?" - identidade.
// Quem esta no patio sobe um binario pelo celular. O resumo aparece na tela e no registro de
// atualizacao, e confere com o que o sistema de compilacao imprimiu. E o unico jeito de alguem
// afirmar QUAL versao ficou na placa sem abrir o modulo.
//
// ISTO CONTINUA NAO SENDO AUTENTICACAO: sem assinatura, quem produz o arquivo produz o resumo.
// O controle de acesso e a senha WPA2 por equipamento. Ver docs/ota.md.
//
// POR QUE NAO mbedtls. mbedtls existe no ESP32 e nao existe no host. Uma verificacao que so pode
// ser exercitada na placa nao pode ser testada contra vetores conhecidos nem sofrer mutacao - e
// verificacao de integridade errada passa despercebida exatamente ate o dia em que importa.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ota {

constexpr size_t kSha256Bytes = 32;

class Sha256 {
public:
    Sha256() { reset(); }

    void reset() {
        h_[0] = 0x6A09E667u; h_[1] = 0xBB67AE85u; h_[2] = 0x3C6EF372u; h_[3] = 0xA54FF53Au;
        h_[4] = 0x510E527Fu; h_[5] = 0x9B05688Cu; h_[6] = 0x1F83D9ABu; h_[7] = 0x5BE0CD19u;
        totalBits_ = 0;
        pendentes_ = 0;
    }

    void update(const uint8_t* dados, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            bloco_[pendentes_++] = dados[i];
            totalBits_ += 8u;
            if (pendentes_ == 64u) {
                comprime(bloco_);
                pendentes_ = 0;
            }
        }
    }

    // Fecha o resumo. Depois disto o objeto so serve de novo apos reset().
    //
    // O comprimento e capturado ANTES do preenchimento e escrito a partir da copia local: o
    // 0x80 e os zeros que vem depois sao enchimento de bloco e nao fazem parte da mensagem.
    // Por isso totalBits_ nao e restaurado aqui - ele ja nao e lido de novo, e "consertar" o
    // contador depois de usar a copia seria uma escrita morta a fingir que protege algo.
    void finish(uint8_t saida[kSha256Bytes]) {
        const uint64_t bits = totalBits_;
        const uint8_t um = 0x80u;
        update(&um, 1);
        const uint8_t zero = 0x00u;
        while (pendentes_ != 56u) {
            update(&zero, 1);
        }
        uint8_t tam[8];
        for (int i = 0; i < 8; ++i) {
            tam[i] = static_cast<uint8_t>((bits >> (56 - 8 * i)) & 0xFFu);
        }
        update(tam, 8);
        for (int i = 0; i < 8; ++i) {
            saida[4 * i + 0] = static_cast<uint8_t>((h_[i] >> 24) & 0xFFu);
            saida[4 * i + 1] = static_cast<uint8_t>((h_[i] >> 16) & 0xFFu);
            saida[4 * i + 2] = static_cast<uint8_t>((h_[i] >> 8) & 0xFFu);
            saida[4 * i + 3] = static_cast<uint8_t>(h_[i] & 0xFFu);
        }
    }

private:
    static uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32u - n)); }

    void comprime(const uint8_t bloco[64]) {
        static const uint32_t k[64] = {
            0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u, 0x3956C25Bu, 0x59F111F1u,
            0x923F82A4u, 0xAB1C5ED5u, 0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
            0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u, 0xE49B69C1u, 0xEFBE4786u,
            0x0FC19DC6u, 0x240CA1CCu, 0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
            0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u, 0xC6E00BF3u, 0xD5A79147u,
            0x06CA6351u, 0x14292967u, 0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
            0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u, 0xA2BFE8A1u, 0xA81A664Bu,
            0xC24B8B70u, 0xC76C51A3u, 0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
            0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u, 0x391C0CB3u, 0x4ED8AA4Au,
            0x5B9CCA4Fu, 0x682E6FF3u, 0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
            0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u};

        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(bloco[4 * i + 0]) << 24) |
                   (static_cast<uint32_t>(bloco[4 * i + 1]) << 16) |
                   (static_cast<uint32_t>(bloco[4 * i + 2]) << 8) |
                   static_cast<uint32_t>(bloco[4 * i + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
        uint32_t e = h_[4], f = h_[5], g = h_[6], hh = h_[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
        h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += hh;
    }

    uint32_t h_[8];
    uint64_t totalBits_;
    uint8_t bloco_[64];
    uint8_t pendentes_;
};

}  // namespace ota
