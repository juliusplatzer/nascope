#pragma once

#include <QPointF>
#include <QString>
#include <QVector>

#include <optional>

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

} // namespace asdex
