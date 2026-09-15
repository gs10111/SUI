// lib_shared/depuri_ota/include/ota_password_store.h
// Le a senha do ponto de acesso gravada NA PRODUCAO, sem transformar a ausencia dela em erro.
//
// POR QUE NAO Preferences. Preferences::begin("ota", /*readOnly=*/true) sobre um namespace que
// ainda nao existe devolve false - correto - mas ANTES disso o proprio core imprime, no console:
//
//   [E][Preferences.cpp:50] begin(): nvs_open failed: NOT_FOUND
//
// Uma placa que nunca passou pelo jig nao tem esse namespace, e hoje NENHUMA passou. Ou seja: o
// console de toda placa da frota abria com um erro em vermelho, em todo boot, para relatar um
// estado previsto e tratado.
//
// Isso importa mais do que parece neste produto. O console e o ultimo caminho de diagnostico
// quando o display morreu e o enlace caiu, e este projeto ja gastou uma sessao inteira de bancada
// atras de falhas que estavam escritas na tela e ninguem lia mais. Um erro que aparece sempre e
// nunca significa nada treina o operador a ignorar a linha em que um dia vai estar escrito algo
// que importa.
//
// Aqui a pergunta e feita direto ao NVS, que devolve um codigo em vez de imprimir: namespace
// ausente e uma RESPOSTA ("esta placa nao tem senha propria"), nao uma falha.
#pragma once

#if defined(HOST_BUILD)
#error "ota_password_store.h fala com a NVS: no host nao ha o que ler"
#endif

#include <stddef.h>

#include <nvs.h>

namespace ota {

// Namespace e chave que o jig de producao grava. Mudar qualquer um dos dois deixa a frota ja
// gravada sem senha propria, em silencio, na proxima atualizacao.
inline const char* passwordNamespace() { return "ota"; }
inline const char* passwordKey() { return "pw"; }

// Devolve true quando ha senha gravada na producao. Em qualquer outro caso devolve false com
// `saida` vazia, SEM imprimir nada - o chamador decide o que fazer, e o que ele faz e cair na
// senha padrao de fabrica (ota_credentials.h).
inline bool readProvisionedPassword(char* saida, size_t cap) {
    if (saida == nullptr || cap == 0u) {
        return false;
    }
    saida[0] = '\0';

    nvs_handle_t h = 0;
    // ESP_ERR_NVS_NOT_FOUND aqui significa "placa que nao passou pelo jig", que e o caso normal
    // hoje. Nao ha erro nenhum a relatar.
    if (nvs_open(passwordNamespace(), NVS_READONLY, &h) != ESP_OK) {
        return false;
    }

    size_t n = cap;
    const esp_err_t err = nvs_get_str(h, passwordKey(), saida, &n);
    nvs_close(h);

    if (err != ESP_OK) {
        saida[0] = '\0';  // nvs_get_str pode ter mexido no buffer antes de falhar
        return false;
    }
    return saida[0] != '\0';
}

}  // namespace ota
