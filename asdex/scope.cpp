#include "scope.h"

#include "cursors.h"
#include "maths.h"
#include "targets.h"
#include "tgtcache.h"
#include "utils.h"

#include <QDebug>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace asdex {

namespace {

constexpr double kFtPerNm        = 6076.12;
constexpr double kZoomStepNm     = 100.0 / kFtPerNm;   // 100 ft per discrete level
constexpr double kMinHalfRangeNm = 6 * kZoomStepNm;    // 600 ft — tightest allowed zoom
constexpr double kMaxHalfRangeNm = 10.0;

double snapZoom(double nm) {
    const double snapped = std::round(nm / kZoomStepNm) * kZoomStepNm;
    return std::clamp(snapped, kMinHalfRangeNm, kMaxHalfRangeNm);
}

QColor backgroundFor(Mode m) {
    // sColorBackgroundDay / sColorBackgroundNight
    const QColor base = (m == Mode::Day) ? QColor(0, 96, 120) : QColor(60, 60, 60);
    return applyBrightness(base, defaultBrightness());
}

// CWT A-E (and legacy H/J) is the heavy classification used by the videomap
// rendering — anything else with tgtType == "aircraft" draws as a normal jet.
bool isHeavyWake(QStringView wake) {
    if (wake.size() != 1) return false;
    const QChar c = wake.at(0).toUpper();
    return c == 'A' || c == 'B' || c == 'C' || c == 'D' || c == 'E'
        || c == 'H' || c == 'J';
}

TargetType classifyTarget(const TgtCache::Target& t) {
    if (t.tgtType != QLatin1String("aircraft") || t.callsign.isEmpty())
        return TargetType::Unknown;
    return isHeavyWake(t.wake) ? TargetType::Heavy : TargetType::Normal;
}

} // namespace

