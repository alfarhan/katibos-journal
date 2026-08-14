#include "display.h"
#include "service/Idle/Idle.h"
#include "app/app.h"

// Reflective LCD
#include "display/RLCD/display_RLCD.h"

//
void display_setup()
{
  display_RLCD_setup();

  // Identifying which screen to show
  JsonDocument &app = status();
  int screen = app["screen"].as<int>();
  app["screen_prev"] = -1;

  //
  _log("Display initializing with screen %d\n", screen);

  // Show Error Screen if error is set
  if (screen == ERRORSCREEN)
  {
    _log("Display Error Screen\n");
    return;
  }
  else if (screen == UPDATESCREEN)
  {
    // go to firmware update screen
    _log("Firmware Update Screen\n");
    return;
  }
  else
  {
    //
    // if screen is not specified
    // then load the wake animation then the word processor
    //
    // boot straight into the editor
    app["screen"] = WORDPROCESSOR;
  }

  //
  _log("Display loading screen %d\n", app["screen"].as<int>());
}

//
void display_loop()
{
  display_RLCD_loop();
}

//
void display_keyboard(int key, bool pressed, int index)
{
    // Any key ends the idle throttle before the event is dispatched, so the
    // screen is back at full refresh by the time this keystroke renders.
    idle_touch();

  _debug("[display_keyboard] Key: [%d] pressed: %d index: %d\n", key, pressed, index);

  display_RLCD_keyboard(key, pressed, index);
}

int display_core()
{
  return display_RLCD_core();
}
