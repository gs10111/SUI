// Agregado de configuracao da Unidade Remota: os parametros que o operador programa e a
// calibracao das duas saidas analogicas, em DOIS registros serializados separadamente.
//
// Manual SUI-DI141388XY: 5.4 (L112/L113 e Tabela 1, faixas de programacao), 5.6 (L144, Preset
// de -90,0 a +90,0 graus), 5.7 (L165 a L186, Auto Calibracao de dois pontos por eixo),
// 5.8 (Sentido do Sensor por eixo), 5.9 (L205 a L223, os quatro limites com valor e operacao),
// 5.10 (L224 a L237, senha de quatro digitos), 5.11 e Tabela 2 (L239 a L267, padroes de fabrica).
// REQ: PER-01 (manual 7, L308: parametros retidos sem bateria e leitura relativa restabelecida
// na religacao - por isso o offset de PSET tambem e campo persistido), RST-01 e RST-02 (o Reset
// Geral repoe a Tabela 2 e RESTAURA - nao apaga - a calibracao analogica gravada em fabrica).
//
// Decisao A8, opcao C, aprovada em 2026-08-31: o registro que COMANDA RELE e o registro de
// CALIBRACAO ANALOGICA sao separados, cada um com magic, versao e CRC-16/MODBUS proprios.
// Corrupcao na calibracao nao pode derrubar os setpoints e corrupcao nos setpoints nao pode
// apagar a calibracao. Cada grupo e carregado por inteiro ou recusado por inteiro: carga
// recusada nao toca em campo nenhum, e um CRC aprovado nao autoriza valor fora de faixa.
//
// Decisao A13: este agregado nao efetiva nada e nao decide politica de boot. Quem escolhe o
// instante da efetivacao (SAIR), o que fazer diante de um registro reprovado e quem pode
// carregar factoryDefaults() e a camada de aplicacao - aqui so existem valor valido, blob e
// recusa. Nao ha prazo nenhum neste agregado, e por isso ele nao recebe IClock.
//
// FRONTEIRA COM O BANCO DUPLO (leitura unica, para nao restar duvida): este arquivo define o
// CONTEUDO de um registro, nao a politica de armazenamento. Numero de sequencia, escolha do
// banco mais novo entre ParamSlot::BankA e ParamSlot::BankB, auto-cura e edicao pendente sao
// do ParamStoreLogic, que ENVELOPA este blob (o campo de versao existe para isso, decisao 2
// item 3 a 8 e 11). Portanto: o blob de parametros NAO e, sozinho, "o que vai na chave
// par_a/par_b" - e a carga util que o ParamStoreLogic grava la dentro, junto do proprio
// contador de geracao. Nenhuma comparacao de idade e possivel aqui e nenhuma e prometida.
// Tamanhos: 32 bytes o registro de parametros e 20 o de calibracao, cada um em seu proprio
// slot, ambos abaixo do capacityBytes() de ports/i_parameter_store.h. Os slots existentes sao
// BankA, BankB e FactoryCal - nao existe slot "cal_usr".
//
// Calibracao NOMINAL x calibracao de FABRICA: factoryDefaults() entrega os codigos NOMINAIS de
// projeto da cadeia bipolar (kNominalCalZeroCode / kNominalCalFullScaleCode). Eles nao sao a
// calibracao de fabrica: esta e medida unidade a unidade pelo jig e vive em ParamSlot::FactoryCal,
// e o Reset Geral do manual 5.11 (L240) manda RESTAURA-LA. O nominal e ultimo recurso, valido
// so quando o slot de fabrica esta ilegivel.
//
// GATE DE PLAUSIBILIDADE DA CALIBRACAO: UM SO, E ELE NAO MORA AQUI (A14, "com o gate de
// plausibilidade do commit unificado em um unico criterio, porque hoje as decisoes 6, 9 e 10
// usam tres, e uma calibracao legitima aprovada por um e recusada por outro"). Quem decide se
// um par (zero, fundo de escala) e legitimo e domain::AnalogScaler::make(), e mais ninguem.
// Ate a etapa 8 este agregado carregava uma SEGUNDA janela propria - zero em [27768, 37767] e
// codigo de fundo de escala em [53982, 63981], derivada do trim de +/-5000 LSB aplicado a cada
// ponto ISOLADO - e essa janela discordava do gate do assistente, que trima o zero e o VAO.
// Com campoZero + campoGanho abaixo de 5000 (por exemplo 2000 e 2500, angulo de fundo 30,0
// graus) o assistente aprovava o par 29768/53482 e este agregado o recusava em silencio,
// gravando o angulo novo sobre os codigos velhos: MEIO PAR, que A14 declara pior que nenhuma
// calibracao porque produz saida plausivel e errada sem assinatura observavel. A segunda janela
// foi REMOVIDA; sobrou o criterio do AnalogScaler, e o static_assert abaixo prende a unica
// parte do gate que este arquivo ainda precisa repetir (a faixa do angulo de fundo de escala).
#pragma once

