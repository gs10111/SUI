// Implementacao da Auto Calibracao de um eixo. Contrato, decisoes e desvios de manual em
// domain/analog_calibration.h (manual 5.7 L166..L187, decisao A14).
//
// Registro serializado, 6 bytes, tudo inteiro e em little endian, sem preenchimento e sem
// depender de layout de struct: [0..1] angulo de fundo de escala em decimos, int16;
// [2..3] codigo de zero, uint16; [4..5] codigo de fundo de escala, uint16. A integridade do
// registro na memoria nao volatil e do adaptador de armazenamento; aqui a validacao e
// semantica e tem dois criterios: o gate de regime, AnalogScaler::make(), e a janela do campo
// de trim, trimFieldsOf().
//
// trimFieldsOf() e o unico lugar que converte um par em par de campos de 4 digitos, e serve
// aos dois usos: em restore() e criterio de aceitacao, em loadFields() e conversao. Nao existe
// mais grampo de campo: um par que a janela nao representa e RECUSADO na porta da NVS, nunca
// exibido com o valor mais proximo. A comparacao e feita em uma unica desigualdade por campo,
// sobre o valor convertido a uint32, porque assim o mesmo teste pega o valor negativo e o
// valor grande demais sem criar ramo que nenhuma entrada alcanca. Alcance real de cada um,
// contra o gate de make(): o campo de zero e violado nos dois lados (zero de 26214 a 27767 da
// campo negativo, zero de 37768 a 40371 da campo acima de 9999) e o campo de ganho so pelo
// lado negativo (vao de 20971 a 21213); vao acima de 31213 exigiria, pelo piso do espelho,
// zero maior ou igual a 36457 e portanto codigo de fundo de escala maior ou igual a 67671,
// que nao cabe em uint16 e por isso nunca chega aqui.
//
// commit() nao tem guarda de estouro de 16 bits, e isso e proposital. O codigo de fundo de
// escala do buffer vale zero + 26214 + (ganho - 5000) com zero em [27768, 37767] e ganho em
// [0, 9999], logo fica em [48982, 68980]: nunca e negativo, e quando passa de 65535 o valor
// truncado cai em [0, 3444], que contra um zero de no minimo 27768 da vao negativo e e sempre
// reprovado por AnalogScaler::make(). Uma guarda ali nao mudaria nenhum resultado, e ramo que
// nao muda resultado em modulo de seguranca e cobertura falsa, nao defesa em profundidade. O
// caminho de estouro esta coberto por teste, pela borda que ele de fato tem: Err::Range e par
// de fabrica preservado.
#include "domain/analog_calibration.h"

