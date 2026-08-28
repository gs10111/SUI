// Estado seguro centralizado: reles off, DAC em zero, OP_MODE tensao, display apagado.
#pragma once

class ISafeState {
public:
    virtual ~ISafeState() = default;
    virtual void enterSafeState() = 0;
};
