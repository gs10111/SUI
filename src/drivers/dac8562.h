// DAC8562 duplo de 16 bits no barramento do DAC (folha 2/2), TI SLAS719E tabelas 8, 9 e 17.
// SYNC em IO12 (strapping MTDI): so vira saida em nivel alto dentro de begin(), nunca antes do boot.
// LDAC esta em nivel alto por R15 10K para +5 V, entao o registro LDAC ignora o pino (SLAS719E tabela 17).
#pragma once

#include <stdint.h>

#include "board_pins.h"
#include "drivers/spi_bus.h"
#include "status.h"

class Dac8562 {
public:
    static constexpr uint8_t kChannelCount = 2;
    static constexpr uint16_t kFullScaleCode = 0xFFFF;

    Dac8562(SpiBus& bus, board::Pin sync, uint32_t clockHz);

    Status begin();
    Status writeChannel(uint8_t ch, uint16_t code);
    Status softReset();
    Status powerUpBoth();
    Status enableInternalRefGain2();
    Status ignoreLdacPin();
    Status setClockHz(uint32_t hz);
    uint32_t clockHz() const { return clockHz_; }
    uint16_t lastCode(uint8_t ch) const;
    bool ready() const { return ready_; }

private:
    Status writeFrame(uint8_t cmd, uint16_t data);
    static uint32_t clampHz(uint32_t hz);

    SpiBus& bus_;
    board::Pin sync_;
    uint32_t clockHz_;
    uint16_t lastCode_[kChannelCount];
    bool ready_;
};
