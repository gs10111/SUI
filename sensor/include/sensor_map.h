// Mapa de registradores publicado pela sensora. Contrato de fio: a supervisora depende desta ordem.
#pragma once

#include <stdint.h>

namespace sensormap {

constexpr uint16_t kRegAngleX = 0;
constexpr uint16_t kRegAngleY = 1;
constexpr uint16_t kRegAngleZ = 2;
constexpr uint16_t kRegStatus = 3;
constexpr uint16_t kRegTempDeciC = 4;
constexpr uint16_t kRegWhoAmI = 5;
constexpr uint16_t kRegFwVersion = 6;

// VERSAO DE FIRMWARE DERIVADA DO BUILD, e nao de uma constante paralela. Fecha a pendencia P5
// de docs/protocolo-rs485.md.
//
// O registrador 6 estava cravado em 0x0001 por duas constantes em sensor/src/main.cpp,
// independentes do fw_version do platformio.ini. O defeito ficava invisivel por coincidencia -
// fw_version e "0.1.0", que codifica exatamente 0x0001 -, mas subir para 0.2.0 deixaria a
// sensora anunciando, vinte vezes por segundo, uma versao que ela nao e.
//
// Sem versao honesta no fio nao ha como verificar se uma atualizacao subiu, nao ha como detectar
// rollback, e nao ha gate de compatibilidade: uma sensora com outro significado de registrador
// passa por todos os testes de transporte e a UR comanda os quatro reles com o numero errado.
//
// FORMATO: (major << 8) | minor. O patch NAO entra - sao dois bytes so, e duas versoes que
// diferem apenas no patch sao a mesma coisa para o mestre. Campo acima de 255 SATURA no proprio
// byte em vez de transbordar para o byte do vizinho: um major de 256 que virasse 0x0000 seria
// indistinguivel de "registrador nunca escrito", que e justamente o estado que o mestre precisa
// conseguir distinguir. Texto mal formado devolve 0, e quem consome trata 0 como "nao publicou".
// VALIDACAO DA STRING INTEIRA, e nao campo a campo. Uma versao mal formada nao pode produzir um
// numero plausivel: "v1.2.3" analisado campo a campo devolveria 0x0002, que passa por versao boa
// e vai para o fio. Ou o texto e "digitos e pontos", ou ele nao e uma versao - e devolve 0, que
// o mestre le como "nao publicou".
constexpr bool fwVersionWellFormed(const char* text) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    bool viuDigito = false;
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p == '.') {
            continue;
        }
        if (*p < '0' || *p > '9') {
            return false;
        }
        viuDigito = true;
    }
    return viuDigito;
}

constexpr uint8_t fwVersionField(const char* text, uint8_t campo) {
    if (!fwVersionWellFormed(text)) {
        return 0;
    }
    uint8_t atual = 0;
    uint32_t valor = 0;
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p == '.') {
            if (atual == campo) {
                return static_cast<uint8_t>(valor);
            }
            ++atual;
            valor = 0;
            continue;
        }
        if (atual == campo) {
            valor = valor * 10u + static_cast<uint32_t>(*p - '0');
            // SATURA no proprio byte em vez de transbordar para o byte do vizinho: um major de
            // 256 que virasse 0x0000 seria indistinguivel de "registrador nunca escrito".
            if (valor > 255u) {
                valor = 255u;
            }
        }
    }
    return (atual == campo) ? static_cast<uint8_t>(valor) : 0;
}

constexpr uint16_t fwVersionReg(const char* text) {
    return static_cast<uint16_t>((static_cast<uint16_t>(fwVersionField(text, 0)) << 8) |
                                 fwVersionField(text, 1));
}

#ifdef FW_VERSION
constexpr uint16_t kFwVersionReg = fwVersionReg(FW_VERSION);
// Zero e indistinguivel de "a sensora nunca publicou nada". Uma versao 0.0.x produziria
// exatamente isso e cegaria o mestre; quem quiser usa-la tem de decidir conscientemente.
static_assert(kFwVersionReg != 0,
              "FW_VERSION codifica 0x0000 no registrador 6, indistinguivel de 'nunca publicado'");
#endif
constexpr uint16_t kRegUptimeS = 7;
constexpr uint16_t kRegCount = 8;

}  // namespace sensormap
