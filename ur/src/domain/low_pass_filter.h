// Filtro passa-baixa de um polo (EMA) em ponto fixo Q8 sobre decimos de grau, com recarga por
// salto. E o unico estagio de filtragem da UR e o dono do numero que segue para display, saida
// analogica e comparacao dos quatro limites.
//
// REQ-MEA-04. Manual SUI-DI141388XY: L71 ("filtro passa-baixa, permitindo adequar o tempo de
// resposta do equipamento as caracteristicas de aplicacao"), L272 ("reduzir os efeitos de
// pequenas oscilacoes e vibracoes"), L142 e L223 (o ponto de atuacao do rele e ajustado
// exatamente no angulo desejado, e o numero comparado e o da leitura do display).
//
// Decisao A6, APROVADA (DECISIONS.md L67 e L249): constante de tempo FIXA de 0,8 s, recarga por
// salto de 2,0 grau SEMPRE ativa, e os quatro degraus 0,2 / 0,8 / 1,6 / 3,2 s existentes apenas
// no fluxo de comissionamento, NUNCA no menu do operador. Por isso este modulo nao expoe
// nenhuma tela nem nenhum item de menu: expoe um construtor e um setTimeConstant() que a camada
// de comissionamento chama, e o default de fabrica kTauDefaultMs.
//
// BASE COMUM 2.1 item 4 e 2.5 (kFilterPeriodMs = kCtrlPeriodMs = 50 ms): a constante de tempo e
// especificada em MILISSEGUNDOS e convertida em coeficiente NESTE periodo, nunca o contrario. O
// periodo de amostragem entra por parametro justamente para que a base de tempo continue sendo
// propriedade da tarefa ctrl e nao um numero escondido aqui dentro. shiftFor() escolhe o k
// inteiro cuja constante realizavel fica mais proxima da pedida; realizedTimeConstantMs()
// devolve a que o equipamento realmente tem, que e a que a folha de dados publica.
//
// A CONTA. Com passo de tempo Ts e coeficiente 2^-k, a constante de tempo do EMA vale
// tau = -Ts/ln(1 - 2^-k). Para k >= 2 essa expressao e igual, com erro abaixo de 1 %, a
// Ts*(2^(k+1) - 1)/2, que e uma multiplicacao e um deslocamento em inteiro - e por isso a
// conversao ms -> coeficiente nao precisa de logaritmo nem de ponto flutuante. Com Ts = 50 ms:
//
//   degrau A6   k   tau realizavel   erro contra o rotulo
//   0,2 s       2      175 ms            -25 ms
//   0,8 s       4      775 ms            -25 ms
//   1,6 s       5     1575 ms            -25 ms
//   3,2 s       6     3175 ms            -25 ms
//
// ZONA MORTA - o defeito de seguranca que este modulo existe para nao ter. Um EMA inteiro
// ingenuo (y += (x*256 - y) >> k) para de andar assim que a diferenca fica menor que 2^k LSB de
// Q8: o valor filtrado congela a alguns centesimos de grau do valor real e ali fica para
// sempre. Numa rampa lenta sobre um limite isso desloca o ponto de atuacao do rele - o rele
// atua no angulo errado, ou nao atua. Aqui a zona morta e eliminada na raiz por um passo
// calculado sobre o MODULO da diferenca, deslocado de k, com PISO DE 1 LSB de Q8 enquanto a
// diferenca nao for zero. A convergencia e monotonica e EXATA nos dois sentidos: em regime
// permanente y = x*256 bit a bit, o filtro nao contribui com nenhum erro de quantizacao, e em
// diferenca zero o estado e estavel, sem ciclo limite. O preco e um trecho final lento,
// limitado a 2^k - 1 passos. Nao ha ultrapassagem: o passo nunca excede a propria diferenca.
//
// SIMETRIA. O passo sai do modulo e so depois recebe o sinal, e nao de um deslocamento
// aritmetico da diferenca com sinal - esse arredonda para -infinito e faria a descida ser mais
// rapida que a subida. O valor filtrado de -x e o espelho exato do de +x em todo o transitorio,
// que e o que a operacao "+" da secao 5.9 (L207 e L208) exige, porque ela compara o MODULO do
// angulo: sem espelho o mesmo rele atuaria em instantes diferentes conforme o lado para o qual
// a estrutura inclina.
//
// RECARGA POR SALTO (nao linearidade declarada, A6). Se a amostra diferir do estado em 2,0 grau
// ou mais, o estado e RECARREGADO com a amostra no mesmo tick, em vez de filtrado. Nao e um
// segundo caminho de decisao nem um segundo numero: e o proprio e unico valor que salta, e
// display, saida analogica e comparacao de rele o veem saltar juntos. Ancoragem no manual: L71
// fala de "pequenas variacoes transitorias" e L272 de "pequenas oscilacoes"; uma excursao de
// 2,0 grau - vinte vezes a resolucao publicada - nao e pequena, e segui-la de imediato e o
// comportamento fiel. Consequencia publicada: qualquer excursao de 2,0 grau ou mais atua o rele
// no mesmo prazo em QUALQUER ajuste de filtro, inclusive em 3,2 s.
//
// DIRIGIDO POR TEMPO, NAO POR AMOSTRA. Todo tick de 50 ms executa exatamente um passo. Um tick
// sem transacao valida nao envenena nem congela o estado: o passo e dado contra a ultima
// amostra valida (retencao de ordem zero) e held() sinaliza a retencao. Assim o tau publicado e
// o tau real - um enlace que perca quadros esparsos soma atraso de transporte, nao multiplica a
// constante de tempo - e nenhum valor inventado entra na conta. Enquanto nao houver a primeira
// amostra valida nao existe leitura: update() devolve Angle::invalid(), porque um filtro nao
// pode inventar o angulo que nunca mediu.
//
// O QUE NAO REINICIALIZA O FILTRO (A6 e decisao 4 item 9). Trocar a constante de tempo NAO
// reinicializa: so k muda, y continua nas mesmas unidades, e a mudanca so altera a velocidade
// de convergencia a partir do tick seguinte. Entrar ou sair do Modo Programacao tambem nao.
// Consequencia deliberada: nenhum commit de parametro pulsa rele. A recarga explicita, por
// reload(), tem uma lista fechada de tres motivos - primeira amostra valida apos o boot,
// primeira amostra valida apos sair da falha de enlace, e reset detectado da placa sensora.
//
// FRONTEIRAS. Este modulo fica A MONTANTE de Sentido do Sensor e de Preset, que sao lineares
// (troca de sinal e soma de constante) e comutam com o EMA: assim o commit de Preset produz um
// degrau exato no valor exibido, com transitorio de filtro igual a zero. Ele nao sabe o que e
// limite, histerese ou rele - quem compara e LimitEvaluator, que le apenas o Angle publicado
// aqui. E nao ha estouro possivel: |y| <= 900 * 256 = 230400 e a maior diferenca representavel
// vale 460800, quatro ordens de grandeza abaixo do teto de int32.
#pragma once

