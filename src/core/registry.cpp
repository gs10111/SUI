// Registro estatico de testes e comandos. Logica pura: sem dependencia de hardware, de Arduino ou do esquematico.
#include "core/command.h"
#include "core/test_runner.h"

#include <ctype.h>

#include "core/cmd_parser.h"

namespace {

struct TestSlots {
    ITest* items[TestRegistry::kMax];
    uint8_t used;
};

struct CommandSlots {
    ICommand* items[CommandRegistry::kMax];
    uint8_t used;
};

TestSlots& testSlots() {
    static TestSlots slots{};
    return slots;
}

CommandSlots& commandSlots() {
    static CommandSlots slots{};
    return slots;
}

int compareIgnoreCase(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) {
        return (a == b) ? 0 : ((a == nullptr) ? -1 : 1);
    }
    while (*a != '\0' && *b != '\0') {
        const int ca = tolower(static_cast<unsigned char>(*a));
        const int cb = tolower(static_cast<unsigned char>(*b));
        if (ca != cb) {
            return (ca < cb) ? -1 : 1;
        }
        ++a;
        ++b;
    }
    if (*a == *b) {
        return 0;
    }
    return (*a == '\0') ? -1 : 1;
}

}  // namespace

bool TestRegistry::add(ITest* test) {
    if (test == nullptr) {
        return false;
    }
    TestSlots& slots = testSlots();
    if (slots.used >= kMax) {
        return false;
    }
    uint8_t pos = slots.used;
    for (uint8_t i = 0; i < slots.used; ++i) {
        if (slots.items[i] != nullptr && test->order() < slots.items[i]->order()) {
            pos = i;
            break;
        }
    }
    for (uint8_t i = slots.used; i > pos; --i) {
        slots.items[i] = slots.items[i - 1];
    }
    slots.items[pos] = test;
    ++slots.used;
    return true;
}

uint8_t TestRegistry::count() {
    return testSlots().used;
}

ITest* TestRegistry::at(uint8_t slot) {
    TestSlots& slots = testSlots();
    if (slot >= slots.used) {
        return nullptr;
    }
    return slots.items[slot];
}

ITest* TestRegistry::find(const char* id) {
    if (id == nullptr) {
        return nullptr;
    }
    TestSlots& slots = testSlots();
    for (uint8_t i = 0; i < slots.used; ++i) {
        ITest* const test = slots.items[i];
        if (test != nullptr && cmd::equalsIgnoreCase(test->id(), id)) {
            return test;
        }
    }
    return nullptr;
}

bool CommandRegistry::add(ICommand* command) {
    if (command == nullptr) {
        return false;
    }
    CommandSlots& slots = commandSlots();
    if (slots.used >= kMax) {
        return false;
    }
    uint8_t pos = slots.used;
    for (uint8_t i = 0; i < slots.used; ++i) {
        if (slots.items[i] != nullptr && compareIgnoreCase(command->name(), slots.items[i]->name()) < 0) {
            pos = i;
            break;
        }
    }
    for (uint8_t i = slots.used; i > pos; --i) {
        slots.items[i] = slots.items[i - 1];
    }
    slots.items[pos] = command;
    ++slots.used;
    return true;
}

uint8_t CommandRegistry::count() {
    return commandSlots().used;
}

ICommand* CommandRegistry::at(uint8_t slot) {
    CommandSlots& slots = commandSlots();
    if (slot >= slots.used) {
        return nullptr;
    }
    return slots.items[slot];
}

ICommand* CommandRegistry::find(const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    CommandSlots& slots = commandSlots();
    for (uint8_t i = 0; i < slots.used; ++i) {
        ICommand* const command = slots.items[i];
        if (command != nullptr && cmd::equalsIgnoreCase(command->name(), name)) {
            return command;
        }
    }
    return nullptr;
}
