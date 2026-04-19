#include "scope.h"

#include "maths.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace asdex {

namespace {

constexpr double kMinHalfRangeNm = 0.01;
constexpr double kMaxHalfRangeNm = 10.0;
constexpr double kNmPerNotch     = 0.05;  // one wheel notch = 120 angleDelta units

QColor backgroundFor(Mode m) {
    // sColorBackgroundDay / sColorBackgroundNight
    return (m == Mode::Day) ? QColor(0, 96, 120) : QColor(60, 60, 60);
}

} // namespace

Scope::Scope(VideoMap map, QWidget* parent) : QWidget(parent), map_(std::move(map)) {
    setWindowTitle(QStringLiteral("nascope — ASDE-X"));
    resize(1280, 800);
    setAutoFillBackground(true);
    setMouseTracking(false);
    setFocusPolicy(Qt::StrongFocus);
    applyBackground();

    if (map_.isValid()) {
        const QRectF b = map_.boundsNm();
        centerNm_    = b.center();
        halfRangeNm_ = std::clamp(0.5 * std::max(b.width(), b.height()),
                                  kMinHalfRangeNm, kMaxHalfRangeNm);
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
    if (!map_.isValid()) return;

    const QTransform t = nmToScreen(centerNm_, halfRangeNm_, size());

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setClipRect(rect());  // belt-and-braces against off-screen path overflow
    map_.render(p, t, mode_);
}

// ---- Pan (right-click drag) -------------------------------------------------

void Scope::mousePressEvent(QMouseEvent* ev) {
    if (ev->button() == Qt::RightButton) {
        panning_    = true;
        lastPanPos_ = ev->position();
        setCursor(Qt::ClosedHandCursor);
        ev->accept();
        return;
    }
    QWidget::mousePressEvent(ev);
}

void Scope::mouseMoveEvent(QMouseEvent* ev) {
    if (!panning_) { QWidget::mouseMoveEvent(ev); return; }

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
        unsetCursor();
        ev->accept();
        return;
    }
    QWidget::mouseReleaseEvent(ev);
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
    const double next = halfRangeNm_ - notches * kNmPerNotch;
    halfRangeNm_ = std::clamp(next, kMinHalfRangeNm, kMaxHalfRangeNm);

    ev->accept();
    update();
}

} // namespace asdex
