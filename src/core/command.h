// Registro estatico de comandos do console. console.cpp nunca conhece um comando concreto.
#pragma once

#include <stdint.h>

struct Ctx;

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual const char* name() const = 0;
    virtual const char* usage() const = 0;
    virtual void execute(Ctx& ctx, uint8_t argc, const char* const* argv) = 0;
};

class CommandRegistry {
public:
    static constexpr uint8_t kMax = 40;
    static bool add(ICommand* command);
    static uint8_t count();
    static ICommand* at(uint8_t index);
    static ICommand* find(const char* name);
};

#define REGISTER_COMMAND(TypeName)                                 \
    namespace {                                                    \
    TypeName g_##TypeName##_instance;                              \
    const bool g_##TypeName##_registered =                         \
        CommandRegistry::add(&g_##TypeName##_instance);            \
    }                                                              \
    static_assert(true, "")
