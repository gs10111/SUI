// src/ports/i_update_portal.h
// O ponto de acesso WiFi e a pagina por onde o pacote sobe.
// Alvo: WifiUpdatePortal (src/adapters/wifi_update_portal.cpp) - softAP WPA2 + WebServer.
// Fake: FakeUpdatePortal (test/fakes) - injeta cabecalho e pedacos na mao.
// REQ:  decisao 17 (OTA), decisao 15 item 8 (autenticacao que nao seja a senha de 4 digitos).
//
// O PORTAL EMPURRA, O DOMINIO DECIDE. O adaptador nao sabe o que e um pacote valido, nao sabe o
// que e um rele e nao decide nada: ele recebe bytes e os entrega ao sink. Quem recusa, quem pede
// confirmacao no painel e quem poe as saidas em alarme e OtaService, que e puro e testado no
// host. Um portal que decidisse sozinho seria um caminho de escrita em flash impossivel de
// exercitar sem WiFi por perto.
#pragma once

#include <stdint.h>

#include "status.h"

// O que a pagina mostra. So texto e numeros - o portal nao interpreta.
struct PortalStatus {
    const char* fase;        // "OCIOSO", "AGUARDANDO CONFIRMACAO NO PAINEL", ...
    const char* detalhe;     // motivo da recusa, motivo da falha, ou vazio
    uint16_t progressoPorMil;
    bool aceitandoPacote;    // a pagina pode oferecer o formulario?
    bool aceitandoBytes;     // a pagina pode enviar a imagem?
};

class IUpdatePortalSink {
public:
    virtual ~IUpdatePortalSink() = default;
    // Os primeiros 64 bytes. Devolve false quando o pacote foi recusado - o portal responde o
    // motivo e NAO segue para os bytes.
    virtual bool onHeader(const uint8_t* bytes, uint32_t n) = 0;
    // Um pedaco da imagem. Devolve false para abortar a transferencia na hora.
    virtual bool onChunk(const uint8_t* dados, uint32_t n) = 0;
    virtual void onEnd() = 0;
    virtual void onAbort() = 0;
};

class IUpdatePortal {
public:
    virtual ~IUpdatePortal() = default;
    IUpdatePortal(const IUpdatePortal&) = delete;
    IUpdatePortal& operator=(const IUpdatePortal&) = delete;

    // Sobe o ponto de acesso. Recusa senha que nao sirva para WPA2: softAP() com senha curta
    // sobe o ponto de acesso ABERTO sem reclamar, e ai o equipamento fica gravavel por quem
    // passar perto.
    virtual Status begin(const char* ssid, const char* senha, IUpdatePortalSink& sink) = 0;

    // Bombeia o servidor. Chamada a cada volta do laco da IHM; nunca bloqueia esperando rede.
    virtual void service() = 0;

    virtual void publish(const PortalStatus& st) = 0;

    virtual uint8_t clientesConectados() const = 0;
    virtual bool noAr() const = 0;

protected:
    IUpdatePortal() = default;
};
