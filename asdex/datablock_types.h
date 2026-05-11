#pragma once

namespace asdex {

enum class LeaderDirection {
    NE,
    N,
    E,
    SE,
    S,
    SW,
    W,
    NW,
};

enum class DataBlockVisibility {
    Inherit,
    ForceOn,
    ForceOff,
};

struct DataBlockSettings {
    bool showDataBlocks = true;
    bool fullDataBlocks = true;

    int fontSize = 2;
    int brightness = 95;
    int leaderLength = 2;
    LeaderDirection leaderDirection = LeaderDirection::NE;
    bool timesharePrimary = true;
    bool alertInProgress = false;

    bool showAltitude = false;
    bool showAircraftType = true;
    bool showSensors = false;
    bool showAircraftCategory = false;
    bool showFix = true;
    bool showVelocity = false;
    bool showScratchpads = true;
};

} // namespace asdex
