// lib_shared/depuri_ota/include/wifi_update_portal.h
// O ponto de acesso WPA2 e a pagina de envio, iguais nas duas placas.
//
// TRES COISAS QUE ESTE ADAPTADOR TEM DE FAZER CERTO E QUE NAO SAO OBVIAS:
//
// 1. RECUSAR SENHA MAL FORMADA ANTES DE SUBIR O PONTO DE ACESSO. WiFi.softAP(ssid, senha) com
//    senha de menos de 8 caracteres NAO devolve erro - sobe o ponto de acesso ABERTO. O
//    equipamento ficaria gravavel por quem passasse perto do patio, em silencio.
//
// 2. BATER O WATCHDOG DE DENTRO DO ENVIO. Conferido no core instalado
//    (libraries/WebServer/src/Parsing.cpp:429-471): _parseForm() le o corpo inteiro do POST byte
//    a byte dentro de UMA chamada a handleClient(). O laco principal nao roda enquanto isso. O
//    unico ponto de execucao nosso e o callback de envio, chamado a cada HTTP_UPLOAD_BUFLEN
//    (1436) bytes - e e la que keepAlive_ e chamado.
//
// 3. NAO DEIXAR A RADIO LIGADA POR PADRAO NA SENSORA SEM MEDIR. Ver docs/ota.md: a sensora tem
//    WiFi.mode(WIFI_OFF) desde sempre por causa da MEDICAO 12 (ruido de RF no SCL3300), que nunca
//    foi feita. Quem monta o sistema decide; este adaptador nao liga radio sozinho.
//
// O ENVIO E EM DUAS ETAPAS, e nao uma:
//    POST /cabecalho  - os 64 bytes. Volta na hora, dizendo se o pacote serve e se falta
//                       confirmar no painel. E o que permite exigir a confirmacao SEM segurar uma
//                       conexao HTTP aberta por um minuto esperando alguem apertar um botao.
//    POST /imagem     - a imagem. So e aceita quando a maquina de fases ja esta em Gravando.
//    GET  /estado     - JSON curto, para a pagina acompanhar sem recarregar.
//
// PORTAL CATIVO. Um DNSServer responde TODA consulta com o IP da propria placa. Sem ele, o
// onNotFound() do WebServer so pega qualquer CAMINHO, nao qualquer ENDERECO: o celular nao
// resolveria nome nenhum, o aviso de "entrar na rede" nunca apareceria, e quem esta no patio
// teria de saber e digitar 192.168.4.1 de cabeca. Com ele, ligar na rede ja abre a tela.
#pragma once

#if defined(HOST_BUILD)
#error "wifi_update_portal.h fala com o radio: no host use um duble de teste"
#endif

#include <stdint.h>

#include <DNSServer.h>
#include <WebServer.h>

#include "ota_portal.h"

class WifiUpdatePortal : public IUpdatePortal {
public:
    WifiUpdatePortal();

    Status begin(const char* ssid, const char* senha, IUpdatePortalSink& sink) override;
    void service() override;
    void end() override;
    void publish(const PortalStatus& st) override;
    void setKeepAlive(void (*fn)(void*), void* ctx) override;
    uint8_t clientesConectados() const override;
    bool noAr() const override { return noAr_; }

    // Diagnostico de bancada.
    const char* ssid() const { return ssid_; }

private:
    void registrarRotas();
    void tratarRaiz();
    void tratarEstado();
    void tratarCabecalho();
    void tratarImagemFim();
    void tratarImagemPedaco();
    void manterVivo();

    static WifiUpdatePortal* instancia_;  // o WebServer so aceita callback sem contexto

    WebServer servidor_;
    DNSServer dns_;
    IUpdatePortalSink* sink_;
    PortalStatus estado_;
    void (*keepAlive_)(void*);
    void* keepAliveCtx_;
    char ssid_[33];
    bool noAr_;
    bool dnsNoAr_;
    bool rotasRegistradas_;
    bool envioAbortado_;
};