Scope::Scope(VideoMap map, TgtCache* cache, QWidget* parent)
    : QWidget(parent), map_(std::move(map)), cache_(cache) {
    setWindowTitle(QStringLiteral("nascope — ASDE-X"));
    resize(1280, 800);
    setAutoFillBackground(true);
    setMouseTracking(true);   // hover events drive the highlight-ring pick
    setFocusPolicy(Qt::StrongFocus);
    applyBackground();

    if (map_.isValid()) {
        const QRectF b = map_.boundsNm();
        centerNm_    = b.center();
        halfRangeNm_ = snapZoom(0.5 * std::max(b.width(), b.height()));
    }

    QString err;
    cursors_ = loadCursors(QStringLiteral("asdex/assets"), &err);
    if (!err.isEmpty()) qWarning().noquote() << "[scope] cursor load:" << err;
    if (const auto it = cursors_.constFind(QStringLiteral("scope_cursor"));
        it != cursors_.constEnd()) {
        setCursor(*it);
    }

    err.clear();
    if (!fontRenderer_.load(QStringLiteral("asdex/assets/font.bin"), &err))
        qWarning().noquote() << "[scope] font load:" << err;

    // Tick the coast-list clock at 1 Hz.
    auto* clockTimer = new QTimer(this);
    connect(clockTimer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
    clockTimer->start(1000);

    // Repaint whenever the cache changes; Qt coalesces multiple updates into
    // a single paint per event-loop pass.
    if (cache_) {
        connect(cache_, &TgtCache::changed, this, QOverload<>::of(&QWidget::update));
    }
}

void Scope::setMode(Mode m) {
    if (mode_ == m) return;
    mode_ = m;
    applyBackground();
    update();
}

void Scope::applyBackground() {
    QPalette pal = palette();
    pal.setColor(QPalette::Window, backgroundFor(mode_));
    setPalette(pal);
}

// ---- Paint ------------------------------------------------------------------

void Scope::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setClipRect(rect());  // belt-and-braces against off-screen path overflow

    if (map_.isValid()) {
        p.setRenderHint(QPainter::Antialiasing);
        const QTransform toScreen = nmToScreen(centerNm_, halfRangeNm_, size());
        map_.render(p, toScreen, mode_);

        if (cache_ && !cache_->targets().isEmpty()) {
            const QTransform lonLatToNmT = lonLatToNm(map_.anchorLonLat());

            // Pick state — track the closest target whose NM position falls
            // within 150 ft of the cursor. Skip while panning (cursor is
            // hidden + the world is dragging under the pointer).
            constexpr double kPickRadiusNm   = 150.0 / kFtPerNm;
            constexpr double kPickRadiusSq  = kPickRadiusNm * kPickRadiusNm;
            std::optional<QPointF> cursorNm;
            if (cursorPx_ && !panning_) {
                bool ok = false;
                const QTransform screenToNm = toScreen.inverted(&ok);
                if (ok) cursorNm = screenToNm.map(*cursorPx_);
            }
            QPointF    closestPosNm;
            TargetType closestType  = TargetType::Normal;
            double     closestDistSq = kPickRadiusSq;
            bool       haveClosest  = false;

            QList<QPointF> historyNm;
            historyNm.reserve(7);
            for (auto it = cache_->targets().constBegin(); it != cache_->targets().constEnd(); ++it) {
                const QString&         key = it.key();
                const TgtCache::Target& t  = it.value();
                if (!t.lat || !t.lon) continue;
                const QPointF posNm = lonLatToNmT.map(QPointF(*t.lon, *t.lat));

                // History dots first → they sit behind the live symbol.
                historyNm.clear();
                for (const QPointF& lonLat : t.posHistory)
                    historyNm.append(lonLatToNmT.map(lonLat));
                drawHistoryDots(p, toScreen, historyNm);

                const TargetType type = classifyTarget(t);
                drawTarget(p, toScreen, posNm,
                           t.heading.value_or(0.0),
                           type,
                           /*alert=*/false);

                // Velocity vector — drawn right after the symbol so it lands
                // on top of it, matching CRC's queue order. Skip for Unknown
                // targets, and skip when track or speed is unknown (drawing
                // a north-pointing zero-length line would be misleading).
                if (type != TargetType::Unknown && t.heading && t.speed && *t.speed > 0.0) {
                    drawVectorLine(p, toScreen, posNm, *t.heading, *t.speed);
                }

                // Leader line + datablock — only for identified targets, when
                // the global F6 toggle is on, and the user hasn't toggled
                // this specific target off via left-click. Leader sits above
                // the symbol; datablock at the leader endpoint.
                if (type != TargetType::Unknown && showAllDatablocks_
                                                && !hiddenDatablocks_.contains(key)) {
                    constexpr double kLeaderAngleDeg = 45.0;  // NE default
                    const QPointF anchorPx = drawLeaderLine(p, toScreen, posNm, kLeaderAngleDeg);

                    DatablockFields f;
                    f.callsign      = t.callsign;
                    f.beacon        = t.squawk;
                    f.hasFlightPlan = !t.exitFix.isEmpty();
                    if (t.altitude) f.altitudeFt = static_cast<int>(*t.altitude);
                    f.acType        = t.acType;
                    f.category      = t.wake;
                    f.exitFix       = t.exitFix;
                    if (t.speed) f.speedKt = static_cast<int>(*t.speed);

                    if (fontRenderer_.isValid())
                        drawDatablock(p, fontRenderer_, anchorPx, kLeaderAngleDeg, f);
                }

                if (cursorNm) {
                    const double dx = posNm.x() - cursorNm->x();
                    const double dy = posNm.y() - cursorNm->y();
                    const double d2 = dx*dx + dy*dy;
                    if (d2 <= closestDistSq) {
                        closestDistSq = d2;
                        closestPosNm  = posNm;
                        closestType   = type;
                        haveClosest   = true;
                    }
                }
            }

            if (haveClosest)
                drawHighlightRing(p, toScreen, closestPosNm,
                                  /*heavy=*/closestType == TargetType::Heavy);
        }
    }

    // 4 px green scope boundary, drawn fully inside the widget so none of the
    // stroke gets clipped at the window edge.
    p.setRenderHint(QPainter::Antialiasing, false);
    QPen borderPen(QColor(0, 255, 0), 4);
    borderPen.setJoinStyle(Qt::MiterJoin);
    p.setPen(borderPen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(rect()).adjusted(2, 2, -2, -2));

    if (fontRenderer_.isValid())
        lists_.draw(p, size(), fontRenderer_);

    // Display Control Bar — last, so it sits above the scope, lists, and border.
    dcb::render(p, fontRenderer_, size(), dcbCfg_);
}

