// Mock de IKeyValueStore para os testes de host (env native). Sem hardware, sem Arduino.
// Armazenamento em array fixo: ate 8 entradas, chave char[24] e buffer de bytes por entrada.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "kv_store.h"
#include "status.h"

template <size_t kValueCap>
class MockKvStoreT : public IKeyValueStore {
public:
    static constexpr size_t kMaxEntries = 8;
    static constexpr size_t kMaxKeyLen = 24;
    static constexpr size_t kValueBytes = kValueCap;

    MockKvStoreT() : entries_(), count_(0) {}

    Status putBlob(const char* key, const void* data, size_t len) override {
        if (!keyOk(key) || (data == nullptr && len > 0)) {
            return Status(Err::Param);
        }
        if (len > kValueCap) {
            return Status(Err::Range);
        }
        Entry* slot = slotFor(key);
        if (slot == nullptr) {
            return Status(Err::Storage);
        }
        if (len > 0) {
            memcpy(slot->bytes, data, len);
        }
        slot->size = len;
        return kOk;
    }

    Status getBlob(const char* key, void* data, size_t cap, size_t& outLen) override {
        outLen = 0;
        if (!keyOk(key) || data == nullptr) {
            return Status(Err::Param);
        }
        const Entry* slot = find(key);
        if (slot == nullptr) {
            return Status(Err::Storage);
        }
        if (slot->size > cap) {
            return Status(Err::Range);
        }
        if (slot->size > 0) {
            memcpy(data, slot->bytes, slot->size);
        }
        outLen = slot->size;
        return kOk;
    }

    Status putU8(const char* key, uint8_t value) override {
        return putBlob(key, &value, sizeof(value));
    }

    Status getU8(const char* key, uint8_t& value) override {
        if (!keyOk(key)) {
            return Status(Err::Param);
        }
        const Entry* slot = find(key);
        if (slot == nullptr) {
            return Status(Err::Storage);
        }
        if (slot->size != sizeof(uint8_t)) {
            return Status(Err::Range);
        }
        value = slot->bytes[0];
        return kOk;
    }

    Status putString(const char* key, const char* value) override {
        if (value == nullptr) {
            return Status(Err::Param);
        }
        return putBlob(key, value, strlen(value) + 1u);
    }

    Status getString(const char* key, char* out, size_t cap) override {
        if (out == nullptr || cap == 0) {
            return Status(Err::Param);
        }
        out[0] = '\0';
        if (!keyOk(key)) {
            return Status(Err::Param);
        }
        const Entry* slot = find(key);
        if (slot == nullptr) {
            return Status(Err::Storage);
        }
        if (slot->size == 0 || slot->bytes[slot->size - 1] != 0) {
            return Status(Err::Storage);
        }
        if (slot->size > cap) {
            return Status(Err::Range);
        }
        memcpy(out, slot->bytes, slot->size);
        return kOk;
    }

    Status remove(const char* key) override {
        if (!keyOk(key)) {
            return Status(Err::Param);
        }
        for (size_t i = 0; i < count_; ++i) {
            if (strncmp(entries_[i].name, key, kMaxKeyLen) == 0) {
                for (size_t j = i + 1; j < count_; ++j) {
                    entries_[j - 1] = entries_[j];
                }
                --count_;
                entries_[count_] = Entry();
                return kOk;
            }
        }
        return Status(Err::Storage);
    }

    void reset() {
        for (size_t i = 0; i < kMaxEntries; ++i) {
            entries_[i] = Entry();
        }
        count_ = 0;
    }

    size_t entryCount() const {
        return count_;
    }

    bool contains(const char* key) const {
        return find(key) != nullptr;
    }

    size_t sizeOf(const char* key) const {
        const Entry* slot = find(key);
        return (slot == nullptr) ? 0u : slot->size;
    }

private:
    struct Entry {
        char name[kMaxKeyLen];
        uint8_t bytes[kValueCap];
        size_t size;

        Entry() : name(), bytes(), size(0) {}
    };

    static bool keyOk(const char* key) {
        return key != nullptr && key[0] != '\0' && strlen(key) < kMaxKeyLen;
    }

    const Entry* find(const char* key) const {
        if (!keyOk(key)) {
            return nullptr;
        }
        for (size_t i = 0; i < count_; ++i) {
            if (strncmp(entries_[i].name, key, kMaxKeyLen) == 0) {
                return &entries_[i];
            }
        }
        return nullptr;
    }

    Entry* slotFor(const char* key) {
        for (size_t i = 0; i < count_; ++i) {
            if (strncmp(entries_[i].name, key, kMaxKeyLen) == 0) {
                return &entries_[i];
            }
        }
        if (count_ >= kMaxEntries) {
            return nullptr;
        }
        Entry* slot = &entries_[count_++];
        *slot = Entry();
        memcpy(slot->name, key, strlen(key));
        return slot;
    }

    Entry entries_[kMaxEntries];
    size_t count_;
};

using MockKvStore = MockKvStoreT<256>;
using MockKvStoreBig = MockKvStoreT<1536>;
