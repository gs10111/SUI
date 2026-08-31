// NvsParameterStore: retencao dos parametros na particao "nvs" da flash interna do U1
// ESP32-WROOM-32D (folha 1/2 do esquematico DE-PURI-DI261924 REV A; nao ha EEPROM, nao ha
// bateria e nao ha CI de memoria externa na placa - o manual 3/L47 e 5.11/L56, que falam em
// "EEPROM interna", ficam cobertos pelo desvio ja declarado na decisao 2, item 18).
// Implementa ports/i_parameter_store.h e tem de ser substituivel pelo fake da porta
// (test/fakes/fake_parameter_store.h): mesmos codigos de erro, mesmos limites, mesmas
// pre-condicoes. O contrato de substituibilidade esta preso em
// test/native/test_fakes_parameter_store/.
//
// ESTE ADAPTADOR MOVE BYTES. Nao ha uma unica regra de negocio aqui, por desenho e por ordem
// do cabecalho da porta: layout de registro, magic, versao do registro, CRC-16/MODBUS, numero
// de sequencia, escolha do banco mais novo, auto-cura e carga de padroes de fabrica sao
// dominio puro - Parameters (src/domain/parameters.{h,cpp}, que ja serializa e ja confere o
// CRC) envelopado pelo ParamStoreLogic, testavel em env:native. Consequencia direta, e ela e
// deliberada: read(ParamSlot::BankA) NAO decide nada sobre BankB, e um blob corrompido volta
// daqui com kOk - a corrupcao e visivel para quem tem o CRC, que e o dominio. Quem carrega
// padrao diante de dois bancos reprovados e a camada de aplicacao (decisao 8, e decisao 2
// item 10: registro invalido no boot e falha latchada, nao carga silenciosa da Tabela 2).
//
// PENDENCIA DE INTEGRACAO, ESCALADA AO DONO: ParamStoreLogic (numero de sequencia, escolha do
// banco mais novo, auto-cura) AINDA NAO EXISTE em src/domain/. Sem ele ninguem escolhe o banco
// mais novo com CRC valido no boot da etapa 8. Este adaptador nao pode suprir a falta sem
// virar dominio, e o cabecalho da porta proibe explicitamente que ele o faca.
//
// BANCO DUPLO. Os tres slots da porta sao tres chaves independentes do mesmo namespace:
// BankA -> "par_a", BankB -> "par_b" (o par que forma o banco duplo do registro vigente,
// decisao 2 item 3) e FactoryCal -> "cal_fab" (calibracao medida pelo jig, escrita uma vez na
// producao e so restaurada pelo Reset Geral, manual 5.11/L240, decisao 2 item 12). Atomicidade
// entre os dois bancos e propriedade da ALTERNANCIA de chave, nao de uma transacao da NVS:
// escrever "par_a" nunca toca em "par_b", entao um corte de energia no meio de uma gravacao
// destroi no maximo o banco que estava sendo escrito, e o outro continua integro e legivel.
// O que ainda nao se pode afirmar (M3): que a NVS nao corrompa a PAGINA compartilhada pelas
// duas chaves durante uma compactacao interrompida. Se corromper, o banco duplo por chave nao
// protege e a separacao tem de ir para particoes distintas.
//
// NAMESPACE VERSIONADO: "depuri1", o mesmo do firmware de teste de fabrica
// (src/platform/nvs_store.h:12), com o digito final valendo por versao do CONJUNTO de chaves.
// Uma mudanca incompativel de layout troca o namespace para "depuri2" e nao reinterpreta byte
// nenhum do anterior: firmware novo em placa antiga le namespace vazio, cai no Err::Storage de
// slot nunca escrito e o dominio trata isso como registro ausente, que e o caminho ja testado.
// ESCALADO AO DONO: compartilhar o namespace com o firmware de fabrica NAO e neutro. As chaves
// "serial", "date", ssid/senha/modo de wifi, o blob de calibracao e o flag de wdt gravadas na
// producao permanecem na particao quando o firmware de aplicacao e carregado, ocupam entradas
// e aproximam a compactacao. Ou a aplicacao ganha namespace proprio, ou o roteiro de producao
// apaga as chaves de fabrica no fim - e M3 tem de rodar as 100 escritas com apagamento numa
// particao que TAMBEM carregue as chaves de fabrica, senao mede uma particao mais vazia que a
// do campo e passa por sorte.
//
// CUSTO REAL DE UMA GRAVACAO NA PARTICAO, com a contabilidade certa. platformio.ini nao define
// board_build.partitions, entao vale o default.csv do framework: "nvs, data, nvs, 0x9000,
// 0x5000" = 20 KB = 5 setores de 4 KB. Cada pagina de 4 KB tem 126 entradas de 32 B, e um blob
// custa 1 entrada de indice + 1 cabecalho de dado + ceil(len/32) entradas de dado. Um registro
// de 32 B custa 3 entradas; um de 33 a 64 B custa 4. O que manda nessa conta e o len PEDIDO em
// write(), nao kCapacityBytes - a capacidade e teto, nunca vai para a flash - entao a unica
// economia real esta em o dominio chamar write() com o comprimento do registro serializado, e
// nunca com capacityBytes(). Com 4 entradas por chave e um commit de banco duplo escrevendo as
// DUAS chaves (8 entradas), a pagina enche em ~15 commits, e e ai que a NVS compacta um setor -
// justamente a janela em que a cache cai e a tarefa ctrl para. Era ISSO que a frase antiga
// deste cabecalho ("64 B nao custa nada, a NVS so grava o comprimento pedido") escondia: os
// bytes gravados sao mesmo so os pedidos, mas o preco e cobrado em entradas de 32 B e em
// apagamentos de setor, nao em bytes.
//
// QUANTO TEMPO write() BLOQUEIA, e o que ainda nao foi medido. Uma gravacao e
// Preferences::putBytes (que ja faz o commit da NVS) seguida da releitura obrigatoria da
// porta. O caso comum nao apaga setor e custa cerca de 15 ms (decisao 2, item 16). O caso
// ruim apaga um setor de 4 KB da particao "nvs" - a NVS compacta a pagina quando as entradas
// livres acabam - e durante o apagamento a CACHE DE FLASH E DESABILITADA: toda tarefa que
// executa de flash PARA, inclusive a tarefa ctrl de 50 ms. Nesse intervalo os quatro reles e
// os dois canais do DAC8562 mantem o ultimo estado escrito (GPIO e DAC sao latches de
// hardware) e nenhuma avaliacao de limite ocorre. E exatamente por isso que o chute do WDI do
// STWD100 sai de ISR de timer de HARDWARE em IRAM, e nao de esp_timer: o callback de esp_timer
// roda em ESP_TIMER_TASK, executa de flash e para junto (base comum, item 6; decisao 2 item
// 16). Este adaptador nao chuta o watchdog e nao chama o relogio - quem controla o ciclo e o
// dominio, e a porta publica writeBudgetMs() para ele planejar a janela.
//
// O ORCAMENTO DECLARADO AQUI E O DA PORTA: kWriteBudgetMs = 250 ms. A decisao 2 item 16 fala
// em kNvsCommitBudgetMs = 500 ms e a decisao 3 item 16 em 100 ms; a propria DECISIONS.md
// registra essa divergencia como reconciliacao pendente. O adaptador nao arbitra: publica o
// numero da porta, que e o unico contrato que o dominio ja usa, e o dono reconcilia.
// E ISTO NAO E SO UMA DIVERGENCIA NUMERICA, E UM DEFEITO DE COMPORTAMENTO JA PREVISTO:
//   250 ms > 150 ms do criterio de falha da tarefa ctrl; portanto write() SO pode ser chamado
//   com o contador de falha da ctrl suspenso ou com os reles congelados de proposito -
//   decisao 6.
// Sem isso, uma gravacao legitima de parametro que consuma o proprio orcamento declarado
// custa 5 ticks de 50 ms, a tarefa ctrl acumula 3 transacoes invalidas (150 ms, base comum
// item 3), declara FALHA DE COMUNICACAO e joga os quatro reles ao estado seguro: salvar um
// setpoint dispara alarme de sensor. DECISAO DO DONO, duas saidas mutuamente exclusivas:
//   (a) baixar kWriteBudgetMs abaixo de 150 ms NA PORTA e fatiar o commit; ou
//   (b) o contrato da porta passar a exigir que write() rode dentro da janela em que os reles
//       ja estao congelados de proposito (ou com a flag nvsCommitInProgress da decisao 2
//       item 16 suspendendo o contador de falha da ctrl).
// M3 (DECISIONS.md, medicoes de bancada) mede qual das duas e viavel. Nenhuma das duas cabe
// neste arquivo: as duas mudam a porta ou a base comum.
//
// A MEDICAO M3 AINDA NAO FOI FEITA. Nenhum numero de tempo deste cabecalho foi medido nesta
// placa: os 15 ms, os 250 ms e a premissa de que a janela de cache-off cabe no token de
// liveness de 800 ms sao PREVISAO. M3 mede, com osciloscopio no RST# do STWD100 e no IO19, a
// maior lacuna de WDI e o tempo de parede de putBytes + commit + releitura em 100 escritas que
// forcem apagamento de setor, mais 200 cortes de AC dentro da janela de commit. Aceitacao:
// lacuna de WDI ate 250 ms com o chute em IRAM e commit ate 500 ms em 100 de 100. Enquanto M3
// nao rodar, "write() bloqueia por ate 250 ms" e uma promessa nao verificada; se M3 reprovar,
// a correcao e fatiar o commit ou gravar so com os reles congelados de proposito - nao mexer
// neste arquivo. A vida util (~40 gravacoes por pagina, margem 89x, decisao 2 item 17) tambem
// e calculo, nao medicao: M3 conta apagamentos de setor por 5.000 gravacoes (aceitacao <= 150)
// e, reprovando, o intervalo minimo de gravacao do PSET tem de subir.
//
// JANELA SEM GUARDA DE WATCHDOG ANTES DO setup() - CORRECAO DE PREMISSA DO PASSO 7. O passo 7
// da ordem de boot (DECISIONS.md L608) orca "800 ms no pior caso de particao virgem ou
// corrompida que exige apagamento" e afirma que isso "sobrevive porque o chute do passo 1 e
// ISR/IRAM". Isso e FALSO para o caminho de particao virgem/corrompida: quem chama
// nvs_flash_init() e, em ESP_ERR_NVS_NO_FREE_PAGES / ESP_ERR_NVS_NEW_VERSION_FOUND, apaga a
// particao inteira, e o initArduino() do core (framework-arduinoespressif32 3.20017.241212,
// cores/esp32/esp32-hal-misc.c:249-262), que roda ANTES de setup() - logo antes de existir
// qualquer chute de WDI. Essa janela soma-se ao bootloader (~300 ms, ainda A_MEDIR) e fica sem
// guarda nenhuma contra o tWD minimo de 1,12 s do STWD100. Consequencias:
//   - begin() chama nvs_flash_init() explicitamente (e recupera com apagamento DENTRO do
//     setup(), onde a ISR do passo 1 ja esta armada), para cumprir o passo 7 ao pe da letra e
//     tornar a falha atribuivel a este adaptador em vez de silenciosa no core;
//   - M3 ("20 boots incluindo um com particao NVS virgem", DECISIONS.md L470/L623) tem de
//     disparar no RST# do STWD100 e medir do RESET ao primeiro pulso de WDI, nao do setup()
//     em diante;
//   - ate M3 rodar, a producao entrega a unidade com a particao "nvs" ja formatada pelo jig,
//     para que o campo nunca veja o caso virgem.
//
// Reaproveitamento: a mecanica de Preferences (abertura do namespace, checagem de chave,
// putBytes/getBytes e a traducao de toda falha da biblioteca em Err::Storage, sem impressao
// dentro do driver) vem de src/platform/nvs_store.cpp do firmware de teste de fabrica, ja
// validado em bancada. O que muda aqui e so o que a porta exige: releitura com comparacao
// byte a byte antes do kOk, blob curto demais para o buffer do chamador como Err::Param (e
// nao Err::Range), contador de escritas por slot e nomes de slot.
//
// Heap: prefs_ e o buffer de releitura sao membros; este adaptador nao aloca nada apos o
// setup(). A NVS aloca internamente durante o commit, dentro da biblioteca do framework, e
// isso e do framework, nao do firmware de aplicacao.
//
// REQ: PER-01 (manual 7/L308), MAN-5.4-L101/L102/L127/L128, MAN-5.11-L233 e Tabela 2,
//      decisao 2 (banco duplo, itens 3, 4, 12 e 16), decisao 8 (registros separados),
//      decisao 13 (senha e parametro como os outros), base comum secao 2.1 e ordem de boot.
#pragma once

