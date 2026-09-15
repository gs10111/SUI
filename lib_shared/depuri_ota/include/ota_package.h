// O CABECALHO DE 64 BYTES QUE ABRE TODO PACOTE DE ATUALIZACAO, e a regra que decide se o arquivo
// pode sequer comecar a ser gravado.
//
// POR QUE UM CABECALHO PROPRIO, SE O esp_ota_end() JA VALIDA A IMAGEM. Valida - mas so DEPOIS de
// escrever 1280 KiB na particao ociosa, e sem responder a pergunta que mais importa aqui:
//
//   DE QUE PLACA E ESTE ARQUIVO?
//
// As duas placas do SUI-DI141388XY sobem o mesmo ponto de acesso, com a mesma pagina, e quem
// atualiza esta no patio com o celular na mao. Uma imagem da supervisora gravada na sensora e uma
// imagem valida, assinada pelo bootloader, que sobe e nao tem SCL3300, nao tem RS-485 escravo e
// nao tem como ser atualizada de novo: vira caminhonete com cabo USB. O campo `alvo` existe para
// esse erro, e e conferido ANTES do primeiro byte gravado.
//
// O resto do cabecalho responde o que o esp_ota_end() responde tarde demais ou nao responde:
// tamanho (cabe na particao? o arquivo veio truncado?), CRC-32 (chegou inteiro?), SHA-256 (e
// AQUELE arquivo?) e a versao (o que ficou gravado, para o registro).
//
// ORDEM DAS CONFERENCIAS - e deliberada, porque a mensagem na tela depende dela:
//   1. tamanho do cabecalho    -> "arquivo pequeno demais para ser um pacote"
//   2. magica                  -> "isto nao e um pacote deste produto" (um .txt, uma foto)
//   3. CRC-32 do cabecalho     -> "pacote corrompido"  (nenhum campo abaixo e confiavel antes)
//   4. versao do cabecalho     -> "pacote de um formato mais novo que este firmware"
//   5. alvo                    -> "este pacote e da OUTRA placa"
//   6. tamanho da imagem       -> "nao cabe na particao" / "tamanho zero"
//   7. reservados em zero      -> recusa o que este firmware nao sabe interpretar
//
// LAYOUT (little-endian, escrito byte a byte para nao depender da ordem da maquina):
//   desl. tam. campo
//     0     8  magica "DEPURIOT"
//     8     2  versaoCabecalho
//    10     2  alvo (1 = supervisora DE-PURI-DI261924, 2 = sensora PUSI-DI261930)
//    12     4  tamanhoImagem, em bytes, SEM contar estes 64
//    16     4  crc32Imagem
//    20    32  sha256Imagem
//    52     1  versaoMaior
//    53     1  versaoMenor
//    54     1  versaoCorrecao
//    55     1  sinalizadores (reservado, tem de ser 0)
//    56     4  reservado (tem de ser 0)
//    60     4  crc32Cabecalho, sobre os bytes 0..59
//   ----   64
//
// ISTO NAO E ASSINATURA. Quem produz o arquivo produz o resumo; nada aqui prova origem. O
// controle de acesso e a senha WPA2 por equipamento, impressa na etiqueta. Ver docs/ota.md.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ota_crc32.h"
#include "ota_sha256.h"

namespace ota {

constexpr size_t kHeaderBytes = 64;
constexpr size_t kMagicBytes = 8;
constexpr uint16_t kHeaderVersion = 1;

constexpr uint16_t kAlvoSupervisora = 1;  // DE-PURI-DI261924 (UR-DI151399)
constexpr uint16_t kAlvoSensora = 2;      // PUSI-DI261930

// Menor imagem que vale a pena tentar: abaixo disto e truncamento, nao firmware.
constexpr uint32_t kMinImageBytes = 4096;

struct PackageHeader {
    uint16_t versaoCabecalho;
    uint16_t alvo;
    uint32_t tamanhoImagem;
    uint32_t crc32Imagem;
    uint8_t sha256Imagem[kSha256Bytes];
    uint8_t versaoMaior;
    uint8_t versaoMenor;
    uint8_t versaoCorrecao;
};

enum class HeaderVerdict : uint8_t {
    Ok = 0,
    Curto,             // nao chegaram 64 bytes
    MagicaInvalida,    // nao e um pacote deste produto
    CabecalhoCorrompido,
    VersaoDesconhecida,
    AlvoErrado,        // pacote da OUTRA placa
    TamanhoInvalido,   // zero, pequeno demais, ou maior que a particao
    ReservadoNaoZero,
};

namespace detail {

inline uint16_t le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline void putLe16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
}

inline void putLe32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}