namespace domain {

bool AnalogCalibration::trimFieldsOf(const AnalogScaler& scaler, uint16_t& zeroField,
                                     uint16_t& gainField) {
    const int32_t zero = static_cast<int32_t>(scaler.zeroCode());
    const int32_t span = static_cast<int32_t>(scaler.fullScaleCode()) - zero;
    const int32_t z = zero - static_cast<int32_t>(AnalogScaler::kZeroCode) +
                      static_cast<int32_t>(kTrimNeutral);
    const int32_t g = span - AnalogScaler::kNominalSpan + static_cast<int32_t>(kTrimNeutral);
    const uint32_t limite = static_cast<uint32_t>(kTrimFieldMax);
    if (static_cast<uint32_t>(z) > limite || static_cast<uint32_t>(g) > limite) {
        return false;
    }
    zeroField = static_cast<uint16_t>(z);
    gainField = static_cast<uint16_t>(g);
    return true;
}

AnalogCalibration::AnalogCalibration()
    : scaler_(), step_(Step::Idle), zeroField_(kTrimNeutral), gainField_(kTrimNeutral),
      fullScaleDeci_(AnalogScaler::kFactoryFullScaleDeci) {
    loadFields();
}

void AnalogCalibration::loadFields() {
    (void)trimFieldsOf(scaler_, zeroField_, gainField_);
    fullScaleDeci_ = scaler_.fullScaleAngleDeci();
}

int32_t AnalogCalibration::bufferZeroCode() const {
    return static_cast<int32_t>(AnalogScaler::kZeroCode) + static_cast<int32_t>(zeroField_) -
           static_cast<int32_t>(kTrimNeutral);
}

int32_t AnalogCalibration::bufferFullScaleCode() const {
    return bufferZeroCode() + AnalogScaler::kNominalSpan + static_cast<int32_t>(gainField_) -
           static_cast<int32_t>(kTrimNeutral);
}

Status AnalogCalibration::begin() {
    if (step_ != Step::Idle) {
        return Status(Err::Busy);
    }
    loadFields();
    step_ = Step::Zero;
    return kOk;
}

Status AnalogCalibration::setZeroField(uint16_t field) {
    if (step_ != Step::Zero) {
        return Status(Err::NotCalibrated);
    }
    if (field > kTrimFieldMax) {
        return Status(Err::Range);
    }
    zeroField_ = field;
    return kOk;
}

Status AnalogCalibration::confirmZero() {
    if (step_ != Step::Zero) {
        return Status(Err::NotCalibrated);
    }
    step_ = Step::FullScaleAngle;
    return kOk;
}

Status AnalogCalibration::setFullScaleAngle(int16_t deci) {
    if (step_ != Step::FullScaleAngle) {
        return Status(Err::NotCalibrated);
    }
    if (deci < AnalogScaler::kFullScaleMinDeci || deci > AnalogScaler::kFullScaleMaxDeci) {
        return Status(Err::Range);
    }
    fullScaleDeci_ = deci;
    return kOk;
}

Status AnalogCalibration::confirmFullScaleAngle() {
    if (step_ != Step::FullScaleAngle) {
        return Status(Err::NotCalibrated);
    }
    step_ = Step::Gain;
    return kOk;
}

Status AnalogCalibration::setGainField(uint16_t field) {
    if (step_ != Step::Gain) {
        return Status(Err::NotCalibrated);
    }
    if (field > kTrimFieldMax) {
        return Status(Err::Range);
    }
    gainField_ = field;
    return kOk;
}

Status AnalogCalibration::commit() {
    if (step_ != Step::Gain) {
        return Status(Err::NotCalibrated);
    }
    const int32_t zero = bufferZeroCode();
    const int32_t full = bufferFullScaleCode();
    AnalogScaler candidate;
    if (!AnalogScaler::make(static_cast<uint16_t>(zero), static_cast<uint16_t>(full),
                            fullScaleDeci_, candidate)) {
        return Status(Err::Range);
    }
    scaler_ = candidate;
    step_ = Step::Idle;
    return kOk;
}

void AnalogCalibration::abort() {
    loadFields();
    step_ = Step::Idle;
}

bool AnalogCalibration::serialize(uint8_t* out, uint8_t cap) const {
    if (out == nullptr || cap < kRecordBytes) {
        return false;
    }
    const uint16_t angle = static_cast<uint16_t>(scaler_.fullScaleAngleDeci());
    out[0] = static_cast<uint8_t>(angle & 0xFFu);
    out[1] = static_cast<uint8_t>((angle >> 8) & 0xFFu);
    out[2] = static_cast<uint8_t>(scaler_.zeroCode() & 0xFFu);
    out[3] = static_cast<uint8_t>((scaler_.zeroCode() >> 8) & 0xFFu);
    out[4] = static_cast<uint8_t>(scaler_.fullScaleCode() & 0xFFu);
    out[5] = static_cast<uint8_t>((scaler_.fullScaleCode() >> 8) & 0xFFu);
    return true;
}

bool AnalogCalibration::restore(const uint8_t* in, uint8_t len) {
    if (in == nullptr || len < kRecordBytes) {
        return false;
    }
    const int16_t angle =
        static_cast<int16_t>(static_cast<uint16_t>(in[0]) | (static_cast<uint16_t>(in[1]) << 8));
    const uint16_t zero =
        static_cast<uint16_t>(static_cast<uint16_t>(in[2]) | (static_cast<uint16_t>(in[3]) << 8));
    const uint16_t full =
        static_cast<uint16_t>(static_cast<uint16_t>(in[4]) | (static_cast<uint16_t>(in[5]) << 8));
    AnalogScaler candidate;
    if (!AnalogScaler::make(zero, full, angle, candidate)) {
        return false;
    }
    uint16_t zeroField = 0;
    uint16_t gainField = 0;
    if (!trimFieldsOf(candidate, zeroField, gainField)) {
        return false;
    }
    scaler_ = candidate;
    return true;
}

}  // namespace domain
