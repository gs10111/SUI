// NVS via Preferences (folha 1/2, U1): toda falha da biblioteca vira Err::Storage, sem impressao.
// Datasheet aplicavel: ESP32-WROOM-32D (particao nvs da flash interna).
#include "platform/nvs_store.h"

#include <string.h>

NvsStore::NvsStore() : prefs_(), ready_(false) {}

NvsStore::~NvsStore() {
    end();
}

bool NvsStore::keyOk(const char* key) {
    if (key == nullptr || key[0] == '\0') {
        return false;
    }
    return strlen(key) <= kNvsMaxKeyLen;
}

Status NvsStore::begin() {
    if (ready_) {
        return kOk;
    }
    if (!prefs_.begin(kNvsNamespace, false)) {
        return Err::Storage;
    }
    ready_ = true;
    return kOk;
}

void NvsStore::end() {
    if (!ready_) {
        return;
    }
    prefs_.end();
    ready_ = false;
}

Status NvsStore::putBlob(const char* key, const void* data, size_t len) {
    if (!ready_) {
        return Err::NotInit;
    }
    if (!keyOk(key) || data == nullptr || len == 0) {
        return Err::Param;
    }
    if (prefs_.putBytes(key, data, len) != len) {
        return Err::Storage;
    }
    return kOk;
}

Status NvsStore::getBlob(const char* key, void* data, size_t cap, size_t& outLen) {
    outLen = 0;
    if (!ready_) {
        return Err::NotInit;
    }
    if (!keyOk(key) || data == nullptr || cap == 0) {
        return Err::Param;
    }
    const size_t stored = prefs_.getBytesLength(key);
    if (stored == 0) {
        return Err::Storage;
    }
    if (stored > cap) {
        return Err::Range;
    }
    const size_t got = prefs_.getBytes(key, data, cap);
    if (got != stored) {
        return Err::Storage;
    }
    outLen = got;
    return kOk;
}

Status NvsStore::putU8(const char* key, uint8_t value) {
    if (!ready_) {
        return Err::NotInit;
    }
    if (!keyOk(key)) {
        return Err::Param;
    }
    if (prefs_.putUChar(key, value) != 1) {
        return Err::Storage;
    }
    return kOk;
}

Status NvsStore::getU8(const char* key, uint8_t& value) {
    if (!ready_) {
        return Err::NotInit;
    }
    if (!keyOk(key)) {
        return Err::Param;
    }
    if (prefs_.getType(key) != PT_U8) {
        return Err::Storage;
    }
    value = prefs_.getUChar(key, 0);
    return kOk;
}

Status NvsStore::putString(const char* key, const char* value) {
    if (!ready_) {
        return Err::NotInit;
    }
    if (!keyOk(key) || value == nullptr) {
        return Err::Param;
    }
    const size_t written = prefs_.putString(key, value);
    if (written != strlen(value)) {
        return Err::Storage;
    }
    if (value[0] == '\0' && !prefs_.isKey(key)) {
        return Err::Storage;
    }
    return kOk;
}

Status NvsStore::getString(const char* key, char* out, size_t cap) {
    if (!ready_) {
        return Err::NotInit;
    }
    if (!keyOk(key) || out == nullptr || cap == 0) {
        return Err::Param;
    }
    out[0] = '\0';
    if (prefs_.getType(key) != PT_STR) {
        return Err::Storage;
    }
    if (prefs_.getString(key, out, cap) == 0) {
        out[0] = '\0';
        return Err::Storage;
    }
    out[cap - 1] = '\0';
    return kOk;
}

Status NvsStore::remove(const char* key) {
    if (!ready_) {
        return Err::NotInit;
    }
    if (!keyOk(key)) {
        return Err::Param;
    }
    if (!prefs_.remove(key)) {
        return Err::Storage;
    }
    return kOk;
}