#include <stdint.h>

#include "domain/analog_scaler.h"
#include "domain/angle.h"
#include "status.h"

namespace domain {

enum class Axis : uint8_t { X = 0, Y = 1 };

// Limites 1 e 2 sao do eixo X, 3 e 4 do eixo Y (manual 5.9, L207).
enum class LimitId : uint8_t { X1 = 0, X2 = 1, Y1 = 2, Y2 = 3 };

// Manual 5.9, L208 a L211.
enum class LimitOp : uint8_t { Off = 0, GreaterEqual = 1, LessEqual = 2, Absolute = 3 };

// Manual 5.8, Tabela 2 L264/L265.
enum class SensorDir : uint8_t { Clockwise = 0, CounterClockwise = 1 };

class Parameters {
public:
    static constexpr uint8_t kAxisCount = 2;
    static constexpr uint8_t kLimitCount = 4;

    static constexpr uint32_t kParamMagic = 0x44505231u;  // "DPR1"
    static constexpr uint32_t kCalMagic = 0x44435231u;    // "DCR1"
    static constexpr uint16_t kParamVersion = 1;
    static constexpr uint16_t kCalVersion = 1;
    static constexpr uint16_t kParamBlobSize = 32;
    static constexpr uint16_t kCalBlobSize = 20;

    static constexpr uint16_t kPasswordMax = 9999;
    static constexpr int16_t kCalFullScaleMinDeci = 1;
    static constexpr int16_t kCalFullScaleMaxDeci = Angle::kMaxDeciDeg;

    // A9: offset := P - dir * bruto, com P e dir*bruto em [-900,+900]. O offset de PSET nao e
    // um angulo medido e por isso nao e um Angle: sua faixa e o DOBRO da faixa de medicao.
    static constexpr int16_t kPresetOffsetMinDeci = -1800;
    static constexpr int16_t kPresetOffsetMaxDeci = 1800;

    // Cadeia bipolar V_OUT(D) = 25*D/65536 - 12,5 V: 32768 = 0,00 V, 58982 = +10,00 V. Estes
    // sao os codigos NOMINAIS de projeto, nao a calibracao medida em fabrica.
    static constexpr uint16_t kNominalCalZeroCode = 32768;
    static constexpr uint16_t kNominalCalFullScaleCode = 58982;

    // AMARRACAO DO GATE UNICO. A faixa do angulo de fundo de escala e a unica parte do criterio
    // de A14 que este arquivo repete (porque setCalFullScale escreve so esse campo); as duas
    // constantes tem de continuar sendo LITERALMENTE as do AnalogScaler, senao voltam a existir
    // dois gates. Se alguem mexer num dos lados, o build para aqui.
    static_assert(kCalFullScaleMinDeci == AnalogScaler::kFullScaleMinDeci,
                  "o piso do fundo de escala tem de ser o do gate unico de A14");
    static_assert(kCalFullScaleMaxDeci == AnalogScaler::kFullScaleMaxDeci,
                  "o teto do fundo de escala tem de ser o do gate unico de A14");

