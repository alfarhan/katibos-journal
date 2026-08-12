#pragma once

//
void battery_setup();
void battery_loop();

// last sampled charge, 0-100, or -1 before the first sample / when unsupported
int battery_percent();