#include <stdint.h>

#include "domain/angle.h"

namespace domain {

class LowPassFilter {
public:
    static constexpr int32_t kScale = 256;
    static constexpr int16_t kJumpReloadDeci = 20;
    static constexpr uint8_t kMaxShift = 12;

    static constexpr uint16_t kTauStep1Ms = 200;
    static constexpr uint16_t kTauStep2Ms = 800;
    static constexpr uint16_t kTauStep3Ms = 1600;
    static constexpr uint16_t kTauStep4Ms = 3200;
    static constexpr uint16_t kTauDefaultMs = kTauStep2Ms;

    static uint8_t shiftFor(uint16_t timeConstantMs, uint16_t samplePeriodMs);
    static uint32_t realizedTimeConstantMs(uint8_t shift, uint16_t samplePeriodMs);

    LowPassFilter(uint16_t timeConstantMs, uint16_t samplePeriodMs);

    void setTimeConstant(uint16_t timeConstantMs);

    uint8_t shift() const { return shift_; }
    uint16_t samplePeriodMs() const { return periodMs_; }
    uint32_t timeConstantMs() const { return realizedTimeConstantMs(shift_, periodMs_); }

    Angle update(const Angle& sample);
    Angle value() const;

    void reload(const Angle& sample);
    void reset();

    bool primed() const { return primed_; }
    bool reloaded() const { return reloaded_; }
    bool held() const { return held_; }

private:
    int32_t state_;
    int16_t holdDeci_;
    uint16_t periodMs_;
    uint8_t shift_;
    bool primed_;
    bool reloaded_;
    bool held_;
};

}  // namespace domain