    // Monotonicidade da saida analogica (manual 5.7: "proporcional e simetrica") tambem sai do
    // gate unico e nao de janelas disjuntas: make() exige vao >= kSpanMin = 20971 codigos, logo
    // fullScaleCode > zeroCode por construcao, e par degenerado ou invertido nunca e aceito.
    static_assert(AnalogScaler::kSpanMin > 0,
                  "vao minimo positivo e o que proibe par degenerado ou invertido");

    // Tabela 2, L250 a L267.
    static constexpr int16_t kDefaultPresetDeci = 0;
    static constexpr int16_t kDefaultLimitDeci = 50;
    static constexpr int16_t kDefaultLimitOffDeci = 0;
    static constexpr uint16_t kDefaultPassword = 1234;
    static constexpr int16_t kDefaultCalFullScaleDeci = 450;

    // Seletor vindo da IHM (indice de menu) ou de um blob e dado externo, nao invariante de
    // tipo: enum de C++ nao restringe o valor castado. Sem estas duas guardas o indice errado
    // vira escrita fora dos limites do agregado que comanda os quatro reles.
    static constexpr bool axisValid(Axis axis) { return static_cast<uint8_t>(axis) < kAxisCount; }
    static constexpr bool limitIdValid(LimitId id) { return static_cast<uint8_t>(id) < kLimitCount; }

    Parameters();

    // Tabela 2 literal, com os codigos de DAC NOMINAIS. Quem tem direito de chamar isto e a
    // decisao da camada de aplicacao: A8 proibe carregar padroes em silencio diante de um
    // registro reprovado, e o Reset Geral do manual 5.11 recarrega a calibracao de fabrica
    // por cima destes nominais.
    static Parameters factoryDefaults();

    // Getters com seletor invalido devolvem o valor SEGURO, nunca memoria vizinha: angulo
    // invalido (nao existe leitura), operacao Off (rele em repouso, manual 5.9 L208) e o
    // codigo nominal da saida analogica.
    Angle preset(Axis axis) const {
        return axisValid(axis) ? Angle::fromDeciDegrees(rel_.presetDeci[idx(axis)]) : Angle::invalid();
    }
    int16_t presetOffsetDeci(Axis axis) const {
        return axisValid(axis) ? rel_.presetOffsetDeci[idx(axis)] : static_cast<int16_t>(0);
    }
    SensorDir sensorDir(Axis axis) const {
        return axisValid(axis) ? static_cast<SensorDir>(rel_.sensorDir[idx(axis)])
                               : SensorDir::Clockwise;
    }
    Angle limitValue(LimitId id) const {
        return limitIdValid(id) ? Angle::fromDeciDegrees(rel_.limitDeci[idx(id)]) : Angle::invalid();
    }
    LimitOp limitOp(LimitId id) const {
        return limitIdValid(id) ? static_cast<LimitOp>(rel_.limitOp[idx(id)]) : LimitOp::Off;
    }
    uint16_t password() const { return rel_.password; }

    Angle calFullScale(Axis axis) const {
        return axisValid(axis) ? Angle::fromDeciDegrees(cal_.fullScaleDeci[idx(axis)])
                               : Angle::invalid();
    }
    uint16_t calZeroCode(Axis axis) const {
        return axisValid(axis) ? cal_.zeroCode[idx(axis)] : kNominalCalZeroCode;
    }
    uint16_t calFullScaleCode(Axis axis) const {
        return axisValid(axis) ? cal_.fullScaleCode[idx(axis)] : kNominalCalFullScaleCode;
    }

    // Err::Param quando o SELETOR nao existe (defeito de chamador), Err::Range quando o VALOR
    // nao cabe na faixa do campo. Em qualquer recusa o campo anterior permanece.
    Status setPreset(Axis axis, Angle value);
    Status setPresetOffset(Axis axis, int16_t offsetDeci);
    Status setSensorDir(Axis axis, SensorDir dir);
    Status setLimitValue(LimitId id, Angle value);
    Status setLimitOp(LimitId id, LimitOp op);
    Status setPassword(uint16_t value);
    Status setCalFullScale(Axis axis, Angle value);

