// Senha de acesso ao Modo Programacao: guarda o valor de 4 digitos, compara a senha digitada
// e governa o momento em que uma senha nova passa a valer.
//
// Manual SUI-DI141388XY (docs/manual-cliente-sui-2026.txt, citado por numero de linha do
// arquivo bruto):
//   5.1  L82  - "Para acessar o Modo Programacao, e necessario informar a senha de acesso".
//   5.2  L90  - MENU mantido ~3 s abre o Modo Programacao mediante a senha.
//   5.3  L99 e L106 - padrao de fabrica 1234 ("A senha 1234 vem configurada de fabrica" e a
//                observacao "A senha padrao de fabrica e 1234", restaurada no reset geral).
//   5.3  L101 - a senha e digitada digito a digito na tela de login.
//   5.3  L103 - senha correta abre o Menu de Opcoes; senha errada mostra "Senha incorreta!"
//                por alguns segundos e PERMITE nova tentativa.
//   5.3  L105 - "retornara ao Modo Normal apos aproximadamente 2 minutos de inatividade".
//   Tabela 1 L131 - "Senha | 0000 a 9999 (padrao de fabrica: 1234)".
//   5.4  L136 - o Modo Programacao tambem sai por timeout de ~2 minutos sem tecla.
//   5.10 L224 a L237 - edicao da senha; L237: "A nova senha somente passara a ser utilizada
//                nos proximos acessos ao Modo Programacao".
//
// Decisao A13: a efetivacao e unica e acontece no SAIR - por isso a senha editada fica
// ENCOSTADA (stage) e so troca a vigente em commitOnExit(); ate la, e a senha antiga que
// autoriza o acesso, que e exatamente o que L237 manda. commitOnExit() SEM edicao encostada e
// obrigatoriamente inerte: sem essa guarda, todo SAIR do Modo Programacao regravaria o valor
// encostado e a senha do painel voltaria sozinha para a de fabrica, sem sinal na tela.
// O timeout de 120 s nao apaga a edicao encostada: A13 opcao A deixa a configuracao pendente
// para revisao em vez de descartar em silencio. O bloqueio de 5 tentativas por 60 s e
// explicitamente TEMPORARIO e VOLATIL: vive so na RAM e nao sobrevive ao boot, porque bloqueio
// permanente transformaria erro de digitacao num painel de cais em visita de manutencao.
//
// Toda medida de prazo (bloqueio de 60 s e inatividade de 120 s) passa por deadlineReached()
// de ports/i_clock.h, nunca por "a > b": o tempo aqui e tempo de relogio injetado e tem de
// atravessar o wrap de 2^32 ms intacto.
//
// O que fazer quando o valor lido da memoria nao e um numero de 4 digitos continua sem decisao
// (Decisao 13, pendencia f): por isso load() apenas RECUSA o valor impossivel e devolve false,
// deixando a politica com o chamador, em vez de restaurar 1234 por conta propria.
#pragma once

#include <stdint.h>

#include "ports/i_clock.h"

namespace domain {

enum class AccessResult : uint8_t {
    Granted,
    Wrong,
    Locked,
};

class Password {
public:
    static constexpr uint16_t kFactory = 1234;
    static constexpr uint16_t kMaxValue = 9999;
    static constexpr uint8_t kMaxAttempts = 5;
    static constexpr uint32_t kLockoutMs = 60000u;
    static constexpr uint32_t kInactivityMs = 120000u;

    explicit Password(const IClock& clock);

    static constexpr bool inRange(uint16_t candidate) { return candidate <= kMaxValue; }

    bool load(uint16_t stored);
    uint16_t effective() const;

    AccessResult submit(uint16_t typed);
    bool locked() const;
    uint8_t attemptsLeft() const;

    bool stage(uint16_t candidate);
    bool staged() const;
    void commitOnExit();

    void noteActivity();
    bool timedOut() const;

private:
    const IClock& clock_;
    uint32_t lockSinceMs_;
    uint32_t lastActivityMs_;
    uint16_t stored_;
    uint16_t staged_;
    uint8_t attempts_;
    bool hasStaged_;
    bool lockActive_;
};

}  // namespace domain
