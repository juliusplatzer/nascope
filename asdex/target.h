#ifndef ASDEX_TARGET_H_
#define ASDEX_TARGET_H_

#include "asdex/colors.h"

#include <QMatrix4x4>
#include <QPointF>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>

namespace renderer {
class CommandBuffer;
}

namespace asdex {

struct TargetHistoryPoint {
    QPointF positionFeet;
};

struct AsdexTarget {
    QString id;
    QString callsign;
    QString aircraftType;
    QString category;
    QString beaconCode;
    QString fix;
    QString scratchpad1;
    QString scratchpad2;

    QPointF positionFeet;

    // Aviation convention: 0 = north, 90 = east, 180 = south, 270 = west.
    double headingDegrees = 0.0;
    double groundTrackDegrees = 0.0;
    double groundSpeedKnots = 0.0;

    std::optional<double> altitudeTrue;

    bool correlated = true;
    bool heavy = false;
    bool duplicateBeaconCode = false;
    bool coasting = false;
    bool highlighted = false;

    QVector<TargetHistoryPoint> history;
};

void drawTargets(const QVector<AsdexTarget>& targets,
                 renderer::CommandBuffer* commandBuffer,
                 const QMatrix4x4& worldProjection,
                 Mode mode,
                 int vectorSeconds,
                 const std::function<bool(const AsdexTarget&)>& vectorVisibleForTarget,
                 int brightness = 95);

int clampedTargetVectorSeconds(int seconds);

} // namespace asdex

#endif  // ASDEX_TARGET_H_
