#pragma once

#include <QColor>

#include <algorithm>

namespace asdex {

enum class Mode { Day, Night };

inline constexpr int kBrightnessMin = 1;
inline constexpr int kBrightnessMax = 99;
inline constexpr int kBrightnessDefault = 95;
inline constexpr int kBrightnessFloorDefault = 20;

inline QColor applyBrightness(const QColor& color,
                              int brightness = kBrightnessDefault,
                              int minBrightness = kBrightnessFloorDefault) {
    brightness = std::clamp(brightness, kBrightnessMin, kBrightnessMax);
    minBrightness = std::clamp(minBrightness, 0, 100);

    const double scale =
        (brightness * (100.0 - minBrightness) / 100.0 + minBrightness) / 100.0;

    const auto channel = [scale](int value) {
        return std::clamp(static_cast<int>(value * scale), 0, 255);
    };

    return QColor(channel(color.red()), channel(color.green()), channel(color.blue()), color.alpha());
}

inline QColor backgroundColor(Mode mode) {
    return (mode == Mode::Day) ? QColor(0, 96, 120) : QColor(60, 60, 60);
}

} // namespace asdex
