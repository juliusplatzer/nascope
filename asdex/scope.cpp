#include "scope.h"

#include "cursors.h"
#include "maths.h"
#include "targets.h"
#include "tgtcache.h"

#include <QDebug>
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
    return (m == Mode::Day) ? QColor(0, 96, 120) : QColor(60, 60, 60);
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
    cursors_ = loadCursors(QStringLiteral("asdex/cursors.bin"), &err);
    if (!err.isEmpty()) qWarning().noquote() << "[scope] cursor load:" << err;
    if (const auto it = cursors_.constFind(QStringLiteral("scope_cursor"));
        it != cursors_.constEnd()) {
        setCursor(*it);
    }

    err.clear();
    if (!fontRenderer_.load(QStringLiteral("asdex/font.bin"), &err))
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
            QPointF closestPosNm;
            double  closestDistSq = kPickRadiusSq;
            bool    haveClosest   = false;

            QList<QPointF> historyNm;
            historyNm.reserve(7);
            for (const auto& t : cache_->targets()) {
                if (!t.lat || !t.lon) continue;
                const QPointF posNm = lonLatToNmT.map(QPointF(*t.lon, *t.lat));

                // History dots first → they sit behind the live symbol.
                historyNm.clear();
                for (const QPointF& lonLat : t.posHistory)
                    historyNm.append(lonLatToNmT.map(lonLat));
                drawHistoryDots(p, toScreen, historyNm);

                drawTarget(p, toScreen, posNm,
                           t.heading.value_or(0.0),
                           classifyTarget(t),
                           /*alert=*/false);

                if (cursorNm) {
                    const double dx = posNm.x() - cursorNm->x();
                    const double dy = posNm.y() - cursorNm->y();
                    const double d2 = dx*dx + dy*dy;
                    if (d2 <= closestDistSq) {
                        closestDistSq = d2;
                        closestPosNm  = posNm;
                        haveClosest   = true;
                    }
                }
            }

            if (haveClosest) drawHighlightRing(p, toScreen, closestPosNm);
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
}

// ---- Pan (right-click drag) -------------------------------------------------

void Scope::mousePressEvent(QMouseEvent* ev) {
    if (ev->button() == Qt::RightButton) {
        panning_    = true;
        lastPanPos_ = ev->position();
        setCursor(Qt::BlankCursor);
        ev->accept();
        return;
    }
    QWidget::mousePressEvent(ev);
}

void Scope::mouseMoveEvent(QMouseEvent* ev) {
    if (!panning_) {
        cursorPx_ = ev->position();
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
        panning_ = false;
        // Restore the ASDE-X scope cursor, not Qt's default arrow.
        if (const auto it = cursors_.constFind(QStringLiteral("scope_cursor"));
            it != cursors_.constEnd()) setCursor(*it);
        else                           unsetCursor();
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

    // Scroll up (positive) → zoom in → smaller halfRangeNm.
    halfRangeNm_ = snapZoom(halfRangeNm_ - notches * kZoomStepNm);

    ev->accept();
    update();
}

} // namespace asdex