    // NAO EXISTEM setCalZeroCode() E setCalFullScaleCode(). Eram escritores de MEIO PAR: cada um
    // movia uma ponta da reta de calibracao sozinho, sob uma janela que nao olhava a outra ponta.
    // O sub-item de A14 ("meio par gravado e pior que nenhuma calibracao") proibe justamente
    // isso, e nenhum caminho de produto precisava deles - so os testes os chamavam. Quem escreve
    // calibracao escreve o par (setCalPair) ou o trio (setCalTriple), sempre por inteiro.

    // Commit do assistente de A14: zero e ganho gravados de uma vez so. Se o par for recusado
    // pelo gate unico, NENHUM dos dois e escrito.
    Status setCalPair(Axis axis, uint16_t zeroCode, uint16_t fullScaleCode);

    // O UNICO ponto de escrita que o produto usa (composition root: fim da Auto Calibracao e
    // Reset Geral). Os TRES campos que descrevem a reta de um eixo - codigo de zero, codigo de
    // fundo de escala e angulo de fundo de escala - sao validados por UMA chamada a
    // AnalogScaler::make() e gravados de uma vez ou nenhuma. Enquanto existirem duas escritas
    // separadas com dois Status, existe um caminho em que uma passa e a outra reprova e o eixo
    // fica com angulo novo sobre codigos velhos.
    Status setCalTriple(Axis axis, uint16_t zeroCode, uint16_t fullScaleCode, Angle fullScale);

    // O gate unico, exposto para quem precisa PERGUNTAR antes de escrever (e para o teste de
    // grade que prova que tudo que AnalogCalibration::commit() aceita este agregado aceita).
    static bool calPairValid(uint16_t zeroCode, uint16_t fullScaleCode);

    // Escreve o registro inteiro no buffer do chamador. Err::Param se cap nao couber.
    Status serializeParams(uint8_t* dst, uint16_t cap, uint16_t& outLen) const;
    Status serializeCal(uint8_t* dst, uint16_t cap, uint16_t& outLen) const;

    // Err::Param (blob curto ou ponteiro nulo), Err::Storage (magic de outro registro),
    // Err::Crc (corrompido), Err::Unsupported (versao desconhecida), Err::Range (campo
    // impossivel sob CRC bom). Em qualquer recusa nenhum campo e alterado. len maior que o
    // registro e aceito: IParameterStore::read devolve o slot inteiro.
    Status loadParams(const uint8_t* src, uint16_t len);
    Status loadCal(const uint8_t* src, uint16_t len);

private:
    // Os dois registros de A8. Grupos separados no tipo, nao so no blob: nenhuma carga
    // consegue escrever no grupo que nao e o seu.
    struct RelayGroup {
        int16_t presetDeci[kAxisCount];
        int16_t presetOffsetDeci[kAxisCount];
        int16_t limitDeci[kLimitCount];
        uint8_t limitOp[kLimitCount];
        uint8_t sensorDir[kAxisCount];
        uint16_t password;
    };

    struct CalGroup {
        int16_t fullScaleDeci[kAxisCount];
        uint16_t zeroCode[kAxisCount];
        uint16_t fullScaleCode[kAxisCount];
    };

    static constexpr uint8_t idx(Axis axis) { return static_cast<uint8_t>(axis); }
    static constexpr uint8_t idx(LimitId id) { return static_cast<uint8_t>(id); }

    static bool angleValid(Angle value);
    static bool presetOffsetValid(int16_t offsetDeci);
    static bool limitOpValid(uint8_t raw);
    static bool sensorDirValid(uint8_t raw);
    static bool passwordValid(uint16_t value);
    static bool fullScaleValid(int16_t deci);
    static bool calTripleValid(uint16_t zeroCode, uint16_t fullScaleCode, int16_t fullScaleDeci);

    RelayGroup rel_;
    CalGroup cal_;
};

}  // namespace domain
