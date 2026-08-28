// Calibracao de 2 pontos: valor = a*code + b, por eixo e por modo. CRC16-MODBUS no registro NVS.
#include "drivers/calibration.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "proto/crc16.h"

static_assert(sizeof(CalRecord) == 48, "layout do CalRecord mudou: versione o registro");

namespace calmath {

namespace {
constexpr float kCodeSlackLsb = 8.0f;
}  // namespace

bool solve(Point p1, Point p2, Coef& out) {
    out.a = 0.0f;
    out.b = 0.0f;
    out.valid = false;
    if (p1.code == p2.code) {
        return false;
    }
    if (!isfinite(p1.value) || !isfinite(p2.value)) {
        return false;
    }
    const float dc = static_cast<float>(p2.code) - static_cast<float>(p1.code);
    const float a = (p2.value - p1.value) / dc;
    if (!isfinite(a) || a == 0.0f) {
        return false;
    }
    const float b = p1.value - a * static_cast<float>(p1.code);
    if (!isfinite(b)) {
        return false;
    }
    out.a = a;
    out.b = b;
    out.valid = true;
    return true;
}

float valueFromCode(const Coef& c, uint16_t code) {
    return c.a * static_cast<float>(code) + c.b;
}

bool codeFromValue(const Coef& c, float value, uint16_t maxCode, uint16_t& out) {
    out = 0;
    if (!c.valid || c.a == 0.0f || !isfinite(value)) {
        return false;
    }
    const float raw = (value - c.b) / c.a;
    if (!isfinite(raw)) {
        return false;
    }
    const float rounded = floorf(raw + 0.5f);
    if (rounded < -kCodeSlackLsb || rounded > static_cast<float>(maxCode) + kCodeSlackLsb) {
        return false;
    }
    float clamped = rounded;
    if (clamped < 0.0f) {
        clamped = 0.0f;
    }
    if (clamped > static_cast<float>(maxCode)) {
        clamped = static_cast<float>(maxCode);
    }
    out = static_cast<uint16_t>(clamped);
    return true;
}

}  // namespace calmath

uint16_t calRecordCrc(const CalRecord& rec) {
    const uint8_t* base = reinterpret_cast<const uint8_t*>(&rec);
    const size_t offset = offsetof(CalRecord, a);
    return crc16Modbus(base + offset, sizeof(CalRecord) - offset);
}

CalibrationStore::CalibrationStore(IKeyValueStore& kv, const char* key) : kv_(kv), key_(key), rec_() {
    memset(&rec_, 0, sizeof(rec_));
    rec_.magic = kCalMagic;
    rec_.version = kCalVersion;
    rec_.crc = calRecordCrc(rec_);
}

bool CalibrationStore::indexOk(uint8_t axis, AoMode mode) {
    return axis < board::kAxisCount && static_cast<uint8_t>(mode) < kCalModeCount;
}

Status CalibrationStore::load() {
    CalRecord tmp;
    memset(&tmp, 0, sizeof(tmp));
    size_t got = 0;
    const Status st = kv_.getBlob(key_, &tmp, sizeof(tmp), got);
    if (st.failed()) {
        return st;
    }
    if (got != sizeof(tmp)) {
        return Status(Err::Storage);
    }
    if (tmp.magic != kCalMagic) {
        return Status(Err::Storage);
    }
    if (tmp.version != kCalVersion) {
        return Status(Err::Unsupported);
    }
    if (calRecordCrc(tmp) != tmp.crc) {
        return Status(Err::Crc);
    }
    rec_ = tmp;
    return kOk;
}

Status CalibrationStore::save() {
    rec_.magic = kCalMagic;
    rec_.version = kCalVersion;
    memset(rec_.reserved, 0, sizeof(rec_.reserved));
    rec_.crc = calRecordCrc(rec_);
    return kv_.putBlob(key_, &rec_, sizeof(rec_));
}

Status CalibrationStore::erase() {
    const Status st = kv_.remove(key_);
    if (st.failed() && st.err != Err::Storage) {
        return st;
    }
    memset(&rec_, 0, sizeof(rec_));
    rec_.magic = kCalMagic;
    rec_.version = kCalVersion;
    rec_.crc = calRecordCrc(rec_);
    return kOk;
}

bool CalibrationStore::has(uint8_t axis, AoMode mode) const {
    if (!indexOk(axis, mode)) {
        return false;
    }
    return rec_.valid[axis][static_cast<uint8_t>(mode)] != 0;
}

Status CalibrationStore::setFromPoints(uint8_t axis, AoMode mode, calmath::Point p1, calmath::Point p2) {
    if (!indexOk(axis, mode)) {
        return Status(Err::Param);
    }
    calmath::Coef c;
    if (!calmath::solve(p1, p2, c)) {
        return Status(Err::Range);
    }
    const uint8_t m = static_cast<uint8_t>(mode);
    rec_.a[axis][m] = c.a;
    rec_.b[axis][m] = c.b;
    rec_.valid[axis][m] = 1;
    return kOk;
}

Status CalibrationStore::coef(uint8_t axis, AoMode mode, calmath::Coef& out) const {
    out.a = 0.0f;
    out.b = 0.0f;
    out.valid = false;
    if (!indexOk(axis, mode)) {
        return Status(Err::Param);
    }
    const uint8_t m = static_cast<uint8_t>(mode);
    if (rec_.valid[axis][m] == 0) {
        return Status(Err::NotCalibrated);
    }
    out.a = rec_.a[axis][m];
    out.b = rec_.b[axis][m];
    out.valid = true;
    return kOk;
}

Status CalibrationStore::codeFor(uint8_t axis, AoMode mode, float value, uint16_t maxCode, uint16_t& code) const {
    calmath::Coef c;
    const Status st = coef(axis, mode, c);
    if (st.failed()) {
        return st;
    }
    if (!calmath::codeFromValue(c, value, maxCode, code)) {
        return Status(Err::Range);
    }
    return kOk;
}

Status CalibrationStore::valueFor(uint8_t axis, AoMode mode, uint16_t code, float& value) const {
    calmath::Coef c;
    const Status st = coef(axis, mode, c);
    if (st.failed()) {
        return st;
    }
    value = calmath::valueFromCode(c, code);
    return kOk;
}
