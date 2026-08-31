// Implementacao do agregado Parameters: defaults da Tabela 2, validacao de seletor e de faixa
// por campo, e os dois blobs separados exigidos pela decisao A8 (opcao C, aprovada em 2026-08-31).
//
// Layout dos registros, deslocamentos fixos, tudo inteiro e sempre em little-endian explicito
// para que o blob gravado independa do compilador e do alinhamento (decisao 2, item 2). Os dois
// blobs sao a CARGA UTIL que o ParamStoreLogic grava no slot; a escolha do slot e o contador de
// geracao do banco duplo nao moram aqui (ver o cabecalho de parameters.h):
//
//   Registro de parametros - 32 bytes, gravado em ParamSlot::BankA / ParamSlot::BankB
//     off  0  uint32 magic = 0x44505231      off 14  int16  limitDeci[4]
//     off  4  uint16 version = 1             off 22  uint8  limitOp[4]
//     off  6  int16  presetDeci[2]           off 26  uint8  sensorDir[2]
//     off 10  int16  presetOffsetDeci[2]     off 28  uint16 password
//                                            off 30  uint16 crc sobre 0..29
//
//   Registro de calibracao - 20 bytes, gravado em ParamSlot::FactoryCal
//     off  0  uint32 magic = 0x44435231      off 10  uint16 zeroCode[2]
//     off  4  uint16 version = 1             off 14  uint16 fullScaleCode[2]
//     off  6  int16  fullScaleDeci[2]        off 18  uint16 crc sobre 0..17
//
// O CRC e o crc16Modbus de proto/crc16.h (poly 0xA001 refletido, seed 0xFFFF), o mesmo do quadro
// do enlace: uma unica implementacao no produto inteiro, e o proprio cabecalho da funcao ja a
// declara "usado no quadro do jig e na NVS". E codigo puro, sem nenhuma dependencia de hardware:
// a regra hexagonal deste projeto veta hardware no dominio, nao codigo compartilhado.
#include "domain/parameters.h"

#include "proto/crc16.h"

