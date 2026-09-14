// NOME DO PONTO DE ACESSO DE ATUALIZACAO (por equipamento) E SENHA (padrao de fabrica).
//
// ================================ LEIA ISTO ANTES DE MEXER ================================
// A SENHA PADRAO ESTA NO FIRMWARE, E O FIRMWARE E O ARQUIVO QUE ENTREGAMOS AO CLIENTE. Um unico
// pacote .ota que vaze abre TODA a frota, para sempre, e nao ha como trocar a senha sem regravar
// todas as placas. Isto foi escolhido conscientemente pelo bigboss em 2026-09-14, com o custo
// declarado, e nao e um descuido a ser "descoberto" depois.
//
// O QUE ISSO NAO PIOROU: a alternativa anterior derivava a senha do MAC, e o MAC vai no ar em
// texto claro em toda baliza - qualquer um com o firmware calculava a senha de qualquer
// equipamento do patio. As duas sao publicas. A senha fixa so e mais honesta sobre isso.
//
// O CAMINHO PARA A SENHA DE VERDADE CONTINUA ABERTO, e e por isso que a ordem abaixo importa:
//
//   1. NVS "ota"/"pw"  - senha SORTEADA na producao, gravada pelo jig, impressa na etiqueta da
//                        placa. Nao esta em firmware nenhum. E o que a Decisao 15 item 8 exige.
//                        Se existir, GANHA - o firmware nem olha para a padrao.
//   2. kSenhaPadrao    - esta aqui. Vale enquanto o jig nao sortear nada, que e hoje.
//
// Quando a producao passar a gravar, nenhuma linha de firmware muda: as placas novas saem com
// senha propria e as antigas continuam subindo com a padrao ate serem regravadas.
// ==========================================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ota_package.h"

namespace ota {

constexpr size_t kMacBytes = 6;
constexpr size_t kSsidMaxChars = 32;

// Minimo e maximo da WPA2-PSK em texto: abaixo de 8 o supplicant recusa; acima de 63 nao e mais
// passphrase.
constexpr size_t kWpa2MinChars = 8;
constexpr size_t kWpa2MaxChars = 63;

// SENHA PADRAO DE FABRICA. Trocar isto aqui troca a senha de toda placa que nao tenha senha
// gravada na producao - e so vale na proxima gravacao de cada uma.
//
// 15 caracteres, ASCII imprimivel: serve para WPA2 com folga. Continua passando por
// passwordWellFormed() antes de subir o ponto de acesso, porque quem monta o sistema pode ter
// posto outra coisa em NVS e uma senha mal formada faz o softAP subir ABERTO.
inline const char* defaultPassword() { return "dieletrons-2025"; }

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

// A senha efetiva pode vir de NVS, escrita por quem operou o jig. Se ela nao servir para WPA2, o
// WiFi.softAP() sobe o ponto de acesso ABERTO sem reclamar - e ai o equipamento inteiro fica
// gravavel por quem passar perto. Esta funcao e o que impede isso, e por isso ela e aplicada
// TAMBEM a senha padrao: uma constante mal editada neste arquivo abriria a frota inteira.
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
