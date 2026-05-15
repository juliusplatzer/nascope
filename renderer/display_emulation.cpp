#include "renderer/display_emulation.h"

namespace renderer {

DisplayEmulationSettings DisplayEmulationSettings::necLcd2190Uxp() {
    DisplayEmulationSettings settings;
    settings.mode = DisplayEmulationMode::NecLcd2190Uxp;
    settings.scaleMode = DisplayScaleMode::FitPreserveAspect;
    settings.blackLift = 0.008f;
    settings.contrastScale = 0.97f;
    settings.saturationScale = 0.96f;
    settings.whiteBalanceR = 1.025f;
    settings.whiteBalanceG = 1.000f;
    settings.whiteBalanceB = 0.940f;
    settings.pixelGridStrength = 0.035f;
    settings.temporalBlend = 0.04f;
    return settings;
}

}  // namespace renderer
