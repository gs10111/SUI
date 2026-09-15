// lib_shared/depuri_ota/include/ota_portal.h
// O ponto de acesso WiFi e a pagina por onde o pacote sobe. Mesma porta nas duas placas.
// Alvo: WifiUpdatePortal (lib_shared/depuri_ota/src/wifi_update_portal.cpp) - softAP WPA2 + WebServer.
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

    // O estado DEPOIS da decisao que acabou de ser tomada.
    //
    // Existe porque o portal so recebe o estado por publish(), do laco principal, e o laco ja
    // rodou nesta volta quando os tratadores executam: o que o portal tinha em maos era a fase
    // ANTERIOR. Um pacote recusado por alvo errado registrava no console
    // "cabecalho RECUSADO - PRONTO PARA RECEBER" - a linha de diagnostico que existe justamente
    // porque nao havia nada para olhar saia dizendo a coisa errada.
    virtual void preencherStatus(PortalStatus& st) const = 0;
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

    // Bombeia o servidor. Chamada a cada volta do laco da IHM.
    //
    // ATENCAO - ELA BLOQUEIA DURANTE UM ENVIO, E ISSO NAO E EVITAVEL. Conferido no core
    // instalado (libraries/WebServer/src/Parsing.cpp:429-471): _parseForm() le o corpo inteiro do
    // POST byte a byte dentro de UMA chamada a handleClient(), e so devolve quando o envio
    // termina. O laco da IHM nao roda nesse intervalo. Para 1280 KiB sao segundos - muito acima
    // do token de liveness de 800 ms da UR e do tWD minimo de 1120 ms do STWD100 da sensora.
    //
    // Por isso existe setKeepAlive(): o adaptador chama essa funcao a cada pedaco recebido
    // (HTTP_UPLOAD_BUFLEN = 1436 bytes), e ela e o unico codigo nosso que roda durante o envio.
    //
    // O QUE CADA PLACA PRECISA FAZER LA E DIFERENTE, e confundir as duas custa caro:
    //   - SUPERVISORA: o batimento do watchdog e da tarefa ctrl, que vive no core 0 e continua
    //     rodando enquanto o loop() (core 1) esta preso aqui. O watchdog NAO depende deste
    //     gancho. O que depende e o painel: sem ele a barra de progresso congela no 0%.
    //   - SENSORA: nao ha segunda tarefa. O laco e um so, e e ele que renova o token de liveness
    //     do ExtWatchdog. Sem batimento aqui a placa reseta no meio de TODO envio.
    virtual void service() = 0;

    // Chamada a cada pedaco recebido, de DENTRO do envio. Ver a distincao acima antes de decidir
    // o que ligar aqui.
    //
    // O QUE ELA NAO RESOLVE: se o cliente parar de enviar sem desconectar (celular que bloqueia a
    // tela), Parsing.cpp:339 fica em "while(!client.available() && client.connected()) delay(2)",
    // que nao tem prazo. Nenhum pedaco chega, esta funcao nao e chamada, e o watchdog reseta a
    // placa. ISSO E ACEITAVEL E E O COMPORTAMENTO DESEJADO: nada foi ativado, a otadata nao foi
    // tocada e a particao que esta rodando esta intacta - a placa volta no firmware antigo. Ver
    // docs/ota.md.
    virtual void setKeepAlive(void (*fn)(void*), void* ctx) = 0;

    // DIAGNOSTICO. Chamado a cada evento do portal com uma linha pronta. Existe porque a primeira
    // tentativa de envio em bancada falhou e NAO HAVIA NADA para olhar: a barra nao andava, a
    // pagina nao dizia nada e o console nao imprimia uma linha sequer. Um caminho que grava flash
    // a 500 m nao pode ser mudo.
    virtual void setLogger(void (*fn)(void*, const char*), void* ctx) = 0;

    // Derruba o ponto de acesso e o servidor. Existe porque o radio deixou de ficar no ar o
    // tempo todo em 2026-09-14: ele sobe sob comando e cai sozinho quando ninguem mais esta
    // usando, e "cai" tem de ser uma operacao de verdade e nao um sinalizador que finge.
    virtual void end() = 0;

    virtual void publish(const PortalStatus& st) = 0;

    // O ENDERECO QUE O RADIO REALMENTE ASSUMIU, em texto ("192.168.4.1"). Existe para que
    // ninguem - nem a documentacao, nem quem esta no patio - tenha de confiar no valor padrao de
    // memoria: ele vem da pilha de rede, depois de o ponto de acesso subir. Vazio quando o radio
    // esta desligado.
    virtual const char* enderecoPagina() const = 0;

    // Quantas requisicoes o radio ja atendeu desde que subiu. E o que alimenta o relogio de
    // inatividade do portao (ota_gate.h): "houve atividade" e este numero ter mudado.
    //
    // CONTA REQUISICAO E NAO CLIENTE ASSOCIADO, e a diferenca e a que importa em campo: um
    // celular no bolso continua associado a rede por horas sem pedir nada.
    virtual uint32_t requisicoes() const = 0;

    virtual uint8_t clientesConectados() const = 0;
    virtual bool noAr() const = 0;

protected:
    IUpdatePortal() = default;
};
