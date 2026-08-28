// Contexto injetado em testes e comandos. Agregado de interfaces estreitas, montado em main.cpp.
#pragma once

#include "console_io.h"
#include "core/report.h"
#include "drivers/calibration.h"
#include "iface/ianalog_output.h"
#include "iface/ibuttons.h"
#include "iface/idigital_output_bank.h"
#include "iface/idisplay.h"
#include "iface/ioperator.h"
#include "iface/isafe_state.h"
#include "iface/iserial_transport.h"
#include "iface/iwatchdog.h"
#include "core/itest_runner.h"
#include "kv_store.h"
#include "proto/irs485_protocol.h"

struct BootInfo {
    uint32_t resetReason;
    const char* resetReasonName;
    uint8_t strappingLevel[8];
    uint8_t strappingCount;
    uint32_t flashSizeBytes;
    uint32_t chipIdLow;
    uint32_t chipIdHigh;
    char macText[20];
    bool wdtResetExpected;
    bool wdtResetObserved;
};

struct Ctx {
    IConsoleIO& io;
    IOperator& op;
    IAnalogOutput& ao;
    IDigitalOutputBank& relays;
    ISerialTransport& rs485;
    IRs485Protocol& proto;
    IDisplay& display;
    IButtons& buttons;
    IWatchdog& wdt;
    CalibrationStore& cal;
    IKeyValueStore& kv;
    Report& report;
    ISafeState& safe;
    BootInfo& boot;
    ITestRunner* runner;
    const char* fwVersion;
    const char* boardRev;
};
