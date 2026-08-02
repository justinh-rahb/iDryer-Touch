#pragma once

// Local-only entry point for iDryer Touch. Selected by -DIDRYER_TOUCH_LOCAL,
// which also compiles out src/main_v2.cpp (the cloud bridge).
namespace idryer_touch {

void setup();
void loop();

} // namespace idryer_touch
