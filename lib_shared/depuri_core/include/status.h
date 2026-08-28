// Codigos de erro dos drivers. Sem excecoes, sem impressao dentro de driver.
#pragma once

#include <stdint.h>

enum class Err : uint8_t {
    Ok = 0,
    Param,
    Range,
    Timeout,
    NotInit,
    NotCalibrated,
    Busy,
    Io,
    Crc,
    Storage,
    Unsupported,
    Aborted,
    HwFault,
};

struct Status {
    Err err;

    constexpr Status() : err(Err::Ok) {}
    constexpr Status(Err e) : err(e) {}
    constexpr bool ok() const { return err == Err::Ok; }
    constexpr bool failed() const { return err != Err::Ok; }
    constexpr explicit operator bool() const { return err == Err::Ok; }
};

constexpr Status kOk{Err::Ok};

const char* errName(Err e);
