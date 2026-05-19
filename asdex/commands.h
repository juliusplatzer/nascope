#ifndef ASDEX_COMMANDS_H_
#define ASDEX_COMMANDS_H_

namespace asdex {

// Central registry of operator command types. New command implementations
// add their type here and live in the corresponding cmd*.cpp file:
//
//   Slew commands (clicking a target):       cmdslew.cpp
//   Setup / DCB-entry commands (chapter 4):  cmdsetup.cpp
enum class CommandType {
    None,

    // --- cmdslew ---
    EditDatablockFields,

    // --- cmdsetup ---
    Range,
    Rotate,
    VectorLength,
    LeaderLength,
    LeaderDirection,
    MapReposition,
    Brightness,
    HoldBarsBrightness,
    MovementAreasBrightness,
    BackgroundBrightness,
    TrackBrightness,
    DataBlocksBrightness,
    ListsBrightness,
    TempMapAreasBrightness,
    TempMapTextBrightness,
    DcbBrightness,

    CharSize,
    DataBlockCharSize,
    DcbCharSize,
    CoastSuspendCharSize,
    TempDataCharSize,
    PreviewAreaCharSize,

    TempData,
    DefineTempText,
    DefineTempTextLocation,

    DbArea,
    DefineTraitArea,
    DefineTraitAreaTraits,
    DefineTraitAreaDbCharSize,
    DefineTraitAreaDbBrightness,
    DefineTraitAreaLeaderLength,
    DefineTraitAreaLeaderDirection,
    DefineOffArea,
    ModifyTraitArea,
    ModifyTraitAreaTraits,
    ModifyTraitAreaDbCharSize,
    ModifyTraitAreaDbBrightness,
    ModifyTraitAreaLeaderLength,
    ModifyTraitAreaLeaderDirection,
    DeleteAllDbAreas,
    DeleteOneDbArea,

    DbEdit,
};

}  // namespace asdex

#endif  // ASDEX_COMMANDS_H_
