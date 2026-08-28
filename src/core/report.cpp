// Relatorio: bloco legivel + linha CSV entre #RESULT_BEGIN/#RESULT_END. Sem dependencia de Arduino.
#include "core/report.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {

void copyField(char* dst, size_t cap, const char* src) {
    if (cap == 0) {
        return;
    }
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    while (i + 1 < cap && src[i] != '\0') {
        const char c = src[i];
        dst[i] = (c == '\n' || c == '\r') ? ' ' : c;
        ++i;
    }
    dst[i] = '\0';
}

void copyCsvField(char* dst, size_t cap, const char* src) {
    if (cap == 0) {
        return;
    }
    if (src == nullptr || src[0] == '\0') {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    while (i + 1 < cap && src[i] != '\0') {
        const char c = src[i];
        dst[i] = (c == ',' || c == '\n' || c == '\r' || c == ';') ? ' ' : c;
        ++i;
    }
    dst[i] = '\0';
}

uint16_t appendf(char* out, uint16_t cap, uint16_t used, const char* fmt, ...) {
    if (out == nullptr || cap == 0 || used >= cap) {
        return used;
    }
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(out + used, static_cast<size_t>(cap - used), fmt, ap);
    va_end(ap);
    if (n < 0) {
        return used;
    }
    const uint16_t grown = static_cast<uint16_t>(used + n);
    return (grown >= cap) ? static_cast<uint16_t>(cap - 1) : grown;
}

constexpr uint32_t kReportMagic = 0x44505231u;
constexpr uint16_t kReportVersion = 1;

struct ReportBlob {
    uint32_t magic;
    uint16_t version;
    uint8_t count;
    uint8_t reserved;
    char serial[kReportSerialLen];
    char date[kReportDateLen];
    ReportItem items[kReportMaxItems];
};

}  // namespace

Report::Report() : items_(), count_(0), serial_(), date_(), fw_(""), rev_("") {
    clear();
}

void Report::clear() {
    memset(items_, 0, sizeof(items_));
    count_ = 0;
    copyField(serial_, sizeof(serial_), "NO-SERIAL");
    copyField(date_, sizeof(date_), "NO-DATE");
}

void Report::setSerial(const char* serial) {
    copyCsvField(serial_, sizeof(serial_), serial);
    if (serial_[0] == '\0') {
        copyField(serial_, sizeof(serial_), "NO-SERIAL");
    }
}

void Report::setDate(const char* date) {
    copyCsvField(date_, sizeof(date_), date);
    if (date_[0] == '\0') {
        copyField(date_, sizeof(date_), "NO-DATE");
    }
}

void Report::setMeta(const char* fwVersion, const char* boardRev) {
    fw_ = (fwVersion != nullptr) ? fwVersion : "";
    rev_ = (boardRev != nullptr) ? boardRev : "";
}

Status Report::record(const char* id, const char* name, Verdict verdict, const char* note, uint32_t uptimeMs) {
    if (id == nullptr) {
        return Status(Err::Param);
    }
    ReportItem* slot = nullptr;
    for (uint8_t i = 0; i < count_; ++i) {
        if (strncmp(items_[i].id, id, kReportIdLen) == 0) {
            slot = &items_[i];
            break;
        }
    }
    if (slot == nullptr) {
        if (count_ >= kReportMaxItems) {
            return Status(Err::Range);
        }
        slot = &items_[count_++];
    }
    copyCsvField(slot->id, sizeof(slot->id), id);
    copyCsvField(slot->name, sizeof(slot->name), name);
    copyCsvField(slot->note, sizeof(slot->note), note);
    slot->verdict = verdict;
    slot->uptimeMs = uptimeMs;
    return kOk;
}

const ReportItem& Report::at(uint8_t index) const {
    static const ReportItem kEmpty = {};
    if (index >= count_) {
        return kEmpty;
    }
    return items_[index];
}

