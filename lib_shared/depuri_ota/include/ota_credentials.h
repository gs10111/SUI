// NOME E SENHA DO PONTO DE ACESSO DE ATUALIZACAO, por equipamento.
//
// ================================ LEIA ISTO ANTES DE MEXER ================================
// UMA SENHA DERIVADA DO MAC SO E SECRETA ENQUANTO O FIRMWARE FOR SECRETO - e o firmware e
// justamente o arquivo que entregamos ao cliente. O BSSID de um ponto de acesso VAI NO AR, em
// texto claro, em todo quadro de baliza. Quem tiver um pacote .ota e um analisador de espectro
// calcula a senha de qualquer equipamento do patio sem chegar perto dele.
//
// Por isso a derivacao daqui e o CAMINHO DEGRADADO, e nao o desenho:
//
//   1. CAMINHO NORMAL - a senha e sorteada na producao, gravada em NVS pelo jig e impressa na
//      etiqueta da placa. Nao esta em firmware nenhum. Quem esta no patio precisa do equipamento
//      na mao para ler a etiqueta, que e exatamente o que a Decisao 15 item 8 exige.
//   2. CAMINHO DEGRADADO - placa sem senha em NVS (nunca passou pelo jig, ou a NVS foi apagada):
//      cai na derivacao daqui, para que a placa continue atualizavel. A UR AVISA NA TELA que a
//      senha e derivada, porque nesse estado ela nao vale como controle de acesso.
//
// Nao "consertar" isto trocando o sal por um maior ou espalhando mais o resumo: o problema nao e
// a forca da funcao, e o fato de a entrada e o algoritmo serem ambos publicos. O conserto e a
// senha sorteada na producao.
// ==========================================================================================
//
// FORMATO DA SENHA. 12 caracteres de um alfabeto de 32 sem 0/O/1/I/L - alguem vai ler isto de uma
// etiqueta pequena, num patio, e digitar no celular. Sao 60 bits, muito acima do minimo do WPA2 e
// muito abaixo do que um humano aguenta errar.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ota_package.h"
#include "ota_sha256.h"

namespace ota {

constexpr size_t kMacBytes = 6;
constexpr size_t kPasswordChars = 12;
constexpr size_t kSsidMaxChars = 32;

// Minimo e maximo da WPA2-PSK em texto: abaixo de 8 o supplicant recusa; acima de 63 nao e mais
// passphrase.
constexpr size_t kWpa2MinChars = 8;
constexpr size_t kWpa2MaxChars = 63;

// Sem 0/O/1/I/L: numa etiqueta pequena, no patio, essas cinco viram a mesma coisa.
inline const char* passwordAlphabet() { return "23456789ABCDEFGHJKMNPQRSTUVWXYZ#"; }
constexpr size_t kAlphabetSize = 32;

// Separa este uso do resumo de qualquer outro que venha a existir com a mesma entrada.
inline const char* passwordDomain() { return "DIELETRONS-SUI-OTA-AP-v1"; }

inline const char* prefixoAlvo(uint16_t alvo) {
    return alvo == kAlvoSensora ? "SUI-SEN-" : "SUI-UR-";
}

// SSID: prefixo da placa + os tres ultimos bytes do MAC. O sufixo existe para que dois
// equipamentos no mesmo patio nao apresentem o mesmo nome - conectar no errado e subir firmware
// no equipamento errado.
// Devolve o numero de caracteres escritos (sem o terminador), ou 0 se nao couber.
inline size_t apSsid(uint16_t alvo, const uint8_t mac[kMacBytes], char* saida, size_t n) {
    static const char kHex[] = "0123456789ABCDEF";
    if (saida == nullptr || mac == nullptr) {
        return 0;
    }
    const char* pre = prefixoAlvo(alvo);
    size_t i = 0;
    while (pre[i] != '\0') {
        if (i + 1u >= n) {
            return 0;
        }
        saida[i] = pre[i];
        ++i;
    }
    for (size_t b = 3; b < kMacBytes; ++b) {
        if (i + 2u >= n) {
            return 0;
        }
        saida[i++] = kHex[(mac[b] >> 4) & 0x0Fu];
        saida[i++] = kHex[mac[b] & 0x0Fu];
    }
    if (i >= n) {
        return 0;
    }
    saida[i] = '\0';
    return i;
}

// CAMINHO DEGRADADO - ver o aviso no topo do arquivo. Deterministica de proposito: uma placa que
// caiu aqui tem de apresentar a MESMA senha depois de reiniciar, senao o operador que anotou a
// senha ha cinco minutos nao entra mais.
inline void derivedPassword(const uint8_t mac[kMacBytes], char saida[kPasswordChars + 1u]) {
    Sha256 s;
    const char* d = passwordDomain();
    size_t n = 0;
    while (d[n] != '\0') {
        ++n;
    }
    s.update(reinterpret_cast<const uint8_t*>(d), n);
    s.update(mac, kMacBytes);
    uint8_t resumo[kSha256Bytes];
    s.finish(resumo);

    const char* alfabeto = passwordAlphabet();
    for (size_t i = 0; i < kPasswordChars; ++i) {
        saida[i] = alfabeto[resumo[i] % kAlphabetSize];
    }
    saida[kPasswordChars] = '\0';
}

// A senha que veio da producao pode ser qualquer coisa que alguem digitou no jig. Se ela nao
// servir para WPA2, o WiFi.softAP() sobe o ponto de acesso ABERTO sem reclamar - e ai o
// equipamento inteiro fica gravavel por quem passar perto. Esta funcao e o que impede isso.
inline bool passwordWellFormed(const char* pw) {
    if (pw == nullptr) {
        return false;
    }
    size_t n = 0;
    while (pw[n] != '\0') {
        const unsigned char c = static_cast<unsigned char>(pw[n]);
        if (c < 0x20u || c > 0x7Eu) {  // WPA2 em texto so aceita ASCII imprimivel
            return false;
        }
        ++n;
        if (n > kWpa2MaxChars) {
            return false;
        }
    }
    return n >= kWpa2MinChars;
}

}  // namespace ota
