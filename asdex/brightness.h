#pragma once

#include <QColor>

#include <algorithm>
#include <cmath>

namespace asdex {

// CRC ASDEX brightness range and floor.
inline constexpr int kBrightnessMin      = 1;
inline constexpr int kBrightnessMax      = 99;
inline constexpr int kBrightnessDefault  = 95;
inline constexpr int kBrightnessFloorDef = 20;  // CRC's MIN_BRIGHTNESS_VALUE for ASDEX

/**
 * Mirrors CRC's RenderUtils.ApplyColorBrightness. Scales R, G, B by
 *
 *     scale = (brightness * (100 - minBrightness) / 100 + minBrightness) / 100
 *
 * preserving alpha. `minBrightness` is the bottom-end floor — at brightness=1
 * the result still has at least `minBrightness%` of the original RGB, so the
 * scope never goes pitch black. `brightness` is clamped to [1, 99].
 */
inline QColor applyBrightness(const QColor& c, int brightness,
                              int minBrightness = kBrightnessFloorDef) {
    brightness    = std::clamp(brightness,    kBrightnessMin, kBrightnessMax);
    minBrightness = std::clamp(minBrightness, 0,              100);

    const double scale =
        (brightness * (100.0 - minBrightness) / 100.0 + minBrightness) / 100.0;

    auto chan = [scale](int v) {
        return std::clamp(static_cast<int>(std::round(v * scale)), 0, 255);
    };
    return QColor(chan(c.red()), chan(c.green()), chan(c.blue()), c.alpha());
}

/**
 * Module-wide default brightness — read by draw helpers that don't carry their
 * own brightness setting (target symbols, videomap fills, scope background).
 * The DCB has its own per-Config brightness; everything else consults this.
 */
inline int g_defaultBrightness = kBrightnessDefault;
inline int  defaultBrightness()              { return g_defaultBrightness; }
inline void setDefaultBrightness(int b)      {
    g_defaultBrightness = std::clamp(b, kBrightnessMin, kBrightnessMax);
}

} // namespace asdex
