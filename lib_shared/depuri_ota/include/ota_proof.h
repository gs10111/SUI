// PROVA DE BOOT DE UMA IMAGEM RECEM-SUBIDA. Puro: sem Arduino, sem esp_*, sem float, sem heap.
// Compila no host das duas placas.
//
// O PROBLEMA QUE ELE RESOLVE, conferido no core instalado
// (cores/esp32/esp32-hal-misc.c:203-238, com CONFIG_APP_ROLLBACK_ENABLE=y):
//
//     bool verifyRollbackLater() __attribute__((weak));   // devolve false
//     bool verifyOta()           __attribute__((weak));   // devolve TRUE
//     void initArduino() {
//         if (!verifyRollbackLater()) {
//             if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
//                 if (verifyOta()) esp_ota_mark_app_valid_cancel_rollback();
//                 ...
//
// De fabrica, portanto, o ESP32 marca como VALIDA qualquer imagem que apenas chegue a rodar
// initArduino() - ANTES do setup(). Uma sensora atualizada que sobe e nao fala com o SCL3300
// fica marcada como boa e NAO ha rollback automatico; a 500 m, dentro de um modulo, isso vira
// caminhonete com cabo USB.
//
// A saida e sobrescrever verifyRollbackLater() com uma definicao FORTE que devolve true - o core
// pula o bloco inteiro - e decidir aqui, com criterio explicito e prazo.
//
// O CRITERIO NAO PODE SER "o firmware subiu": e exatamente o que o core ja fazia. Tem de ser
// evidencia de que a FUNCAO do equipamento voltou, por ciclos CONSECUTIVOS, e tem de ter PRAZO -
// uma prova sem prazo deixa a particao em PENDING_VERIFY para sempre e a placa cai em rollback
// no proximo ciclo de energia, num laco que ninguem entende.
//
// QUEM CHAMA noteGood()/noteBad() e cada placa, com o seu proprio criterio de "funcionando":
//   UR      - um ciclo de controle com leitura da sensora aceita;
//   sensora - uma leitura do SCL3300 com status valido.
// Este modulo nao sabe o que e um ciclo bom, e de proposito: ele so conta.
#pragma once

#include <stdint.h>

namespace ota {

// Ciclos CONSECUTIVOS de funcionamento normal exigidos. Cinco e o mesmo numero que a UR ja usa
// para sair de falha de enlace (kGoodsToRecover), e por isso nao inventa uma segunda nocao de
// "recuperado" no mesmo produto.
constexpr uint8_t kProofGoodCycles = 5;

// Prazo total da prova. Tem de ser MAIOR que a recuperacao normal do equipamento, senao uma
// imagem boa reprova: a UR leva 5 quadros bons mais 2000 ms de permanencia minima para sair de
// falha de enlace, e a sensora ate 1700 ms para se recuperar de um afundamento de alimentacao.
// 30 s da margem larga para um boot lento, um enlace que demora a subir e uma sensora que ainda
// esta no start-up do SCL3300 - e continua curto o bastante para o tecnico ver o resultado
// enquanto ainda esta ao lado do equipamento.
constexpr uint32_t kProofDeadlineMs = 30000;

static_assert(kProofDeadlineMs >= 30000u, "prazo menor que a recuperacao normal reprova imagem boa");
static_assert(kProofGoodCycles >= 5u, "menos ciclos que o criterio de recuperacao ja usado na UR");

enum class ProofVerdict : uint8_t {
    Provando,   // ainda dentro do prazo, sem ciclos bons suficientes
    Aprovado,   // marque a particao como valida e cancele o rollback
    Reprovado,  // prazo vencido: marque invalida e reinicie para a particao anterior
};

class BootProof {
public:
    BootProof() : startMs_(0), goodCycles_(0), started_(false), approved_(false) {}

    // Ancora o prazo. Chamado UMA vez, onde a prova comeca - na primeira passagem do laco, ja
    // com o relogio de pe. Nao ancora no construtor porque o objeto e global e o relogio so
    // existe depois do setup().
    //
    // ESQUECER begin() FALHA PARA O LADO SEGURO, e isso e escolha: sem ancora, verdict() devolve
    // Provando para sempre, ninguem marca a particao como valida, ela fica em PENDING_VERIFY e o
    // proximo ciclo de energia faz o bootloader voltar para a particao anterior. A imagem nova e
    // recusada em vez de aceita sem prova.
    void begin(uint32_t nowMs) {
        if (started_) {
            return;
        }
        startMs_ = nowMs;
        started_ = true;
    }

    void noteGood(uint32_t nowMs) {
        (void)nowMs;
        if (approved_ || !started_) {
            return;
        }
        if (goodCycles_ < kProofGoodCycles) {
            ++goodCycles_;
        }
        if (goodCycles_ >= kProofGoodCycles) {
            approved_ = true;
        }
    }

    // Um ciclo ruim ZERA a contagem: a prova e de funcionamento CONTINUO. Ciclos bons
    // intercalados com ruins descrevem um equipamento intermitente, que e exatamente o que nao
    // pode ser dado por bom e marcado como valido para sempre.
    void noteBad() {
        if (approved_) {
            return;
        }
        goodCycles_ = 0;
    }

    ProofVerdict verdict(uint32_t nowMs) const {
        // APROVADO E DEFINITIVO: depois de marcada valida, a particao nao volta a
        // PENDING_VERIFY. Continuar "provando" seria mentira, e um ciclo ruim depois da
        // aprovacao nao pode disparar rollback - isso reiniciaria a placa em operacao normal.
        if (approved_) {
            return ProofVerdict::Aprovado;
        }
        if (!started_) {
            return ProofVerdict::Provando;
        }
        // Subtracao unsigned: atravessa o wrap de 2^32 sem caso especial. A UR fica energizada
        // meses; uma comparacao escrita como "now > inicio + prazo" reprovaria na hora se a
        // imagem subisse perto do wrap.
        if ((nowMs - startMs_) >= kProofDeadlineMs) {
            return ProofVerdict::Reprovado;
        }
        return ProofVerdict::Provando;
    }

    uint8_t goodCycles() const { return goodCycles_; }
    bool approved() const { return approved_; }
    bool started() const { return started_; }

private:
    uint32_t startMs_;
    uint8_t goodCycles_;
    bool started_;
    bool approved_;
};

}  // namespace ota