// ---- Hover cursor ----------------------------------------------------------

void Scope::updateHoverCursor() {
    if (!cursorPx_) return;
    const QRect dcbStripe = dcb::stripeRect(fontRenderer_, size(), dcbCfg_);
    const bool overDcb = !dcbStripe.isEmpty() && dcbStripe.contains(cursorPx_->toPoint());
    const QString want = overDcb ? QStringLiteral("dcb_cursor")
                                 : QStringLiteral("scope_cursor");
    if (const auto it = cursors_.constFind(want); it != cursors_.constEnd()) {
        setCursor(*it);
    }
}

// ---- Click pick + datablock toggle -----------------------------------------

std::optional<QString> Scope::pickClosestTargetKey(QPointF pxPos) const {
    if (!cache_ || cache_->targets().isEmpty()) return std::nullopt;

    const QTransform toScreen = nmToScreen(centerNm_, halfRangeNm_, size());
    bool ok = false;
    const QTransform screenToNm = toScreen.inverted(&ok);
    if (!ok) return std::nullopt;

    const QPointF cursorNm = screenToNm.map(pxPos);
    const QTransform lonLatToNmT = lonLatToNm(map_.anchorLonLat());

    constexpr double kPickRadiusNm = 150.0 / kFtPerNm;
    constexpr double kPickRadiusSq = kPickRadiusNm * kPickRadiusNm;

    std::optional<QString> bestKey;
    double bestDistSq = kPickRadiusSq;

    for (auto it = cache_->targets().constBegin(); it != cache_->targets().constEnd(); ++it) {
        const TgtCache::Target& t = it.value();
        if (!t.lat || !t.lon) continue;
        if (classifyTarget(t) == TargetType::Unknown) continue;
        const QPointF posNm = lonLatToNmT.map(QPointF(*t.lon, *t.lat));
        const double dx = posNm.x() - cursorNm.x();
        const double dy = posNm.y() - cursorNm.y();
        const double d2 = dx*dx + dy*dy;
        if (d2 <= bestDistSq) {
            bestDistSq = d2;
            bestKey    = it.key();
        }
    }
    return bestKey;
}

// ---- Pan (right-click drag) -------------------------------------------------

void Scope::mousePressEvent(QMouseEvent* ev) {
    if (ev->button() == Qt::RightButton) {
        // Right-click within 150 ft of a non-Unknown target → open the
        // datablock editor for it. Otherwise fall through to pan-drag.
        if (auto key = pickClosestTargetKey(ev->position())) {
            enterEditMode(*key);
            ev->accept();
            return;
        }
        panning_    = true;
        lastPanPos_ = ev->position();
        setCursor(Qt::BlankCursor);
        ev->accept();
        return;
    }
    if (ev->button() == Qt::LeftButton) {
        // Left-click within 150 ft of a non-Unknown target → toggle that
        // target's leader line + datablock visibility.
        if (auto key = pickClosestTargetKey(ev->position())) {
            if (!hiddenDatablocks_.remove(*key)) hiddenDatablocks_.insert(*key);
            update();
            ev->accept();
            return;
        }
    }
    QWidget::mousePressEvent(ev);
}

