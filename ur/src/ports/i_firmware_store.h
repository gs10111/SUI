// src/ports/i_firmware_store.h
// Encaminha para lib_shared/depuri_ota/include/ota_firmware_store.h.
//
// A porta mora em lib_shared porque a sensora precisa EXATAMENTE da mesma, e duas copias do
// caminho que decide qual imagem vira a particao de boot divergiriam. Este arquivo existe para
// que src/ continue dizendo, no lugar de sempre, quais portas a Unidade Remota usa.
#pragma once

#include "ota_firmware_store.h"