bool Report::complete() const {
    if (count_ == 0) {
        return false;
    }
    for (uint8_t i = 0; i < count_; ++i) {
        if (items_[i].verdict != Verdict::Pass && items_[i].verdict != Verdict::Fail) {
            return false;
        }
    }
    return true;
}

const char* Report::overallText() const {
    if (anyFail()) {
        return "FAIL";
    }
    return complete() ? "PASS" : "INCOMPLETO";
}

bool Report::anyFail() const {
    for (uint8_t i = 0; i < count_; ++i) {
        if (items_[i].verdict == Verdict::Fail || items_[i].verdict == Verdict::Abort) {
            return true;
        }
    }
    return false;
}

uint16_t Report::formatHuman(char* out, uint16_t cap) const {
    if (out == nullptr || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    uint16_t n = 0;
    n = appendf(out, cap, n, "==== RELATORIO DE TESTE DE FABRICA ====\r\n");
    n = appendf(out, cap, n, "Placa    : DE-PURI-DI261924 REV %s\r\n", rev_);
    n = appendf(out, cap, n, "Firmware : %s\r\n", fw_);
    n = appendf(out, cap, n, "Serie    : %s\r\n", serial_);
    n = appendf(out, cap, n, "Data     : %s\r\n", date_);
    n = appendf(out, cap, n, "%-6s %-28s %-7s %-9s %s\r\n", "ID", "TESTE", "RESULT", "UPTIME_MS", "NOTA");
    for (uint8_t i = 0; i < count_; ++i) {
        const ReportItem& it = items_[i];
        n = appendf(out, cap, n, "%-6s %-28s %-7s %-9lu %s\r\n", it.id, it.name, verdictName(it.verdict),
                    static_cast<unsigned long>(it.uptimeMs), it.note);
    }
    n = appendf(out, cap, n, "VEREDITO GERAL: %s\r\n", overallText());
    return n;
}

uint16_t Report::formatCsv(char* out, uint16_t cap) const {
    if (out == nullptr || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    uint16_t n = 0;
    n = appendf(out, cap, n, "%s\r\n", kResultBegin);
    n = appendf(out, cap, n, "%s,%s,%s,%s,%s", serial_, fw_, rev_, date_, overallText());
    for (uint8_t i = 0; i < count_; ++i) {
        n = appendf(out, cap, n, ",%s=%c", items_[i].id, verdictChar(items_[i].verdict));
    }
    n = appendf(out, cap, n, "\r\n%s\r\n", kResultEnd);
    return n;
}

Status Report::save(IKeyValueStore& kv, const char* key) const {
    ReportBlob blob;
    memset(&blob, 0, sizeof(blob));
    blob.magic = kReportMagic;
    blob.version = kReportVersion;
    blob.count = count_;
    memcpy(blob.serial, serial_, sizeof(blob.serial));
    memcpy(blob.date, date_, sizeof(blob.date));
    memcpy(blob.items, items_, sizeof(blob.items));
    return kv.putBlob(key, &blob, sizeof(blob));
}

Status Report::load(IKeyValueStore& kv, const char* key) {
    ReportBlob blob;
    memset(&blob, 0, sizeof(blob));
    size_t got = 0;
    const Status st = kv.getBlob(key, &blob, sizeof(blob), got);
    if (st.failed()) {
        return st;
    }
    if (got != sizeof(blob) || blob.magic != kReportMagic || blob.version != kReportVersion) {
        return Status(Err::Storage);
    }
    if (blob.count > kReportMaxItems) {
        return Status(Err::Storage);
    }
    memcpy(items_, blob.items, sizeof(items_));
    count_ = blob.count;
    memcpy(serial_, blob.serial, sizeof(serial_));
    memcpy(date_, blob.date, sizeof(date_));
    serial_[sizeof(serial_) - 1] = '\0';
    date_[sizeof(date_) - 1] = '\0';
    return kOk;
}
