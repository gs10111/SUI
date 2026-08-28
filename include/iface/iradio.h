// Visao estreita do radio para quem so precisa saber se ele esta ligado (medida analogica de +/-0,5 % FE).
#pragma once

#include "status.h"

class IRadio {
public:
    virtual ~IRadio() = default;
    virtual bool running() const = 0;
    virtual const char* modeName() const = 0;
    virtual Status stop() = 0;
};