inline const char* magic() { return "DEPURIOT"; }

}  // namespace detail

// O CABECALHO VIAJA EM HEXADECIMAL, e esta funcao e o motivo.
//
// O WebServer do core entrega o corpo de um POST como String, e faz isso com
// `String(plainBuf)` (libraries/WebServer/src/Parsing.cpp:217) - construtor de C-string, que PARA
// NO PRIMEIRO BYTE ZERO. O cabecalho tem um zero logo no offset 9 (o byte alto da versao do
// formato), entao um POST binario chegava com 9 bytes em vez de 64: o pacote era descartado por
// "curto demais" antes de qualquer conferencia, e a pagina nao tinha nem motivo para mostrar.
//
// Mandar 128 caracteres ASCII em vez de 64 bytes crus custa nada e nao depende de o transporte
// preservar byte zero. A imagem continua indo crua, por multipart, que tem outro caminho no core.
//
// Devolve quantos bytes decodificou. Recusa comprimento impar, caractere fora de [0-9a-fA-F] e
// saida que nao cabe - em silencio nao, com zero: quem chama distingue.
inline size_t decodeHex(const char* texto, size_t n, uint8_t* saida, size_t cap) {
    if (texto == nullptr || saida == nullptr || (n % 2u) != 0u || (n / 2u) > cap) {
        return 0;
    }
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') { return c - '0'; }
        if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
        if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
        return -1;
    };
    for (size_t i = 0; i < n; i += 2u) {
        const int hi = nibble(texto[i]);
        const int lo = nibble(texto[i + 1u]);
        if (hi < 0 || lo < 0) {
            return 0;
        }
        saida[i / 2u] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return n / 2u;
}

// Le e julga o cabecalho. `out` so tem conteudo utilizavel quando devolve Ok.
inline HeaderVerdict parseHeader(const uint8_t* bytes, size_t n, uint16_t meuAlvo,
                                 uint32_t tamanhoParticao, PackageHeader& out) {
    if (bytes == nullptr || n < kHeaderBytes) {
        return HeaderVerdict::Curto;
    }
    for (size_t i = 0; i < kMagicBytes; ++i) {
        if (bytes[i] != static_cast<uint8_t>(detail::magic()[i])) {
            return HeaderVerdict::MagicaInvalida;
        }
    }
    // Antes disto nenhum campo abaixo e confiavel.
    if (crc32(bytes, kHeaderBytes - 4u) != detail::le32(bytes + 60)) {
        return HeaderVerdict::CabecalhoCorrompido;
    }

    const uint16_t versao = detail::le16(bytes + 8);
    if (versao != kHeaderVersion) {
        return HeaderVerdict::VersaoDesconhecida;
    }
    const uint16_t alvo = detail::le16(bytes + 10);
    if (alvo != meuAlvo) {
        return HeaderVerdict::AlvoErrado;
    }
    const uint32_t tamanho = detail::le32(bytes + 12);
    if (tamanho < kMinImageBytes || tamanho > tamanhoParticao) {
        return HeaderVerdict::TamanhoInvalido;
    }
    if (bytes[55] != 0u || detail::le32(bytes + 56) != 0u) {
        return HeaderVerdict::ReservadoNaoZero;
    }

    out.versaoCabecalho = versao;
    out.alvo = alvo;
    out.tamanhoImagem = tamanho;
    out.crc32Imagem = detail::le32(bytes + 16);
    for (size_t i = 0; i < kSha256Bytes; ++i) {
        out.sha256Imagem[i] = bytes[20 + i];
    }
    out.versaoMaior = bytes[52];
    out.versaoMenor = bytes[53];
    out.versaoCorrecao = bytes[54];
    return HeaderVerdict::Ok;
}

