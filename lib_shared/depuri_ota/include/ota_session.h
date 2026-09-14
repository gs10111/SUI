// A MAQUINA DE FASES DA ATUALIZACAO. Pura: sem Arduino, sem WiFi, sem flash, sem ponto
// flutuante. O que ela decide e QUANDO cada coisa pode acontecer - e, principalmente, quando NAO
// pode.
//
// TRES REGRAS QUE ESTA MAQUINA EXISTE PARA GARANTIR:
//
// 1. NENHUM BYTE VAI PARA A FLASH ANTES DA CONFIRMACAO NO PAINEL. aceitaBytes() so e verdadeiro
//    em Gravando, e Gravando so e alcancavel passando por Aguardando quando exigeConfirmacao.
//    Foi isto que o operador escolheu: aviso na tela ANTES, e nao um equipamento que comeca a se
//    regravar porque alguem apontou um celular para ele.
//
// 2. AS SAIDAS VAO PARA ALARME ANTES DA PRIMEIRA ESCRITA, e nao durante. Apagar um setor de
//    flash trava o processador por centenas de milissegundos; o laco de controle nao roda, e
//    quatro reles seguindo uma leitura velha e pior do que quatro reles em alarme declarado.
//
// 3. AS SAIDAS NAO FICAM EM ALARME PARA SEMPRE. Este e o modo de falha de campo de verdade: o
//    operador confirma, comeca a subir, o celular bloqueia a tela, ele vai almocar - e a maquina
//    fica travada em alarme sem ninguem entender por que. Por isso ha relogio de ESTAGNACAO
//    (nenhum byte novo) e um TETO ABSOLUTO para a fase de gravacao. Vencido qualquer um dos dois,
//    a sessao cai em Falhou e as saidas voltam ao normal.
//
// ABORTAR NO MEIO DA GRAVACAO E SEGURO E NAO REINICIA A PLACA: quem esta sendo escrita e a
// particao OCIOSA. A imagem que esta rodando nao e tocada em momento nenhum. O unico instante
// irreversivel e a troca de particao, que acontece depois de Verificando.
#pragma once

#include <stdint.h>

#include "ota_package.h"

namespace ota {

enum class Phase : uint8_t {
    Ocioso = 0,      // ponto de acesso no ar, operacao normal; nada em curso
    Aguardando,      // cabecalho aceito, esperando confirmacao no painel
    Preparando,      // confirmado; o chamador esta pondo as saidas em alarme
    Gravando,        // escrevendo na particao ociosa
    Verificando,     // imagem completa; conferindo CRC-32, SHA-256 e a propria imagem
    Concluido,       // particao trocada; o chamador reinicia
    Recusado,        // o pacote nao serve (alvo errado, corrompido...); mostra o motivo
    Falhou,          // deu errado depois de comecar; saidas voltam ao normal
};

enum class FailReason : uint8_t {
    Nenhuma = 0,
    NaoConfirmado,     // ninguem confirmou no painel dentro do prazo
    Estagnou,          // parou de chegar byte
    TempoEsgotado,     // teto absoluto da gravacao
    ImagemInvalida,    // CRC-32, SHA-256 ou tamanho
    FalhaDeGravacao,   // a flash recusou
    FalhaDeTroca,      // esp_ota_set_boot_partition falhou
    Cancelado,         // alguem cancelou no painel
};

// Prazo para alguem confirmar no painel. Mais do que isto e um equipamento que ficou esperando.
constexpr uint32_t kConfirmacaoTimeoutMs = 60000;
// Sem byte novo por este tempo, a sessao morre e as saidas voltam. Subir 1280 KiB por WiFi leva
// segundos ate num celular ruim: 60 s sem NADA e desistencia, nao lentidao.
constexpr uint32_t kEstagnacaoMs = 60000;
// Teto absoluto da gravacao, para o caso de um fluxo lento porem continuo que nunca estagna.
constexpr uint32_t kGravacaoTetoMs = 300000;
// Tempo que a recusa e o erro ficam na tela antes de a sessao voltar a Ocioso.
constexpr uint32_t kMensagemMs = 10000;

class Session {
public:
    // exigeConfirmacao: verdadeiro na supervisora, que tem painel e teclado. Na sensora nao ha
    // onde confirmar - e o alarme sai de qualquer jeito, porque o enlace cai e a supervisora
    // declara falha por conta propria (decisao A5).
    explicit Session(bool exigeConfirmacao)
        : fase_(Phase::Ocioso),
          motivo_(FailReason::Nenhuma),
          recusa_(HeaderVerdict::Ok),
          exigeConfirmacao_(exigeConfirmacao),
          marcoMs_(0),
          inicioGravacaoMs_(0),
          ultimoProgressoMs_(0),
          gravados_(0),
          total_(0) {}

    Phase phase() const { return fase_; }
    FailReason failReason() const { return motivo_; }
    HeaderVerdict rejectReason() const { return recusa_; }
    uint32_t gravados() const { return gravados_; }
    uint32_t total() const { return total_; }

    // Regra 1. So aqui e verdadeiro.
    bool aceitaBytes() const { return fase_ == Phase::Gravando; }

    // Regra 2. Verdadeiro desde ANTES da primeira escrita ate a troca de particao.
    bool saidasEmAlarme() const {
        return fase_ == Phase::Preparando || fase_ == Phase::Gravando ||
               fase_ == Phase::Verificando || fase_ == Phase::Concluido;
    }

