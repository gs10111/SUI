// Confere um .ota com O MESMO CODIGO QUE RODA NA PLACA, antes de alguem ir ao patio.
//
// POR QUE NAO BASTA O empacota_ota.py TER DITO QUE DEU CERTO. Sao duas implementacoes do mesmo
// layout - uma em Python, que escreve, e outra em C++, que le. O teste do vetor dourado prende as
// duas, mas so para um arquivo sintetico. Esta ferramenta le o arquivo DE VERDADE, o que saiu do
// build de hoje, com o parser de verdade.
//
// Compilar e usar:
//   g++ -std=gnu++17 -I lib_shared/depuri_ota/include -o /tmp/verifica_ota scripts/verifica_ota.cpp
//   /tmp/verifica_ota ur/.pio/build/esp32dev/firmware-supervisora-0.1.0.ota 1
//   /tmp/verifica_ota sensor/.pio/build/pusi/firmware-sensora-0.2.0.ota 2
//
// O segundo argumento e o alvo: 1 = supervisora, 2 = sensora. Passar o alvo ERRADO de proposito e
// o teste que mais vale a pena rodar - tem de dar veredito 5 (AlvoErrado).
//
// Vereditos de cabecalho: 0 Ok, 1 Curto, 2 MagicaInvalida, 3 CabecalhoCorrompido,
//                         4 VersaoDesconhecida, 5 AlvoErrado, 6 TamanhoInvalido, 7 ReservadoNaoZero
// Vereditos de imagem:    0 Incompleta, 1 Ok, 2 TamanhoErrado, 3 Crc32Errado, 4 Sha256Errado
#include <stdio.h>
#include <stdlib.h>
#include "ota_package.h"
int main(int argc, char** argv) {
    if (argc < 3) { printf("uso: %s <arquivo.ota> <alvo 1|2>\n", argv[0]); return 2; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("abrir"); return 2; }
    uint8_t cab[ota::kHeaderBytes];
    if (fread(cab, 1, sizeof(cab), f) != sizeof(cab)) { printf("curto\n"); return 1; }
    ota::PackageHeader h;
    const ota::HeaderVerdict v = ota::parseHeader(cab, sizeof(cab), (uint16_t)atoi(argv[2]), 1310720u, h);
    printf("cabecalho: veredito=%d alvo=%u tamanho=%u versao=%u.%u.%u\n", (int)v, h.alvo,
           h.tamanhoImagem, h.versaoMaior, h.versaoMenor, h.versaoCorrecao);
    if (v != ota::HeaderVerdict::Ok) return 1;
    ota::ImageVerifier iv; iv.begin(h);
    uint8_t buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) { if (!iv.feed(buf, n)) { printf("excesso\n"); return 1; } }
    fclose(f);
    printf("imagem   : veredito=%d recebidos=%u de %u\n", (int)iv.finish(), iv.recebidos(), iv.esperado());
    return iv.finish() == ota::ImageVerdict::Ok ? 0 : 1;
}
