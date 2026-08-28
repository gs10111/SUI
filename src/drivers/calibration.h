// Calibracao de 2 pontos por eixo e por modo. Matematica pura + persistencia via IKeyValueStore.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "board_pins.h"
#include "iface/ianalog_output.h"
#include "kv_store.h"
#include "status.h"

namespace calmath {

struct Point {
    uint16_t code;
    float value;
};

struct Coef {
    float a;
    float b;
    bool valid;
};

bool solve(Point p1, Point p2, Coef& out);
float valueFromCode(const Coef& c, uint16_t code);
bool codeFromValue(const Coef& c, float value, uint16_t maxCode, uint16_t& out);

}  // namespace calmath

constexpr uint32_t kCalMagic = 0x44504331u;
constexpr uint16_t kCalVersion = 1;
constexpr uint8_t kCalModeCount = 2;

struct CalRecord {
    uint32_t magic;
    uint16_t version;
    uint16_t crc;
    float a[board::kAxisCount][kCalModeCount];
    float b[board::kAxisCount][kCalModeCount];
    uint8_t valid[board::kAxisCount][kCalModeCount];
    uint8_t reserved[4];
};

uint16_t calRecordCrc(const CalRecord& rec);

class CalibrationStore {
public:
    explicit CalibrationStore(IKeyValueStore& kv, const char* key = "cal_v1");

    Status load();
    Status save();
    Status erase();

    bool has(uint8_t axis, AoMode mode) const;
    Status setFromPoints(uint8_t axis, AoMode mode, calmath::Point p1, calmath::Point p2);
    Status coef(uint8_t axis, AoMode mode, calmath::Coef& out) const;
    Status codeFor(uint8_t axis, AoMode mode, float value, uint16_t maxCode, uint16_t& code) const;
    Status valueFor(uint8_t axis, AoMode mode, uint16_t code, float& value) const;
    const CalRecord& record() const { return rec_; }

private:
    static bool indexOk(uint8_t axis, AoMode mode);
    IKeyValueStore& kv_;
    const char* key_;
    CalRecord rec_;
};
