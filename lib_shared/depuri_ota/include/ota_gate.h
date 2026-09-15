// O PORTAO DO PONTO DE ACESSO: quando ele sobe, e quando ele cai sozinho.
//
// MUDANCA DE DESENHO EM 2026-09-14. Antes o ponto de acesso ficava no ar 100% do tempo, nas duas
// placas. Agora ele sobe SOB COMANDO, a partir do menu da supervisora, atras de um codigo fixo.
// A troca encolhe a janela de exposicao de "sempre" para "os minutos em que alguem esta
// atualizando", e e o unico ganho de seguranca real que este produto teve depois que a senha
// virou fixa - porque uma senha publicada num ponto de acesso que so existe por dez minutos, com
// um tecnico na frente do painel, e um problema muito menor do que a mesma senha num ponto de
// acesso permanente.
//
// POR QUE HA PRAZO, E POR QUE ELE NAO E UM SO:
//
//   - kSemAtividadeMs - o radio esta no ar e NADA CHEGA. Cobre os dois casos de uma vez: o
//                       tecnico que ativou e foi chamado para outra coisa, e o celular esquecido
//                       no bolso, associado a rede e sem pedir nada. Vinte minutos.
//   - kTetoMs         - alguem esta MESMO usando, e continua usando. A propria pagina consulta o
//                       estado a cada 700 ms enquanto estiver aberta, entao uma aba esquecida
//                       aberta renovaria o prazo de atividade para sempre sem este teto.
//
// A CONTA E DE ATIVIDADE, E NAO DE CLIENTE ASSOCIADO, e a diferenca e a que importa em campo: um
// celular no bolso continua associado a rede por horas sem enviar nada. Contar associacao como
// vida deixaria o radio ligado exatamente no caso que o prazo existe para cobrir.
//
// E POR QUE O TETO NAO VALE DURANTE UMA ATUALIZACAO: derrubar o radio no meio de uma gravacao
// deixaria a particao ociosa pela metade e a maquina em alarme ate a sessao morrer por prazo
// proprio. A sessao ja tem os relogios dela (ota_session.h); este portao nao pode atropela-los.
#pragma once

#include <stdint.h>

namespace ota {

// CODIGO DE ATIVACAO, fixo e nao configuravel.
//
// NAO E a senha do Modo Programacao (1234, que o cliente troca) e nao e a senha do WiFi
// (dieletrons-2025). Sao tres coisas com tres propositos:
//   - 1234          diz "posso mexer na configuracao"   - o cliente troca
//   - 1976          diz "posso ligar o radio"           - fixo, so quem faz manutencao sabe
//   - dieletrons-2025 diz "posso falar com o radio"     - fixo, vai na etiqueta
// Ser fixo e deliberado: um cliente que troque a senha do Modo Programacao e a esqueca nao pode,
// com isso, ficar sem caminho de atualizacao. E saber 1234 nao basta para ligar o radio.
constexpr uint16_t kCodigoAtivacao = 1976;

constexpr uint32_t kSemAtividadeMs = 1200000;  // 20 min sem nada chegar
constexpr uint32_t kTetoMs = 3600000;          // 60 min no ar, mesmo com uso continuo

constexpr bool codigoCorreto(uint16_t digitado) { return digitado == kCodigoAtivacao; }

class ApGate {
public:
    ApGate() : ativoDesdeMs_(0), ultimaAtividadeMs_(0), ativo_(false) {}

    bool ativo() const { return ativo_; }

    void ativar(uint32_t nowMs) {
        // Reativar um portao ja aberto RENOVA os prazos. E o gesto de quem esta na frente do
        // painel dizendo "ainda estou aqui" - recusar seria obrigar a esperar o prazo vencer
        // para poder pedir de novo.
        ativoDesdeMs_ = nowMs;
        ultimaAtividadeMs_ = nowMs;
        ativo_ = true;
    }

    void desativar() { ativo_ = false; }

    // Chamada a cada volta do laco. `houveAtividade` e verdadeiro quando ALGO CHEGOU pelo radio
    // desde o tique anterior - uma requisicao a pagina, um pedaco de imagem. Nao e "ha alguem
    // conectado": ver o cabecalho.
    void tick(uint32_t nowMs, bool houveAtividade, bool sessaoEmCurso) {
        // GUARDA DEFENSIVA, e assumidamente equivalente hoje: nenhum caminho abaixo ABRE o
        // portao, so fecha, entao remove-la nao muda nada que se possa observar - um mutante que
        // a apaga sobrevive, e isso esta registrado aqui em vez de ser escondido atras de um
        // teste forjado. Ela fica porque o dia em que tick() passar a mexer em estado que
        // sobreviva a ativar() e o dia em que a falta dela vira defeito silencioso.
        if (!ativo_) {
            return;
        }
        if (houveAtividade) {
            ultimaAtividadeMs_ = nowMs;
        }
        if (sessaoEmCurso) {
            // Uma gravacao em curso tem os prazos dela. Derrubar o radio aqui deixaria a particao
            // ociosa pela metade e a maquina em alarme ate a sessao morrer sozinha.
            return;
        }
        if ((nowMs - ultimaAtividadeMs_) >= kSemAtividadeMs) {
            desativar();
            return;
        }
        if ((nowMs - ativoDesdeMs_) >= kTetoMs) {
            desativar();
        }
    }

    // Quanto falta para cair, em segundos, para a tela. 0 quando nao esta ativo.
    uint16_t restanteS(uint32_t nowMs) const {
        if (!ativo_) {
            return 0;
        }
        const uint32_t gastoOcioso = nowMs - ultimaAtividadeMs_;
        uint32_t faltaOcioso =
            (gastoOcioso >= kSemAtividadeMs) ? 0u : (kSemAtividadeMs - gastoOcioso);
        const uint32_t gastoTeto = nowMs - ativoDesdeMs_;
        const uint32_t faltaTeto = (gastoTeto >= kTetoMs) ? 0u : (kTetoMs - gastoTeto);
        if (faltaTeto < faltaOcioso) {
            faltaOcioso = faltaTeto;
        }
        return static_cast<uint16_t>(faltaOcioso / 1000u);
    }

private:
    uint32_t ativoDesdeMs_;
    uint32_t ultimaAtividadeMs_;
    bool ativo_;
};

}  // namespace ota
