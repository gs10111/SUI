// Abstracao de armazenamento nao volatil. Impl ESP32 = NVS/Preferences; host = mock.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "status.h"

class IKeyValueStore {
public:
    virtual ~IKeyValueStore() = default;
    virtual Status putBlob(const char* key, const void* data, size_t len) = 0;
    virtual Status getBlob(const char* key, void* data, size_t cap, size_t& outLen) = 0;
    virtual Status putU8(const char* key, uint8_t value) = 0;
    virtual Status getU8(const char* key, uint8_t& value) = 0;
    virtual Status putString(const char* key, const char* value) = 0;
    virtual Status getString(const char* key, char* out, size_t cap) = 0;
    virtual Status remove(const char* key) = 0;
};
