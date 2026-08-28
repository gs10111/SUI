// Selecao de driver de display e habilitacao da IHM em tempo de compilacao (nao ha MISO: sem auto-deteccao).
#pragma once

#define DISPLAY_DRIVER_RAW 1
#define DISPLAY_DRIVER_NULL 2
#define DISPLAY_DRIVER_U8G2 3

#ifndef DISPLAY_DRIVER
#define DISPLAY_DRIVER DISPLAY_DRIVER_NULL
#endif

#ifndef IHM_ENABLED
#define IHM_ENABLED 0
#endif

#ifndef FW_VERSION
#define FW_VERSION "0.0.0-dev"
#endif

#ifndef BOARD_REV
#define BOARD_REV "A"
#endif
