// Persistencia em NVS do ESP32-WROOM-32D (folha 1/2, U1): namespace unico aberto no begin().
// Datasheet aplicavel: ESP32-WROOM-32D (flash 4 MB, particao nvs).
#pragma once

#include <Preferences.h>
#include <stddef.h>
#include <stdint.h>

#include "kv_store.h"
#include "status.h"

constexpr const char* kNvsNamespace = "depuri1";
constexpr size_t kNvsMaxKeyLen = 15;

class NvsStore : public IKeyValueStore {
public:
    NvsStore();
    ~NvsStore() override;

    Status begin();
    void end();
    bool ready() const { return ready_; }

    Status putBlob(const char* key, const void* data, size_t len) override;
    Status getBlob(const char* key, void* data, size_t cap, size_t& outLen) override;
    Status putU8(const char* key, uint8_t value) override;
    Status getU8(const char* key, uint8_t& value) override;
    Status putString(const char* key, const char* value) override;
    Status getString(const char* key, char* out, size_t cap) override;
    Status remove(const char* key) override;

private:
    static bool keyOk(const char* key);

    Preferences prefs_;
    bool ready_;
};
