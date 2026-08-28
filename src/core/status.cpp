// Nomes legiveis de Err. Unico lugar que traduz codigo em texto.
#include "status.h"

const char* errName(Err e) {
    switch (e) {
        case Err::Ok: return "OK";
        case Err::Param: return "PARAM";
        case Err::Range: return "RANGE";
        case Err::Timeout: return "TIMEOUT";
        case Err::NotInit: return "NOT_INIT";
        case Err::NotCalibrated: return "NOT_CALIBRATED";
        case Err::Busy: return "BUSY";
        case Err::Io: return "IO";
        case Err::Crc: return "CRC";
        case Err::Storage: return "STORAGE";
        case Err::Unsupported: return "UNSUPPORTED";
        case Err::Aborted: return "ABORTED";
        case Err::HwFault: return "HW_FAULT";
    }
    return "?";
}
