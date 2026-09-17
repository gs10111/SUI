// Compila SO na placa: WiFi e WebServer nao existem no host.
#if !defined(HOST_BUILD)

#include "wifi_update_portal.h"

#include <Arduino.h>
#include <WiFi.h>

#include <stdarg.h>

#include "ota_credentials.h"
#include "ota_package.h"

WifiUpdatePortal* WifiUpdatePortal::instancia_ = nullptr;

namespace {

// Pagina unica, sem nada de fora: o celular esta ligado no ponto de acesso do equipamento e nao
// tem internet. Toda folha de estilo, todo script e todo texto vem daqui.
const char kPagina[] PROGMEM = R"HTML(<!doctype html><html lang=pt-BR><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>Atualizacao de firmware</title>
<style>
body{font-family:system-ui,sans-serif;margin:0;padding:16px;background:#111;color:#eee}
h1{font-size:1.1rem;margin:0 0 4px}
.sub{color:#999;font-size:.85rem;margin-bottom:16px}
.cx{border:1px solid #333;border-radius:8px;padding:12px;margin-bottom:12px;background:#1a1a1a}
.fase{font-size:1.05rem;font-weight:600}
.det{color:#f5a;margin-top:4px;font-size:.9rem}
progress{width:100%;height:18px;margin-top:8px}
input[type=file]{width:100%;margin:8px 0}
button{width:100%;padding:12px;font-size:1rem;border:0;border-radius:6px;background:#2a6;color:#fff}
button[disabled]{background:#444;color:#888}
.av{background:#4a1a1a;border-color:#a33}
</style>
<h1>Atualizacao de firmware</h1>
<div class=sub id=eq>SUI-DI141388XY</div>
<div class="cx av"><b>As saidas vao para ALARME durante a atualizacao.</b>
Os quatro reles sao acionados e as saidas analogicas vao ao codigo de falha ate a placa
reiniciar. Nao atualize com a maquina em operacao.</div>
<div class=cx><div class=fase id=fase>...</div><div class=det id=det></div>
<progress id=pr max=1000 value=0></progress></div>
<div class=cx><input type=file id=arq accept=".ota">
<button id=bt disabled>Enviar</button></div>
<script>
const $=i=>document.getElementById(i);
let ocupado=false;
async function estado(){
 if(ocupado)return;
 try{const r=await fetch('/estado');const j=await r.json();
  $('fase').textContent=j.fase;$('det').textContent=j.det||'';
  $('pr').value=j.prog;$('eq').textContent=j.ssid;
  $('bt').disabled=!($('arq').files.length&&j.pac);
  if(j.byt&&window.pend){const f=window.pend;window.pend=null;enviaImagem(f);}
 }catch(e){}
}
$('arq').onchange=()=>estado();
$('bt').onclick=async()=>{
 const f=$('arq').files[0];if(!f)return;
 $('bt').disabled=true;ocupado=true;
 try{
  // HEXADECIMAL, e nao os 64 bytes crus: o WebServer do ESP32 entrega o corpo como String
  // construida de C-string, que para no primeiro byte zero - e o cabecalho tem um zero no
  // offset 9. Cru, chegavam 9 bytes e o pacote era descartado sem motivo visivel.
  const b=new Uint8Array(await f.slice(0,64).arrayBuffer());
  if(b.length<64){$('det').textContent='arquivo pequeno demais para ser um pacote';return;}
  const hx=Array.from(b).map(x=>x.toString(16).padStart(2,'0')).join('');
  const r=await fetch('/cabecalho',{method:'POST',body:hx});
  const j=await r.json();
  $('fase').textContent=j.fase;$('det').textContent=j.det||'';
  if(j.ok){window.pend=f;$('det').textContent='pacote aceito - aguardando o painel';}
  else if(!j.det){$('det').textContent='pacote recusado';}
 }catch(e){$('det').textContent='falha de conexao ao enviar o cabecalho';}
 ocupado=false;
};
// XMLHttpRequest, e nao fetch: e o unico jeito de a barra andar durante o envio.
// O servidor do ESP32 atende UMA conexao por vez e fica preso lendo o corpo do POST do
// primeiro ao ultimo byte, entao nenhuma consulta a /estado e respondida enquanto o envio
// acontece - com fetch, a barra so se mexia quando ja tinha acabado, e na sensora, que nao tem
// painel, era impossivel saber se algo estava sendo gravado.
// upload.onprogress e contado pelo NAVEGADOR e nao depende de resposta nenhuma da placa.
function enviaImagem(f){
 ocupado=true;
 const corpo=new FormData();corpo.append('img',f.slice(64),'img.bin');
 const x=new XMLHttpRequest();
 x.open('POST','/imagem');
 x.upload.onprogress=function(e){
  if(!e.lengthComputable)return;
  const pm=Math.round(e.loaded*1000/e.total);
  $('pr').value=pm;
  // "enviado" e nao "gravado": isto e o que o navegador ja empurrou para o socket, que corre
  // um pouco a frente do que a placa gravou. O numero verdadeiro chega no fim, do lado da placa.
  $('fase').textContent='ENVIANDO '+Math.floor(pm/10)+'%';
  $('det').textContent='nao desligue o equipamento';
 };
 x.onload=function(){
  ocupado=false;
  try{const j=JSON.parse(x.responseText);
      $('fase').textContent=j.fase;$('det').textContent=j.det||'';}
  catch(e){$('det').textContent='resposta inesperada do equipamento';}
 };
 x.onerror=function(){ocupado=false;$('det').textContent='conexao caiu durante o envio';};
 x.onabort=function(){ocupado=false;$('det').textContent='envio cancelado';};
 x.send(corpo);
}
setInterval(estado,700);estado();
</script></html>)HTML";

}  // namespace

WifiUpdatePortal::WifiUpdatePortal()
    : servidor_(80),
      sink_(nullptr),
      estado_{"", "", 0, false, false},
      keepAlive_(nullptr),
      keepAliveCtx_(nullptr),
      logger_(nullptr),
      loggerCtx_(nullptr),
      proximoAvisoBytes_(0),
      noAr_(false),
      dnsNoAr_(false),
      rotasRegistradas_(false),
      requisicoes_(0),
      envioAbortado_(false) {
    ssid_[0] = '\0';
    endereco_[0] = '\0';
}

void WifiUpdatePortal::setKeepAlive(void (*fn)(void*), void* ctx) {
    keepAlive_ = fn;
    keepAliveCtx_ = ctx;
}

void WifiUpdatePortal::setLogger(void (*fn)(void*, const char*), void* ctx) {
    logger_ = fn;
    loggerCtx_ = ctx;
}

void WifiUpdatePortal::registrar(const char* fmt, ...) {
    if (logger_ == nullptr) {
        return;
    }
    char linha[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(linha, sizeof(linha), fmt, ap);
    va_end(ap);
    logger_(loggerCtx_, linha);
}

void WifiUpdatePortal::manterVivo() {
    if (keepAlive_ != nullptr) {
        keepAlive_(keepAliveCtx_);
    }
}

Status WifiUpdatePortal::begin(const char* ssid, const char* senha, IUpdatePortalSink& sink) {
    if (noAr_) {
        return Status(Err::Busy);
    }
    if (ssid == nullptr || ssid[0] == '\0') {
        return Status(Err::Param);
    }
    // REGRA 1 do cabecalho: sem isto, softAP() sobe o ponto de acesso ABERTO em silencio.
    if (!ota::passwordWellFormed(senha)) {
        return Status(Err::Param);
    }

    uint8_t i = 0;
    while (ssid[i] != '\0' && i < sizeof(ssid_) - 1u) {
        ssid_[i] = ssid[i];
        ++i;
    }
    ssid_[i] = '\0';

    WiFi.mode(WIFI_AP);
    // Um canal so e potencia minima: o alcance util e o patio ao lado do equipamento, e nao o
    // predio inteiro. Quem atualiza tem de estar perto o bastante para ler a etiqueta.
    if (!WiFi.softAP(ssid_, senha, 1 /*canal*/, 0 /*visivel*/, 2 /*clientes*/)) {
        WiFi.mode(WIFI_OFF);
        return Status(Err::HwFault);
    }
    WiFi.setTxPower(WIFI_POWER_11dBm);

    sink_ = &sink;
    instancia_ = this;
    // UMA VEZ SO NA VIDA DO OBJETO. WebServer::on() ALOCA um RequestHandler e o encadeia numa
    // lista; ele nao substitui rota existente. Desde que o radio passou a subir e descer sob
    // comando (decisao 17), chamar isto a cada begin() vazaria cinco handlers por ativacao - num
    // equipamento que fica energizado meses e e atualizado de tempos em tempos, isso e heap que
    // nao volta e uma lista de despacho que so cresce.
    if (!rotasRegistradas_) {
        registrarRotas();
        rotasRegistradas_ = true;
    }
    servidor_.begin();

    // Toda consulta de nome responde com o IP da propria placa: e isto que faz o celular abrir a
    // tela sozinho ao entrar na rede. Se o DNS nao subir, o portal CONTINUA funcionando - so
    // exige que alguem digite o endereco - entao a falha aqui nao derruba a atualizacao.
    dns_.setErrorReplyCode(DNSReplyCode::NoError);
    dnsNoAr_ = dns_.start(53, "*", WiFi.softAPIP());

    // Perguntado a pilha de rede, e nao assumido: o valor padrao do softAP e do esp_netif, vem
    // de uma biblioteca pre-compilada e nao esta em cabecalho nenhum deste repositorio. Quem
    // esta no patio le o numero de verdade, no console.
    snprintf(endereco_, sizeof(endereco_), "%s", WiFi.softAPIP().toString().c_str());

    noAr_ = true;
    return kOk;
}

void WifiUpdatePortal::registrarRotas() {
    servidor_.on("/", HTTP_GET, []() { instancia_->tratarRaiz(); });
    servidor_.on("/estado", HTTP_GET, []() { instancia_->tratarEstado(); });
    servidor_.on("/cabecalho", HTTP_POST, []() { instancia_->tratarCabecalho(); });
    servidor_.on(
        "/imagem", HTTP_POST, []() { instancia_->tratarImagemFim(); },
        []() { instancia_->tratarImagemPedaco(); });
    servidor_.onNotFound([]() {
        // Todo celular sonda um endereco proprio para saber se ha internet, e cada fabricante usa
        // o seu. Com o DNS mandando tudo para ca, essas sondas caem AQUI - e devolver a pagina em
        // qualquer caminho e o que faz o aviso de "entrar na rede" abrir direto na tela de
        // atualizacao, em vez de o celular concluir que a rede esta quebrada e sair dela sozinho.
        instancia_->tratarRaiz();
    });
}

void WifiUpdatePortal::service() {
    if (!noAr_) {
        return;
    }
    if (dnsNoAr_) {
        dns_.processNextRequest();
    }
    servidor_.handleClient();
}

// A ORDEM IMPORTA: servidor, DNS, ponto de acesso, radio. Derrubar o radio antes de fechar o
// servidor deixaria sockets pendurados num stack que ja nao existe.
//
// PENDENCIA REGISTRADA: begin()/end() sobem e derrubam a pilha WiFi/netif/lwIP inteira, e isso e
// alocacao depois do setup() - o que este projeto trata como regra dura em todo o resto (ver o
// comentario "proibido pela regra dura" em ModbusSensorLink::begin, que faz questao de ser
// idempotente sem alocar). Numa placa energizada por meses, com ativacoes repetidas, isso e
// fragmentacao acumulada no caminho que grava flash.
//
// Nao foi consertado agora porque manter a pilha WiFi de pe com o radio desligado nao e obviamente
// mais barato do que derruba-la, e a alternativa certa depende de MEDICAO 26 - se o radio de pe
// custar jitter na tarefa ctrl, deixa-lo inicializado o tempo todo e pior, nao melhor. Medir
// primeiro; ver docs/ota.md secao 9.
void WifiUpdatePortal::end() {
    if (!noAr_) {
        return;
    }
    servidor_.stop();
    if (dnsNoAr_) {
        dns_.stop();
        dnsNoAr_ = false;
    }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    noAr_ = false;
    envioAbortado_ = false;
    endereco_[0] = '\0';
}

void WifiUpdatePortal::publish(const PortalStatus& st) { estado_ = st; }

uint8_t WifiUpdatePortal::clientesConectados() const {
    return noAr_ ? static_cast<uint8_t>(WiFi.softAPgetStationNum()) : 0u;
}

void WifiUpdatePortal::tratarRaiz() {
    ++requisicoes_;
    registrar("ota: pagina servida para %s", servidor_.client().remoteIP().toString().c_str());
    // NUNCA GUARDAR ESTA PAGINA EM CACHE. O celular de quem faz manutencao volta a esta rede
    // meses depois, com outro firmware na placa, e serviria a pagina velha de dentro do proprio
    // navegador - com o JS velho falando um protocolo que a placa nova nao entende mais. Foi
    // exatamente assim que a correcao do cabecalho em hexadecimal quase custou outra bancada
    // inteira: a placa ja estava certa e o celular continuava mandando binario cru.
    servidor_.sendHeader("Cache-Control", "no-store, must-revalidate");
    servidor_.sendHeader("Pragma", "no-cache");
    servidor_.sendHeader("Expires", "0");
    servidor_.send_P(200, "text/html", kPagina);
}

void WifiUpdatePortal::tratarEstado() {
    ++requisicoes_;
    char json[320];
    snprintf(json, sizeof(json),
             "{\"fase\":\"%s\",\"det\":\"%s\",\"prog\":%u,\"pac\":%s,\"byt\":%s,\"ssid\":\"%s\"}",
             estado_.fase == nullptr ? "" : estado_.fase,
             estado_.detalhe == nullptr ? "" : estado_.detalhe,
             static_cast<unsigned>(estado_.progressoPorMil),
             estado_.aceitandoPacote ? "true" : "false",
             estado_.aceitandoBytes ? "true" : "false", ssid_);
    servidor_.send(200, "application/json", json);
}

// Os 64 bytes chegam como corpo cru de um POST, e nao como formulario: e o pedaco mais curto
// possivel, e responder na hora e o que permite exigir confirmacao no painel sem segurar uma
// conexao aberta por um minuto.
void WifiUpdatePortal::tratarCabecalho() {
    ++requisicoes_;
    // EM HEXADECIMAL, e nao cru: ver decodeHex() em ota_package.h. O WebServer entrega o corpo
    // como String construida de C-string, que para no primeiro byte zero - e o cabecalho tem um
    // zero no offset 9.
    const String corpo = servidor_.arg("plain");
    uint8_t cab[ota::kHeaderBytes];
    const size_t n = ota::decodeHex(corpo.c_str(), corpo.length(), cab, sizeof(cab));
    registrar("ota: POST /cabecalho  %u caracteres -> %u bytes", (unsigned)corpo.length(),
              (unsigned)n);

    bool aceito = false;
    if (n != ota::kHeaderBytes) {
        registrar("ota: cabecalho MALFORMADO (esperava %u caracteres hexadecimais)",
                  (unsigned)(2u * ota::kHeaderBytes));
    } else if (sink_ == nullptr) {
        registrar("ota: sem destino para o pacote (erro de montagem do firmware)");
    } else {
        aceito = sink_->onHeader(cab, static_cast<uint32_t>(n));
        // Pergunta o estado AGORA. O que estava em estado_ e a fase anterior: publish() so
        // acontece no laco principal, que ja rodou nesta volta.
        sink_->preencherStatus(estado_);
        registrar("ota: cabecalho %s - %s", aceito ? "ACEITO" : "RECUSADO",
                  estado_.detalhe == nullptr || estado_.detalhe[0] == '\0' ? estado_.fase
                                                                          : estado_.detalhe);
    }
    char json[256];
    snprintf(json, sizeof(json), "{\"ok\":%s,\"fase\":\"%s\",\"det\":\"%s\"}",
             aceito ? "true" : "false", estado_.fase == nullptr ? "" : estado_.fase,
             estado_.detalhe == nullptr ? "" : estado_.detalhe);
    servidor_.send(200, "application/json", json);
}

// AQUI DENTRO O LACO PRINCIPAL NAO ESTA RODANDO. Ver a regra 2 no cabecalho: esta funcao e
// chamada a cada 1436 bytes de dentro de handleClient(), e e o unico ponto de execucao nosso
// durante o envio inteiro.
void WifiUpdatePortal::tratarImagemPedaco() {
    HTTPUpload& envio = servidor_.upload();
    ++requisicoes_;  // cada pedaco conta: um envio longo nao pode vencer o prazo de inatividade
    manterVivo();

    if (envio.status == UPLOAD_FILE_START) {
        envioAbortado_ = false;
        proximoAvisoBytes_ = 0;
        registrar("ota: inicio do envio da imagem");
        return;
    }
    if (envio.status == UPLOAD_FILE_WRITE) {
        if (envioAbortado_ || sink_ == nullptr) {
            return;
        }
        if (!sink_->onChunk(envio.buf, envio.currentSize)) {
            envioAbortado_ = true;
            sink_->preencherStatus(estado_);
            registrar("ota: envio RECUSADO no byte %u - %s", (unsigned)envio.totalSize,
                      estado_.detalhe == nullptr ? "" : estado_.detalhe);
            return;
        }
        // Uma linha a cada 64 KiB: o bastante para ver que anda, pouco o bastante para nao
        // inundar um console de 115200 durante um envio de 1280 KiB.
        if (envio.totalSize >= proximoAvisoBytes_) {
            sink_->preencherStatus(estado_);
            registrar("ota: %u KiB gravados (%u por mil)", (unsigned)(envio.totalSize / 1024u),
                      (unsigned)estado_.progressoPorMil);
            proximoAvisoBytes_ = envio.totalSize + 65536u;
        }
        return;
    }
    if (envio.status == UPLOAD_FILE_ABORTED) {
        envioAbortado_ = true;
        registrar("ota: envio ABORTADO pelo cliente no byte %u", (unsigned)envio.totalSize);
        if (sink_ != nullptr) {
            sink_->onAbort();
        }
    }
}

void WifiUpdatePortal::tratarImagemFim() {
    ++requisicoes_;
    if (sink_ != nullptr && !envioAbortado_) {
        sink_->onEnd();
    }
    if (sink_ != nullptr) {
        sink_->preencherStatus(estado_);
    }
    registrar("ota: fim do envio - %s %s", estado_.fase == nullptr ? "?" : estado_.fase,
              estado_.detalhe == nullptr ? "" : estado_.detalhe);
    char json[256];
    snprintf(json, sizeof(json), "{\"fase\":\"%s\",\"det\":\"%s\"}",
             estado_.fase == nullptr ? "" : estado_.fase,
             estado_.detalhe == nullptr ? "" : estado_.detalhe);
    servidor_.send(200, "application/json", json);
}

#endif  // !HOST_BUILD