#include <Preferences.h>
#include <stdint.h>

#include "ports/i_parameter_store.h"
#include "status.h"

class NvsParameterStore final : public IParameterStore {
public:
    static constexpr const char* kNamespace = "depuri1";

    // Teto de blob por slot: 48 B, o numero publicado por ports/i_parameter_store.h ("O
    // registro do produto tem 48 bytes") e ja usado pelo teste do dominio. O adaptador NAO
    // conhece o layout do registro e nao arbitra tamanho de envelope: qualquer mudanca de
    // tamanho entra pela porta e pelo fake, e so depois chega aqui.
    static constexpr uint16_t kCapacityBytes = 48;

    // Orcamento de bloqueio de write() publicado pela porta. erase() bloqueia sob o MESMO
    // orcamento - ver o cabecalho de nvs_parameter_store.cpp.
    static constexpr uint32_t kWriteBudgetMs = 250;

    NvsParameterStore();
    ~NvsParameterStore() override;

    Status begin() override;
    bool ready() const override { return ready_; }

    uint16_t capacityBytes() const override { return kCapacityBytes; }
    bool exists(ParamSlot slot) const override;

    Status read(ParamSlot slot, void* dst, uint16_t cap, uint16_t& outLen) override;
    Status write(ParamSlot slot, const void* src, uint16_t len) override;
    Status erase(ParamSlot slot) override;

    uint32_t writeBudgetMs() const override { return kWriteBudgetMs; }
    uint32_t writeCount(ParamSlot slot) const override;
    const char* slotName(ParamSlot slot) const override;

private:
    static bool slotValid(ParamSlot slot);
    static uint8_t slotIndex(ParamSlot slot);

    // Comprimento gravado na MIDIA para a chave do slot; 0 quando a chave nao existe. E o
    // unico predicado de presenca deste adaptador - nao ha cache de existencia em RAM,
    // justamente porque um cache mente no cenario que a decisao 2 existe para tratar (commit
    // interrompido: a chave ja esta na flash e o write() saiu por erro).
    size_t storedLen(uint8_t index) const;

    // mutable porque Preferences::getBytesLength/isKey nao sao const na biblioteca do
    // framework, e exists() e const por contrato da porta. A consulta nao altera a midia.
    mutable Preferences prefs_;
    uint8_t verify_[kCapacityBytes];
    uint32_t writeCount_[kParamSlotCount];
    bool ready_;
};
