#pragma once

namespace idryer_touch {
namespace ui {

// Builds the screens. Call after display::begin() succeeds.
void begin();

// Refreshes from the current DeviceView. Cheap and self-rate-limiting.
void tick();

} // namespace ui
} // namespace idryer_touch