    // Um pacote so pode ser oferecido quando nao ha nada em curso: aceitar um segundo cabecalho
    // no meio de uma gravacao trocaria o que esta sendo escrito pela metade.
    bool aceitaPacote() const { return fase_ == Phase::Ocioso; }

    uint16_t progressoPorMil() const {
        if (total_ == 0u) {
            return 0;
        }
        const uint64_t p = (static_cast<uint64_t>(gravados_) * 1000u) / total_;
        return static_cast<uint16_t>(p > 1000u ? 1000u : p);
    }

    // Chegou um cabecalho ja julgado por parseHeader().
    void offerHeader(HeaderVerdict veredito, const PackageHeader& h, uint32_t nowMs) {
        if (!aceitaPacote()) {
            return;
        }
        if (veredito != HeaderVerdict::Ok) {
            recusa_ = veredito;
            fase_ = Phase::Recusado;
            marcoMs_ = nowMs;
            return;
        }
        cabecalho_ = h;
        total_ = h.tamanhoImagem;
        gravados_ = 0;
        marcoMs_ = nowMs;
        fase_ = exigeConfirmacao_ ? Phase::Aguardando : Phase::Preparando;
    }

    const PackageHeader& header() const { return cabecalho_; }

    void confirmar(uint32_t nowMs) {
        if (fase_ != Phase::Aguardando) {
            return;
        }
        fase_ = Phase::Preparando;
        marcoMs_ = nowMs;
    }

    // Cancelar vale em Aguardando e tambem durante a gravacao: a particao ociosa fica com lixo, a
    // que esta rodando nao foi tocada, e as saidas voltam ao normal.
    void cancelar(uint32_t nowMs) {
        if (fase_ == Phase::Ocioso || fase_ == Phase::Concluido) {
            return;
        }
        falhar(FailReason::Cancelado, nowMs);
    }

    // O chamador ja pos as saidas em alarme e abriu a particao ociosa. So agora bytes podem
    // entrar.
    void prontoParaGravar(uint32_t nowMs) {
        if (fase_ != Phase::Preparando) {
            return;
        }
        fase_ = Phase::Gravando;
        inicioGravacaoMs_ = nowMs;
        ultimoProgressoMs_ = nowMs;
    }

    void noteGravados(uint32_t bytes, uint32_t nowMs) {
        if (fase_ != Phase::Gravando) {
            return;
        }
        gravados_ = bytes;
        ultimoProgressoMs_ = nowMs;
    }

    void noteFalhaDeGravacao(uint32_t nowMs) { falhar(FailReason::FalhaDeGravacao, nowMs); }

    // A imagem chegou inteira; o veredito vem do ImageVerifier.
    void noteImagemCompleta(ImageVerdict veredito, uint32_t nowMs) {
        if (fase_ != Phase::Gravando) {
            return;
        }
        if (veredito != ImageVerdict::Ok) {
            falhar(FailReason::ImagemInvalida, nowMs);
            return;
        }
        fase_ = Phase::Verificando;
        marcoMs_ = nowMs;
    }

    // O unico instante irreversivel: a particao de boot ja foi trocada.
    void noteTrocaDeParticao(bool ok, uint32_t nowMs) {
        if (fase_ != Phase::Verificando) {
            return;
        }
        if (!ok) {
            falhar(FailReason::FalhaDeTroca, nowMs);
            return;
        }
        fase_ = Phase::Concluido;
        marcoMs_ = nowMs;
    }

    // Todo prazo mora aqui: quem chama so precisa chamar isto a cada volta do laco.
    void tick(uint32_t nowMs) {
        switch (fase_) {
            case Phase::Aguardando:
                if ((nowMs - marcoMs_) >= kConfirmacaoTimeoutMs) {
                    falhar(FailReason::NaoConfirmado, nowMs);
                }
                break;
            case Phase::Gravando:
                // Regra 3. Estagnacao e teto absoluto: os dois existem porque um fluxo lento
                // porem continuo nunca estagna, e um fluxo morto nunca estoura o teto.
                if ((nowMs - ultimoProgressoMs_) >= kEstagnacaoMs) {
                    falhar(FailReason::Estagnou, nowMs);
                } else if ((nowMs - inicioGravacaoMs_) >= kGravacaoTetoMs) {
                    falhar(FailReason::TempoEsgotado, nowMs);
                }
                break;
            case Phase::Recusado:
            case Phase::Falhou:
                if ((nowMs - marcoMs_) >= kMensagemMs) {
                    voltarAoOcioso();
                }
                break;
            default:
                // Preparando e Verificando sao passagens curtas conduzidas pelo chamador, e
                // Concluido termina em reinicio. Sem prazo aqui: um prazo em Preparando abortaria
                // uma sessao legitima enquanto os reles ainda estao acionando.
                break;
        }
    }

private:
    void falhar(FailReason motivo, uint32_t nowMs) {
        motivo_ = motivo;
        fase_ = Phase::Falhou;
        marcoMs_ = nowMs;
    }

    void voltarAoOcioso() {
        fase_ = Phase::Ocioso;
        motivo_ = FailReason::Nenhuma;
        recusa_ = HeaderVerdict::Ok;
        gravados_ = 0;
        total_ = 0;
    }

    PackageHeader cabecalho_;
    Phase fase_;
    FailReason motivo_;
    HeaderVerdict recusa_;
    bool exigeConfirmacao_;
    uint32_t marcoMs_;
    uint32_t inicioGravacaoMs_;
    uint32_t ultimoProgressoMs_;
    uint32_t gravados_;
    uint32_t total_;
};

}  // namespace ota
