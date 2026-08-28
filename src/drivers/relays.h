// Banco dos reles de limite RL2..RL5 (folha 2/2): ESP32 -> BC337 -> bobina AX1RC-5V.
// Ativo em nivel ALTO; estado seguro (bobinas desligadas) e nivel BAIXO em todos os pinos.
#pragma once

#include <stdint.h>

#include "board_pins.h"
#include "iface/idigital_output_bank.h"
#include "status.h"

class RelayBank : public IDigitalOutputBank {
public:
    RelayBank();

    Status begin() override;
    uint8_t count() const override;
    Status set(uint8_t index, bool on) override;
    Status get(uint8_t index, bool& on) const override;
    Status allOff() override;
    Status allOn() override;

    const board::RelayMap& info(uint8_t index) const;
    board::Pin pin(uint8_t index) const;
    bool ready() const { return ready_; }

private:
    static bool indexOk(uint8_t index);
    Status writeAll(bool on);

    bool state_[board::kRelayCount];
    bool ready_;
};
