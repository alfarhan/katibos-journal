#include "MassStorage.h"
#include "app/app.h"

#ifdef BOARD_ESP32_S3
#include "esp32/MassStorageESP32.h"
#endif

//
void ms_setup()
{
    // Register callback for host ejection
    _log("Mass Storage Setup\n");
}

//
void ms_loop()
{
#ifdef BOARD_ESP32_S3
    ms_esp32_loop();
#endif    
}