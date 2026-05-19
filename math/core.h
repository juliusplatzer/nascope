#ifndef MATH_CORE_H_
#define MATH_CORE_H_

#include <cmath>

namespace math {

inline constexpr double kFeetPerNm = 6076.12;
inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kDegreesToRadians = kPi / 180.0;
inline constexpr double kRadiansToDegrees = 180.0 / kPi;
inline constexpr double kEarthRadiusMeters = 6371008.8;

inline double degreesToRadians(double degrees) {
    return degrees * kDegreesToRadians;
}

inline double radiansToDegrees(double radians) {
    return radians * kRadiansToDegrees;
}

inline int normalizedDegrees(int degrees) {
    return ((degrees % 360) + 360) % 360;
}

inline double normalizedDegrees(double degrees) {
    const double value = std::fmod(degrees, 360.0);
    return value < 0.0 ? value + 360.0 : value;
}

} // namespace math

#endif  // MATH_CORE_H_
