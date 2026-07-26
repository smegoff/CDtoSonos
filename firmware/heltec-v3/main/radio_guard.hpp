#pragma once

namespace cdts {

// Holds the SX1262 in hardware reset and leaves all RF-capable pins inert.
// This must be the first hardware operation performed by app_main.
void disable_lora_radio();

}  // namespace cdts