namespace domain {
namespace {

constexpr uint16_t kOffMagic = 0;
constexpr uint16_t kOffVersion = 4;

constexpr uint16_t kOffPreset = 6;
constexpr uint16_t kOffPresetOffset = 10;
constexpr uint16_t kOffLimitValue = 14;
constexpr uint16_t kOffLimitOp = 22;
constexpr uint16_t kOffSensorDir = 26;
constexpr uint16_t kOffPassword = 28;

constexpr uint16_t kOffCalFullScale = 6;
constexpr uint16_t kOffCalZeroCode = 10;
constexpr uint16_t kOffCalFullScaleCode = 14;

void put16(uint8_t* dst, uint16_t value) {
    dst[0] = static_cast<uint8_t>(value & 0xFFu);
    dst[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

void put32(uint8_t* dst, uint32_t value) {
    put16(dst, static_cast<uint16_t>(value & 0xFFFFu));
    put16(dst + 2, static_cast<uint16_t>((value >> 16) & 0xFFFFu));
}

uint16_t get16(const uint8_t* src) {
    return static_cast<uint16_t>(static_cast<uint16_t>(src[0]) |
                                 static_cast<uint16_t>(static_cast<uint16_t>(src[1]) << 8));
}

uint32_t get32(const uint8_t* src) {
    return static_cast<uint32_t>(get16(src)) | (static_cast<uint32_t>(get16(src + 2)) << 16);
}

void sign(uint8_t* blob, uint16_t size) {
    put16(blob + size - 2, crc16Modbus(blob, static_cast<size_t>(size - 2)));
}

// Comum aos dois registros: tamanho, identidade, integridade e versao, nesta ordem.
Status checkEnvelope(const uint8_t* src, uint16_t len, uint16_t size, uint32_t magic,
                     uint16_t version) {
    if (src == nullptr || len < size) {
        return Err::Param;
    }
    if (get32(src + kOffMagic) != magic) {
        return Err::Storage;
    }
    if (crc16Modbus(src, static_cast<size_t>(size - 2)) != get16(src + size - 2)) {
        return Err::Crc;
    }
    if (get16(src + kOffVersion) != version) {
        return Err::Unsupported;
    }
    return kOk;
}

}  // namespace

Parameters::Parameters() : rel_(), cal_() {
    for (uint8_t axis = 0; axis < kAxisCount; ++axis) {
        rel_.presetDeci[axis] = kDefaultPresetDeci;
        rel_.presetOffsetDeci[axis] = 0;
        rel_.sensorDir[axis] = static_cast<uint8_t>(SensorDir::Clockwise);
        cal_.fullScaleDeci[axis] = kDefaultCalFullScaleDeci;
        cal_.zeroCode[axis] = kNominalCalZeroCode;
        cal_.fullScaleCode[axis] = kNominalCalFullScaleCode;
    }
    // Tabela 2: Limites 1 (X1) e 3 (Y1) em + (modulo) com +005,0 graus; Limites 2 (X2) e
    // 4 (Y2) em Off com +000,0 graus.
    rel_.limitDeci[idx(LimitId::X1)] = kDefaultLimitDeci;
    rel_.limitDeci[idx(LimitId::X2)] = kDefaultLimitOffDeci;
    rel_.limitDeci[idx(LimitId::Y1)] = kDefaultLimitDeci;
    rel_.limitDeci[idx(LimitId::Y2)] = kDefaultLimitOffDeci;
    rel_.limitOp[idx(LimitId::X1)] = static_cast<uint8_t>(LimitOp::Absolute);
    rel_.limitOp[idx(LimitId::X2)] = static_cast<uint8_t>(LimitOp::Off);
    rel_.limitOp[idx(LimitId::Y1)] = static_cast<uint8_t>(LimitOp::Absolute);
    rel_.limitOp[idx(LimitId::Y2)] = static_cast<uint8_t>(LimitOp::Off);
    rel_.password = kDefaultPassword;
}

Parameters Parameters::factoryDefaults() { return Parameters(); }

bool Parameters::angleValid(Angle value) { return value.valid(); }

bool Parameters::presetOffsetValid(int16_t offsetDeci) {
    return offsetDeci >= kPresetOffsetMinDeci && offsetDeci <= kPresetOffsetMaxDeci;
}

bool Parameters::limitOpValid(uint8_t raw) {
    return raw <= static_cast<uint8_t>(LimitOp::Absolute);
}

bool Parameters::sensorDirValid(uint8_t raw) {
    return raw <= static_cast<uint8_t>(SensorDir::CounterClockwise);
}

bool Parameters::passwordValid(uint16_t value) { return value <= kPasswordMax; }

bool Parameters::fullScaleValid(int16_t deci) {
    return deci >= kCalFullScaleMinDeci && deci <= kCalFullScaleMaxDeci;
}

// GATE UNICO DE A14, UMA UNICA LINHA DE CODIGO EM TODO O AGREGADO. Nenhuma faixa e repetida
// aqui: quem sabe o que a cadeia analogica consegue emitir e o AnalogScaler.
bool Parameters::calTripleValid(uint16_t zeroCode, uint16_t fullScaleCode, int16_t fullScaleDeci) {
    AnalogScaler sonda;
    return AnalogScaler::make(zeroCode, fullScaleCode, fullScaleDeci, sonda);
}

bool Parameters::calPairValid(uint16_t zeroCode, uint16_t fullScaleCode) {
    // O criterio do angulo e SEPARAVEL do criterio dos codigos dentro de make() - um olha
    // fullScaleAngleDeci, o outro olha zero e vao - entao sondar com o angulo de fabrica
    // responde exatamente "este par de codigos e legitimo?", sem depender de qual angulo esta
    // programado no eixo e sem criar ordem obrigatoria entre as duas escritas.
    return calTripleValid(zeroCode, fullScaleCode, kDefaultCalFullScaleDeci);
}

Status Parameters::setPreset(Axis axis, Angle value) {
    if (!axisValid(axis)) {
        return Err::Param;
    }
    if (!angleValid(value)) {
        return Err::Range;
    }
    rel_.presetDeci[idx(axis)] = value.deciDegrees();
    return kOk;
}

Status Parameters::setPresetOffset(Axis axis, int16_t offsetDeci) {
    if (!axisValid(axis)) {
        return Err::Param;
    }
    if (!presetOffsetValid(offsetDeci)) {
        return Err::Range;
    }
    rel_.presetOffsetDeci[idx(axis)] = offsetDeci;
    return kOk;
}

Status Parameters::setSensorDir(Axis axis, SensorDir dir) {
    if (!axisValid(axis)) {
        return Err::Param;
    }
    if (!sensorDirValid(static_cast<uint8_t>(dir))) {
        return Err::Range;
    }
    rel_.sensorDir[idx(axis)] = static_cast<uint8_t>(dir);
    return kOk;
}

Status Parameters::setLimitValue(LimitId id, Angle value) {
    if (!limitIdValid(id)) {
        return Err::Param;
    }
    if (!angleValid(value)) {
        return Err::Range;
    }
    rel_.limitDeci[idx(id)] = value.deciDegrees();
    return kOk;
}

Status Parameters::setLimitOp(LimitId id, LimitOp op) {
    if (!limitIdValid(id)) {
        return Err::Param;
    }
    if (!limitOpValid(static_cast<uint8_t>(op))) {
        return Err::Range;
    }
    rel_.limitOp[idx(id)] = static_cast<uint8_t>(op);
    return kOk;
}

Status Parameters::setPassword(uint16_t value) {
    if (!passwordValid(value)) {
        return Err::Range;
    }
    rel_.password = value;
    return kOk;
}

Status Parameters::setCalFullScale(Axis axis, Angle value) {
    if (!axisValid(axis)) {
        return Err::Param;
    }
    if (!angleValid(value) || !fullScaleValid(value.deciDegrees())) {
        return Err::Range;
    }
    cal_.fullScaleDeci[idx(axis)] = value.deciDegrees();
    return kOk;
}

Status Parameters::setCalPair(Axis axis, uint16_t zeroCode, uint16_t fullScaleCode) {
    if (!axisValid(axis)) {
        return Err::Param;
    }
    if (!calPairValid(zeroCode, fullScaleCode)) {
        return Err::Range;
    }
    cal_.zeroCode[idx(axis)] = zeroCode;
    cal_.fullScaleCode[idx(axis)] = fullScaleCode;
    return kOk;
}

Status Parameters::setCalTriple(Axis axis, uint16_t zeroCode, uint16_t fullScaleCode,
                                Angle fullScale) {
    if (!axisValid(axis)) {
        return Err::Param;
    }
    if (!angleValid(fullScale)) {
        return Err::Range;
    }
    // UMA chamada ao gate, cobrindo os tres campos. Nenhuma escrita antes dela.
    if (!calTripleValid(zeroCode, fullScaleCode, fullScale.deciDegrees())) {
        return Err::Range;
    }
    cal_.zeroCode[idx(axis)] = zeroCode;
    cal_.fullScaleCode[idx(axis)] = fullScaleCode;
    cal_.fullScaleDeci[idx(axis)] = fullScale.deciDegrees();
    return kOk;
}

Status Parameters::serializeParams(uint8_t* dst, uint16_t cap, uint16_t& outLen) const {
    outLen = 0;
    if (dst == nullptr || cap < kParamBlobSize) {
        return Err::Param;
    }
    put32(dst + kOffMagic, kParamMagic);
    put16(dst + kOffVersion, kParamVersion);
    for (uint8_t axis = 0; axis < kAxisCount; ++axis) {
        put16(dst + kOffPreset + 2 * axis, static_cast<uint16_t>(rel_.presetDeci[axis]));
        put16(dst + kOffPresetOffset + 2 * axis,
              static_cast<uint16_t>(rel_.presetOffsetDeci[axis]));
        dst[kOffSensorDir + axis] = rel_.sensorDir[axis];
    }
    for (uint8_t limit = 0; limit < kLimitCount; ++limit) {
        put16(dst + kOffLimitValue + 2 * limit, static_cast<uint16_t>(rel_.limitDeci[limit]));
        dst[kOffLimitOp + limit] = rel_.limitOp[limit];
    }
    put16(dst + kOffPassword, rel_.password);
    sign(dst, kParamBlobSize);
    outLen = kParamBlobSize;
    return kOk;
}

Status Parameters::serializeCal(uint8_t* dst, uint16_t cap, uint16_t& outLen) const {
    outLen = 0;
    if (dst == nullptr || cap < kCalBlobSize) {
        return Err::Param;
    }
    put32(dst + kOffMagic, kCalMagic);
    put16(dst + kOffVersion, kCalVersion);
    for (uint8_t axis = 0; axis < kAxisCount; ++axis) {
        put16(dst + kOffCalFullScale + 2 * axis, static_cast<uint16_t>(cal_.fullScaleDeci[axis]));
        put16(dst + kOffCalZeroCode + 2 * axis, cal_.zeroCode[axis]);
        put16(dst + kOffCalFullScaleCode + 2 * axis, cal_.fullScaleCode[axis]);
    }
    sign(dst, kCalBlobSize);
    outLen = kCalBlobSize;
    return kOk;
}

Status Parameters::loadParams(const uint8_t* src, uint16_t len) {
    const Status envelope = checkEnvelope(src, len, kParamBlobSize, kParamMagic, kParamVersion);
    if (envelope.failed()) {
        return envelope;
    }

    // Grupo local: so vai para rel_ depois de TODOS os campos aprovados. Escrever campo a campo
    // no agregado que comanda rele e devolver Err::Range depois seria carga parcial (A8).
    RelayGroup lido;
    for (uint8_t axis = 0; axis < kAxisCount; ++axis) {
        lido.presetDeci[axis] = static_cast<int16_t>(get16(src + kOffPreset + 2 * axis));
        lido.presetOffsetDeci[axis] =
            static_cast<int16_t>(get16(src + kOffPresetOffset + 2 * axis));
        lido.sensorDir[axis] = src[kOffSensorDir + axis];
        if (!angleValid(Angle::fromDeciDegrees(lido.presetDeci[axis])) ||
            !presetOffsetValid(lido.presetOffsetDeci[axis]) ||
            !sensorDirValid(lido.sensorDir[axis])) {
            return Err::Range;
        }
    }
    for (uint8_t limit = 0; limit < kLimitCount; ++limit) {
        lido.limitDeci[limit] = static_cast<int16_t>(get16(src + kOffLimitValue + 2 * limit));
        lido.limitOp[limit] = src[kOffLimitOp + limit];
        if (!angleValid(Angle::fromDeciDegrees(lido.limitDeci[limit])) ||
            !limitOpValid(lido.limitOp[limit])) {
            return Err::Range;
        }
    }
    lido.password = get16(src + kOffPassword);
    if (!passwordValid(lido.password)) {
        return Err::Range;
    }

    rel_ = lido;
    return kOk;
}

Status Parameters::loadCal(const uint8_t* src, uint16_t len) {
    const Status envelope = checkEnvelope(src, len, kCalBlobSize, kCalMagic, kCalVersion);
    if (envelope.failed()) {
        return envelope;
    }

    CalGroup lido;
    for (uint8_t axis = 0; axis < kAxisCount; ++axis) {
        lido.fullScaleDeci[axis] = static_cast<int16_t>(get16(src + kOffCalFullScale + 2 * axis));
        lido.zeroCode[axis] = get16(src + kOffCalZeroCode + 2 * axis);
        lido.fullScaleCode[axis] = get16(src + kOffCalFullScaleCode + 2 * axis);
        // MESMO gate da escrita, aplicado aos tres campos de uma vez: um registro que vem da NVS
        // com CRC bom nao pode entrar por uma porta mais larga do que a do assistente (A8: CRC
        // aprovado nao autoriza valor fora de faixa).
        if (!calTripleValid(lido.zeroCode[axis], lido.fullScaleCode[axis],
                            lido.fullScaleDeci[axis])) {
            return Err::Range;
        }
    }

    cal_ = lido;
    return kOk;
}

}  // namespace domain