// Escreve um cabecalho bem formado. Existe para que a ferramenta de empacotamento e o firmware
// NUNCA divirjam: os testes montam pacotes com esta mesma funcao que o parse le.
inline void writeHeader(const PackageHeader& h, uint8_t saida[kHeaderBytes]) {
    for (size_t i = 0; i < kHeaderBytes; ++i) {
        saida[i] = 0;
    }
    for (size_t i = 0; i < kMagicBytes; ++i) {
        saida[i] = static_cast<uint8_t>(detail::magic()[i]);
    }
    detail::putLe16(saida + 8, h.versaoCabecalho);
    detail::putLe16(saida + 10, h.alvo);
    detail::putLe32(saida + 12, h.tamanhoImagem);
    detail::putLe32(saida + 16, h.crc32Imagem);
    for (size_t i = 0; i < kSha256Bytes; ++i) {
        saida[20 + i] = h.sha256Imagem[i];
    }
    saida[52] = h.versaoMaior;
    saida[53] = h.versaoMenor;
    saida[54] = h.versaoCorrecao;
    detail::putLe32(saida + 60, crc32(saida, kHeaderBytes - 4u));
}

enum class ImageVerdict : uint8_t {
    Incompleta = 0,  // ainda faltam bytes
    Ok,
    TamanhoErrado,   // chegaram mais bytes do que o cabecalho prometeu
    Crc32Errado,
    Sha256Errado,
};

// Confere a imagem ENQUANTO ELA PASSA. O arquivo nunca existe inteiro em lugar nenhum: sao ate
// 1280 KiB contra ~300 KiB de RAM.
//
// Excesso de bytes e detectado NA HORA e nao no fim: continuar aceitando depois do tamanho
// prometido seria gravar fora do que foi conferido.
class ImageVerifier {
public:
    ImageVerifier() : esperado_(0), recebidos_(0), crcEsperado_(0), excesso_(false) {
        for (size_t i = 0; i < kSha256Bytes; ++i) {
            shaEsperado_[i] = 0;
        }
    }

    void begin(const PackageHeader& h) {
        esperado_ = h.tamanhoImagem;
        crcEsperado_ = h.crc32Imagem;
        for (size_t i = 0; i < kSha256Bytes; ++i) {
            shaEsperado_[i] = h.sha256Imagem[i];
        }
        recebidos_ = 0;
        excesso_ = false;
        crc_.reset();
        sha_.reset();
    }

    // Devolve false quando o pedaco ultrapassa o tamanho prometido - o chamador tem de abortar.
    bool feed(const uint8_t* dados, size_t n) {
        if (excesso_) {
            return false;
        }
        if (n > static_cast<size_t>(esperado_ - recebidos_)) {
            excesso_ = true;
            return false;
        }
        crc_.update(dados, n);
        sha_.update(dados, n);
        recebidos_ += static_cast<uint32_t>(n);
        return true;
    }

    uint32_t recebidos() const { return recebidos_; }
    uint32_t esperado() const { return esperado_; }

    // 0..1000 (por mil, nao por cento: uma barra de progresso em inteiro sem ponto flutuante).
    uint16_t progressoPorMil() const {
        if (esperado_ == 0u) {
            return 0;
        }
        const uint64_t p = (static_cast<uint64_t>(recebidos_) * 1000u) / esperado_;
        return static_cast<uint16_t>(p > 1000u ? 1000u : p);
    }

    ImageVerdict finish() {
        if (excesso_) {
            return ImageVerdict::TamanhoErrado;
        }
        if (recebidos_ != esperado_) {
            return ImageVerdict::Incompleta;
        }
        if (crc_.digest() != crcEsperado_) {
            return ImageVerdict::Crc32Errado;
        }
        uint8_t obtido[kSha256Bytes];
        sha_.finish(obtido);
        for (size_t i = 0; i < kSha256Bytes; ++i) {
            if (obtido[i] != shaEsperado_[i]) {
                return ImageVerdict::Sha256Errado;
            }
        }
        return ImageVerdict::Ok;
    }

private:
    Crc32 crc_;
    Sha256 sha_;
    uint8_t shaEsperado_[kSha256Bytes];
    uint32_t esperado_;
    uint32_t recebidos_;
    uint32_t crcEsperado_;
    bool excesso_;
};

}  // namespace ota
