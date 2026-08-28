// XTR300AIRGWT: saida de tensao ou corrente dos eixos X e Y (folha 2/2), TI SBOS404.
// M1 amarrado em L; OP_MODE baixo = tensao, alto = corrente. Falhas (EFOT/EFLD/EFCM) so acendem LD1..LD6.
#pragma once

#include <stdint.h>

#include "board_pins.h"
#include "drivers/calibration.h"
#include "drivers/dac8562.h"
#include "iface/ianalog_output.h"
#include "status.h"

class Xtr300AnalogOutput : public IAnalogOutput {
public:
    Xtr300AnalogOutput(Dac8562& dac, board::Pin opModePin, CalibrationStore& cal);

    Status begin() override;
    Status setRaw(uint8_t axis, uint16_t code) override;
    Status getRaw(uint8_t axis, uint16_t& code) const override;
    Status setEngineering(uint8_t axis, float value) override;
    Status setMode(AoMode desired) override;
    AoMode mode() const override { return mode_; }
    Status zeroAll() override;
    Status setSpiHz(uint32_t hz) override;
    uint32_t spiHz() const override;
    uint16_t fullScaleCode() const override { return Dac8562::kFullScaleCode; }

    bool ready() const { return ready_; }

private:
    void driveOpMode(AoMode desired);

    Dac8562& dac_;
    board::Pin opModePin_;
    CalibrationStore& cal_;
    AoMode mode_;
    uint16_t lastCode_[board::kAxisCount];
    bool ready_;
};
