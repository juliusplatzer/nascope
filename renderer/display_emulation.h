#ifndef RENDERER_DISPLAY_EMULATION_H_
#define RENDERER_DISPLAY_EMULATION_H_

#include <QSize>

namespace renderer {

enum class DisplayEmulationMode {
    Native,
    NecLcd2190Uxp,
};

enum class DisplayScaleMode {
    Stretch,
    FitPreserveAspect,
};

struct DisplayEmulationSettings {
    DisplayEmulationMode mode = DisplayEmulationMode::Native;
    DisplayScaleMode scaleMode = DisplayScaleMode::FitPreserveAspect;

    float blackLift = 0.008f;
    float contrastScale = 0.92f;
    float saturationScale = 0.88f;

    float whiteBalanceR = 1.025f;
    float whiteBalanceG = 1.000f;
    float whiteBalanceB = 0.940f;

    float pixelGridStrength = 0.035f;
    float temporalBlend = 0.04f;

    static DisplayEmulationSettings necLcd2190Uxp();
};

struct FrameSpec {
    QSize sceneFramebufferSize;
    QSize outputFramebufferSize;
    DisplayEmulationSettings display;
};

}  // namespace renderer

#endif  // RENDERER_DISPLAY_EMULATION_H_
