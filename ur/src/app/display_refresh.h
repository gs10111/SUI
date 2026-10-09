// src/app/display_refresh.h
// REINICIO PERIODICO DO PAINEL (Decisao 20): quando o composition root deve chamar
// IDisplay::hardReset() fora do boot. Camada de aplicacao pura - nao inclui Arduino.h, nao
// conhece o painel e nao mede tempo; so diz "agora".
//
// POR QUE EXISTE. E preventivo, sem defeito observado: o CN4 nao tem MISO, entao um SSD1322 que
// perca a configuracao (ruido no cabo, surto) fica com a tela errada ou apagada e NADA no
// firmware percebe (IDisplay::verifiable() == false). Sem leitura de volta, a unica defesa e
// refazer o init de tempos em tempos, cegamente. O hardReset() devolve o painel com o QUADRO
// CORRENTE preservado, e o estado da IHM (tela, menu, edicao) mora em RAM e nao e tocado - o
// operador volta exatamente onde estava.
//
// O CUSTO, e por isso ha regras de adiamento: hardReset() bloqueia ate ~500 ms no loopTask. A
// tela fica preta nesse intervalo e as teclas nao sao lidas. A tarefa ctrl (core 0), os reles, o
// DAC e o cachorro NAO param - os delay() sao vTaskDelay e o token de liveness e da ctrl.
//
// REGRAS:
//  - kPeriodMs desde o ULTIMO reinicio (ou desde start(), no boot);
//  - ADIA enquanto houver tecla nos ultimos kQuietMs: piscar no meio de uma edicao perderia um
//    aperto. Reinicia na primeira passagem quieta;
//  - ADIA enquanto o painel for de outro dono (splash, autoteste, radio de atualizacao no ar);
//  - takeDue() CONSOME: ao responder true, o periodo recomeca daquele instante.
#pragma once

#include <stdint.h>

namespace app {

class DisplayRefresh {
public:
    static constexpr uint32_t kPeriodMs = 300000;  // 5 min
    static constexpr uint32_t kQuietMs = 10000;    // 10 s sem tecla

    DisplayRefresh() : lastResetMs_(0), lastKeyMs_(0), keySeen_(false) {}

    // Chamada no fim do setup(): o primeiro reinicio vem kPeriodMs depois do boot.
    void start(uint32_t nowMs) {
        lastResetMs_ = nowMs;
        keySeen_ = false;
    }

    // Chamada em toda passagem do loop() com alguma tecla apertada.
    void noteKey(uint32_t nowMs) {
        lastKeyMs_ = nowMs;
        keySeen_ = true;
    }

    // Chamada na passagem da IHM. true = reinicie o painel agora.
    bool takeDue(uint32_t nowMs, bool panelBusy) {
        if (keySeen_ && (nowMs - lastKeyMs_) >= kQuietMs) {
            // Esquecer a tecla assim que a quietude vence: uma marca guardada para sempre
            // pareceria recente de novo quando nowMs - lastKeyMs_ desse a volta em 2^32 ms.
            keySeen_ = false;
        }
        if (panelBusy || keySeen_) {
            return false;
        }
        if ((nowMs - lastResetMs_) < kPeriodMs) {
            return false;
        }
        lastResetMs_ = nowMs;
        return true;
    }

private:
    uint32_t lastResetMs_;
    uint32_t lastKeyMs_;
    bool keySeen_;
};

}  // namespace app
