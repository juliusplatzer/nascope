#include "scope.h"

#include "cursors.h"
#include "maths.h"

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
        map_.render(p, nmToScreen(centerNm_, halfRangeNm_, size()), mode_);
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
        // Restore the ASDE-X scope cursor, not Qt's default arrow.
        if (const auto it = cursors_.constFind(QStringLiteral("scope_cursor"));
            it != cursors_.constEnd()) setCursor(*it);
        else                           unsetCursor();
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
    halfRangeNm_ = snapZoom(halfRangeNm_ - notches * kZoomStepNm);

    ev->accept();
    update();
}

} // namespace asdex