void Scope::mouseMoveEvent(QMouseEvent* ev) {
    if (!panning_) {
        cursorPx_ = ev->position();
        updateHoverCursor();
        update();
        QWidget::mouseMoveEvent(ev);
        return;
    }

    const QPointF now = ev->position();
    const QPointF dPx = now - lastPanPos_;
    lastPanPos_       = now;

    // Convert pixel delta → NM delta using the active pxPerNm.
    const double availW   = size().width()  * (1.0 - 2.0 * kViewportMargin);
    const double availH   = size().height() * (1.0 - 2.0 * kViewportMargin);
    const double radiusPx = 0.5 * std::min(availW, availH);
    if (radiusPx <= 0.0) { ev->accept(); return; }
    const double pxPerNm = radiusPx / halfRangeNm_;

    // Dragging right moves the world right → scope center shifts west.
    // Screen-y is flipped vs. NM-north, hence the sign flip on y.
    centerNm_.rx() -= dPx.x() / pxPerNm;
    centerNm_.ry() += dPx.y() / pxPerNm;

    ev->accept();
    update();
}

void Scope::mouseReleaseEvent(QMouseEvent* ev) {
    if (panning_ && ev->button() == Qt::RightButton) {
        panning_  = false;
        cursorPx_ = ev->position();
        // Re-evaluate hover so we land on dcb_cursor / scope_cursor as appropriate
        // instead of leaving the BlankCursor from the pan in place.
        updateHoverCursor();
        ev->accept();
        return;
    }
    QWidget::mouseReleaseEvent(ev);
}

void Scope::leaveEvent(QEvent* ev) {
    cursorPx_.reset();
    update();
    QWidget::leaveEvent(ev);
}

// ---- Zoom (wheel / trackpad scroll) ----------------------------------------

void Scope::wheelEvent(QWheelEvent* ev) {
    // Ignore macOS trackpad momentum: those events fire after the user's
    // fingers have left the pad and otherwise keep zooming silently.
    if (ev->phase() == Qt::ScrollMomentum) { ev->ignore(); return; }

    wheelRemainder_ += ev->angleDelta().y();
    const int notches = wheelRemainder_ / 120;
    if (notches == 0) { ev->accept(); return; }
    wheelRemainder_ -= notches * 120;

    // While the datablock editor is open the wheel cycles the active field
    // (CRC: scroll up → previous field, scroll down → next field).
    if (edit_.active) {
        cycleEditField(-notches);
        ev->accept();
        return;
    }

    // Scroll up (positive) → zoom in → smaller halfRangeNm.
    halfRangeNm_ = snapZoom(halfRangeNm_ - notches * kZoomStepNm);

    ev->accept();
    update();
}

// ---- Datablock editor ------------------------------------------------------

void Scope::keyPressEvent(QKeyEvent* ev) {
    // F6 — global datablock toggle. Handled regardless of edit-mode state.
    if (ev->key() == Qt::Key_F6) {
        showAllDatablocks_ = !showAllDatablocks_;
        update();
        ev->accept();
        return;
    }
    if (!edit_.active) { QWidget::keyPressEvent(ev); return; }

    const int key = ev->key();
    if (key == Qt::Key_Up) {
        cycleEditField(-1);
        ev->accept();
        return;
    }
    if (key == Qt::Key_Down || key == Qt::Key_Tab) {
        cycleEditField(+1);
        ev->accept();
        return;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        // Enter on the last field (SP2) saves and exits — see CRC docs:
        // "Press Enter after scratchpad 2 to save."
        if (edit_.field == EditField::Sp2) commitEdit();
        else                               cycleEditField(+1);
        ev->accept();
        return;
    }
    if (key == Qt::Key_Backspace) {
        clearCurrentEditField();
        ev->accept();
        return;
    }
    if (key == Qt::Key_Escape) {
        // Not in CRC's docs but useful: cancel without saving.
        exitEditMode();
        ev->accept();
        return;
    }

    const QString text = ev->text();
    if (!text.isEmpty()) {
        for (const QChar c : text) {
            if (c.isPrint()) appendToCurrentEditField(c);
        }
        ev->accept();
        return;
    }
    QWidget::keyPressEvent(ev);
}

