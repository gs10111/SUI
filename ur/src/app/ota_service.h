// src/app/ota_service.h
// O DONO DA ATUALIZACAO NA UNIDADE REMOTA. Camada de aplicacao pura: recebe as portas por
// referencia, nao instancia nada, nao inclui Arduino.h, nao usa ponto flutuante.
//
// AMARRA TRES PECAS QUE NAO SE CONHECEM:
//   ota::Session       - quando pode gravar (lib_shared/depuri_ota/ota_session.h)
//   ota::ImageVerifier - o que chegou confere (ota_package.h)
//   IFirmwareStore     - a particao ociosa
// e implementa IUpdatePortalSink, que e por onde o portal WiFi empurra bytes.
//
// A INVERSAO QUE IMPORTA: o portal nao decide nada. Ele nao sabe o que e um alvo errado, nao sabe
// o que e um rele e nao sabe o que e confirmacao no painel. Se soubesse, o caminho de escrita em
// flash so poderia ser exercitado com WiFi por perto - e ele e justamente o caminho onde um erro
// vira equipamento morto a 500 m.
//
// QUEM PARA OS RELES NAO E ESTE MODULO. Ele DECLARA, por saidasEmAlarme(), e espera: a gravacao
// so comeca quando main.cpp confirma que as saidas ja foram aplicadas. Se este modulo pudesse
// escrever rele, a camada de aplicacao teria dois donos das saidas - e o segundo dono e sempre o
// que escreve na hora errada.
#pragma once

#include <stdint.h>

#include "ports/i_firmware_store.h"
#include "ports/i_update_portal.h"
#include "ota_package.h"
#include "ota_session.h"

namespace app {

class OtaService : public IUpdatePortalSink {
public:
    OtaService(IFirmwareStore& store, uint16_t meuAlvo, bool exigeConfirmacao);

    // --- o que main.cpp e a IHM leem ---
    ota::Phase phase() const { return sessao_.phase(); }
    ota::FailReason failReason() const { return sessao_.failReason(); }
    ota::HeaderVerdict rejectReason() const { return sessao_.rejectReason(); }
    const ota::PackageHeader& header() const { return sessao_.header(); }
    uint16_t progressoPorMil() const { return sessao_.progressoPorMil(); }
    bool emCurso() const { return sessao_.phase() != ota::Phase::Ocioso; }

    // As saidas TEM de estar em alarme enquanto isto for verdadeiro.
    bool saidasEmAlarme() const { return sessao_.saidasEmAlarme(); }

    // A particao de boot ja foi trocada: main.cpp mostra a tela e reinicia.
    bool reinicioPendente() const { return sessao_.phase() == ota::Phase::Concluido; }

    // --- o que a IHM comanda ---
    void confirmar(uint32_t nowMs) { sessao_.confirmar(nowMs); }
    void cancelar(uint32_t nowMs);

    // Chamada a cada volta do laco da IHM. saidasAplicadas diz que main.cpp JA pos os quatro
    // reles em alarme e as saidas analogicas no codigo de falha - so entao a flash e aberta.
    void service(uint32_t nowMs, bool saidasAplicadas);

    void preencherStatusDoPortal(PortalStatus& st) const;

    // --- IUpdatePortalSink ---
    bool onHeader(const uint8_t* bytes, uint32_t n) override;
    bool onChunk(const uint8_t* dados, uint32_t n) override;
    void onEnd() override;
    void onAbort() override;

    // Diagnostico de bancada: maior tempo de uma unica escrita, medido pelo adaptador.
    uint32_t maiorEscritaMs() const { return store_.maiorEscritaMs(); }

private:
    void fecharFlashSeAberta();

    IFirmwareStore& store_;
    ota::Session sessao_;
    ota::ImageVerifier verificador_;
    uint16_t meuAlvo_;
    uint32_t agoraMs_;
};

// Texto curto e estavel para tela e pagina. Fora da classe para poder ser testado sozinho.
const char* textoDaFase(ota::Phase f);
const char* textoDaRecusa(ota::HeaderVerdict v);
const char* textoDaFalha(ota::FailReason m);

}  // namespace app
