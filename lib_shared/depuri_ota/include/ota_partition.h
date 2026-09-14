// A ACAO de hardware que o veredito da prova de boot manda executar, isolada num lugar so.
//
// Nao e pura (chama esp_ota_*), e por isso NAO entra em src/domain nem src/app da UR - a guarda
// hexagonal reprovaria, e com razao. Vive em lib_shared porque as duas placas precisam do MESMO
// comportamento e duas copias divergiriam: a diferenca entre "marca valida" e "marca invalida e
// reinicia" e a diferenca entre uma placa recuperavel e uma caminhonete.
//
// A DECISAO mora em ota::BootProof (puro, testado no host). Aqui so ficam as tres chamadas de
// IDF e a guarda que impede um boot NORMAL de mexer em particao.
#pragma once

#include <stdint.h>

#include "ota_proof.h"

#if !defined(HOST_BUILD)
#include <esp_ota_ops.h>
#endif

namespace ota {

// A particao corrente esta esperando veredito? Em boot NORMAL isto e falso e todo o mecanismo
// fica inerte - sem esta guarda, uma placa que nunca recebeu OTA ficaria chamando esp_ota_* a
// cada volta do laco.
inline bool pendingVerify() {
#if defined(HOST_BUILD)
    return false;
#else
    const esp_partition_t* rodando = esp_ota_get_running_partition();
    if (rodando == nullptr) {
        return false;
    }
    esp_ota_img_states_t estado = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(rodando, &estado) != ESP_OK) {
        return false;
    }
    return estado == ESP_OTA_IMG_PENDING_VERIFY;
#endif
}

// Aplica o veredito. Devolve true quando o assunto esta encerrado (aprovado ou nao ha nada
// pendente) e o chamador pode parar de perguntar.
//
// REPROVADO NAO RETORNA: esp_ota_mark_app_invalid_rollback_and_reboot() reinicia a placa e o
// bootloader sobe a particao anterior. E o unico caminho automatico de volta que existe para uma
// sensora a 500 m.
inline bool applyVerdict(ProofVerdict veredito) {
#if defined(HOST_BUILD)
    return veredito == ProofVerdict::Aprovado;
#else
    switch (veredito) {
        case ProofVerdict::Aprovado:
            esp_ota_mark_app_valid_cancel_rollback();
            return true;
        case ProofVerdict::Reprovado:
            esp_ota_mark_app_invalid_rollback_and_reboot();
            return true;  // inalcancavel: a chamada acima reinicia
        case ProofVerdict::Provando:
        default:
            return false;
    }
#endif
}

}  // namespace ota
