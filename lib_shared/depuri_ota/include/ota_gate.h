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
//   - kSemClienteMs  - ativou e ninguem conectou. O caso comum e o tecnico que ativou, foi
//                      chamado para outra coisa e esqueceu. Dez minutos.
//   - kTetoMs        - alguem conectou e ficou. Um celular esquecido no bolso, ligado na rede do
//                      equipamento, seguraria o ponto de acesso no ar por dias sem este teto.
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

constexpr uint32_t kSemClienteMs = 600000;   // 10 min sem ninguem conectar
constexpr uint32_t kTetoMs = 3600000;        // 60 min no ar, mesmo com cliente

constexpr bool codigoCorreto(uint16_t digitado) { return digitado == kCodigoAtivacao; }

class ApGate {
public:
    ApGate() : ativoDesdeMs_(0), ultimoClienteMs_(0), ativo_(false), jaTeveCliente_(false) {}

    bool ativo() const { return ativo_; }

    void ativar(uint32_t nowMs) {
        // Reativar um portao ja aberto RENOVA os prazos. E o gesto de quem esta na frente do
        // painel dizendo "ainda estou aqui" - recusar seria obrigar a esperar o prazo vencer
        // para poder pedir de novo.
        ativoDesdeMs_ = nowMs;
        ultimoClienteMs_ = nowMs;
        ativo_ = true;
        jaTeveCliente_ = false;
    }

    void desativar() {
        ativo_ = false;
        jaTeveCliente_ = false;
    }

    // Chamada a cada volta do laco com o que o radio esta vendo.
    void tick(uint32_t nowMs, bool clienteConectado, bool sessaoEmCurso) {
        // GUARDA DEFENSIVA, e assumidamente equivalente hoje: nenhum caminho abaixo ABRE o
        // portao, so fecha, entao remove-la nao muda nada que se possa observar - um mutante que
        // a apaga sobrevive, e isso esta registrado aqui em vez de ser escondido atras de um
        // teste forjado. Ela fica porque o dia em que tick() passar a mexer em estado que
        // sobreviva a ativar() e o dia em que a falta dela vira defeito silencioso.
        if (!ativo_) {
            return;
        }
        if (clienteConectado) {
            ultimoClienteMs_ = nowMs;
            jaTeveCliente_ = true;
        }
        if (sessaoEmCurso) {
            // Uma gravacao em curso tem os prazos dela. Derrubar o radio aqui deixaria a particao
            // ociosa pela metade e a maquina em alarme ate a sessao morrer sozinha.
            return;
        }
        if (!jaTeveCliente_ && (nowMs - ativoDesdeMs_) >= kSemClienteMs) {
            desativar();
            return;
        }
        if ((nowMs - ativoDesdeMs_) >= kTetoMs) {
            desativar();
            return;
        }
        if (jaTeveCliente_ && (nowMs - ultimoClienteMs_) >= kSemClienteMs) {
            desativar();
        }
    }

    // Quanto falta para cair, em segundos, para a tela. 0 quando nao esta ativo.
    uint16_t restanteS(uint32_t nowMs) const {
        if (!ativo_) {
            return 0;
        }
        const uint32_t base = jaTeveCliente_ ? ultimoClienteMs_ : ativoDesdeMs_;
        const uint32_t gastoOcioso = nowMs - base;
        uint32_t faltaOcioso = (gastoOcioso >= kSemClienteMs) ? 0u : (kSemClienteMs - gastoOcioso);
        const uint32_t gastoTeto = nowMs - ativoDesdeMs_;
        const uint32_t faltaTeto = (gastoTeto >= kTetoMs) ? 0u : (kTetoMs - gastoTeto);
        if (faltaTeto < faltaOcioso) {
            faltaOcioso = faltaTeto;
        }
        return static_cast<uint16_t>(faltaOcioso / 1000u);
    }

private:
    uint32_t ativoDesdeMs_;
    uint32_t ultimoClienteMs_;
    bool ativo_;
    bool jaTeveCliente_;
};

}  // namespace ota