void Scope::enterEditMode(const QString& key) {
    if (!cache_) return;
    const auto it = cache_->targets().constFind(key);
    if (it == cache_->targets().constEnd()) return;

    edit_.active = true;
    edit_.key    = key;
    edit_.field  = EditField::Aircraft;

    // Seed buffers from the cache; uppercase to match ATC convention.
    const TgtCache::Target& t = it.value();
    edit_.values[int(EditField::Aircraft)] = t.callsign.toUpper();
    edit_.values[int(EditField::Beacon)]   = t.squawk;
    edit_.values[int(EditField::Category)] = t.wake.toUpper();
    edit_.values[int(EditField::Type)]     = t.acType.toUpper();
    edit_.values[int(EditField::Fix)]      = t.exitFix.toUpper();
    edit_.values[int(EditField::Sp1)].clear();   // scratchpads not tracked yet
    edit_.values[int(EditField::Sp2)].clear();

    refreshPreviewFromEdit();
}

void Scope::commitEdit() {
    if (!edit_.active) return;
    if (cache_) {
        cache_->applyDatablockEdit(edit_.key,
                                   edit_.values[int(EditField::Aircraft)],
                                   edit_.values[int(EditField::Beacon)],
                                   edit_.values[int(EditField::Category)],
                                   edit_.values[int(EditField::Type)],
                                   edit_.values[int(EditField::Fix)]);
    }
    exitEditMode();
}

void Scope::exitEditMode() {
    if (!edit_.active) return;
    edit_ = {};
    auto& pa = lists_.preview();
    pa.commandLines.clear();
    pa.showCursor = false;
    update();
}

void Scope::cycleEditField(int delta) {
    if (!edit_.active || delta == 0) return;
    const int next = std::clamp(int(edit_.field) + delta, 0, kEditFieldCount - 1);
    edit_.field = static_cast<EditField>(next);
    refreshPreviewFromEdit();
}

void Scope::clearCurrentEditField() {
    if (!edit_.active) return;
    edit_.values[int(edit_.field)].clear();
    refreshPreviewFromEdit();
}

void Scope::appendToCurrentEditField(QChar c) {
    if (!edit_.active) return;
    // Scratchpads (SP1/SP2) accept up to 7 chars per the doc but the cache
    // doesn't carry them yet — leave read-only until we wire scratchpad state.
    if (edit_.field == EditField::Sp1 || edit_.field == EditField::Sp2) return;
    edit_.values[int(edit_.field)] += c.toUpper();
    refreshPreviewFromEdit();
}

void Scope::refreshPreviewFromEdit() {
    auto& pa = lists_.preview();
    if (!edit_.active) {
        pa.commandLines.clear();
        pa.showCursor = false;
        update();
        return;
    }

    static constexpr const char* kLabels[kEditFieldCount] = {
        "A/C", "BCN", "CAT", "TYP", "FIX", "SP1", "SP2",
    };
    constexpr int kLabelColumns = 5;  // "XYZ: " — label + colon + space, fixed cursor offset

    pa.commandLines.clear();
    pa.commandLines.reserve(kEditFieldCount);
    for (int i = 0; i < kEditFieldCount; ++i) {
        pa.commandLines << QStringLiteral("%1: %2")
                               .arg(QString::fromLatin1(kLabels[i]),
                                    edit_.values[i]);
    }

    pa.showCursor   = true;
    pa.cursorLine   = int(edit_.field);   // index within commandLines
    pa.cursorColumn = kLabelColumns + edit_.values[int(edit_.field)].size();
    update();
}

} // namespace asdex
