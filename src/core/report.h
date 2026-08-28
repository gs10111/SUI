// Relatorio consolidado: formato legivel + linha CSV delimitada, persistido via IKeyValueStore.
#pragma once

#include <stdint.h>

#include "kv_store.h"
#include "status.h"
#include "verdict.h"

constexpr uint8_t kReportMaxItems = 12;
constexpr uint8_t kReportIdLen = 12;
constexpr uint8_t kReportNameLen = 32;
constexpr uint8_t kReportNoteLen = 48;
constexpr uint8_t kReportSerialLen = 24;
constexpr uint8_t kReportDateLen = 20;

constexpr const char* kResultBegin = "#RESULT_BEGIN";
constexpr const char* kResultEnd = "#RESULT_END";

struct ReportItem {
    char id[kReportIdLen];
    char name[kReportNameLen];
    char note[kReportNoteLen];
    Verdict verdict;
    uint32_t uptimeMs;
};

class Report {
public:
    Report();

    void clear();
    void setSerial(const char* serial);
    void setDate(const char* date);
    void setMeta(const char* fwVersion, const char* boardRev);
    const char* serial() const { return serial_; }
    const char* date() const { return date_; }

    Status record(const char* id, const char* name, Verdict verdict, const char* note, uint32_t uptimeMs);
    uint8_t count() const { return count_; }
    const ReportItem& at(uint8_t index) const;
    bool anyFail() const;
    bool complete() const;
    const char* overallText() const;

    uint16_t formatHuman(char* out, uint16_t cap) const;
    uint16_t formatCsv(char* out, uint16_t cap) const;

    Status save(IKeyValueStore& kv, const char* key = "report") const;
    Status load(IKeyValueStore& kv, const char* key = "report");

private:
    ReportItem items_[kReportMaxItems];
    uint8_t count_;
    char serial_[kReportSerialLen];
    char date_[kReportDateLen];
    const char* fw_;
    const char* rev_;
};
