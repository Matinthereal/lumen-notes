#include "inkcanvas.h"
#include "ink/shapes.h"
#include <QHoverEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QTimer>
#include <QTouchEvent>
#include <QWheelEvent>
#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QImageReader>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QThreadPool>
#include <QPainter>
#include <QHash>
#include <QPointer>
#include <QPainterPath>
#include <QStandardPaths>
#include <QDir>
#include "storage/paths.h"
#include <QVariantList>
#include <algorithm>
#include <cmath>

InkCanvas::InkCanvas(QQuickItem *parent) : QQuickItem(parent), m_undo(&m_doc)
{
    setFlag(ItemHasContents, true);
    setAcceptTouchEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setCursor(Qt::BlankCursor);     // the page draws its own tip (dot / eraser ring); no arrow over the paper
    m_spacingSettle.setSingleShot(true);
    m_spacingSettle.setInterval(350);
    connect(&m_spacingSettle, &QTimer::timeout, this, [this] {
        m_builtSpacing = smoothingSpacing();
        markAllChunksDirty();
        update();
    });
    m_clock.start();

    connect(&m_doc, &InkDocument::strokeAdded, this, &InkCanvas::onStrokeAdded);
    connect(&m_doc, &InkDocument::strokeRemoved, this, &InkCanvas::onStrokeRemoved);
    connect(&m_doc, &InkDocument::strokesChanged, this, &InkCanvas::onStrokesChanged);
    connect(&m_doc, &InkDocument::cleared, this, &InkCanvas::onCleared);
    connect(&m_undo, &UndoStack::changed, this, &InkCanvas::undoChanged);

    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(1000);
    connect(m_statsTimer, &QTimer::timeout, this, [this] {
        QVector<double> lat = m_latencies;
        std::sort(lat.begin(), lat.end());
        const auto pct = [&](double q) { return lat.isEmpty() ? 0.0 : lat[std::min(int(lat.size() - 1), int(q * lat.size()))]; };
        m_stats = QStringLiteral("%1 samples/s · %2 fps · event→swap p50 %3 ms p95 %4 ms · %5 strokes / %6 chunks · rebuild %7 ms")
                      .arg(m_samplesThisSecond).arg(m_framesThisSecond)
                      .arg(pct(0.5), 0, 'f', 1).arg(pct(0.95), 0, 'f', 1)
                      .arg(m_doc.count()).arg(m_chunks.size()).arg(m_lastRebuildMs, 0, 'f', 2);
        m_samplesThisSecond = 0;
        m_framesThisSecond = 0;
        emit statsChanged();
    });
    m_statsTimer->start();

    // Shape snap: fires while the tip rests still at the end of a stroke.
    m_holdTimer = new QTimer(this);
    m_holdTimer->setInterval(60);
    connect(m_holdTimer, &QTimer::timeout, this, &InkCanvas::checkHoldStill);
}

InkCanvas::~InkCanvas() = default;

QString InkCanvas::penStyle() const
{
    switch (m_penStyle) {
    case PenStyle::Classic: return QStringLiteral("classic");
    case PenStyle::Fountain: return QStringLiteral("fountain");
    case PenStyle::Ballpoint: return QStringLiteral("ballpoint");
    case PenStyle::Brush: return QStringLiteral("brush");
    }
    return {};
}

void InkCanvas::setPenStyle(const QString &name)
{
    PenStyle ps = m_penStyle;
    if (name == "classic") ps = PenStyle::Classic;
    else if (name == "fountain") ps = PenStyle::Fountain;
    else if (name == "ballpoint") ps = PenStyle::Ballpoint;
    else if (name == "brush") ps = PenStyle::Brush;
    if (ps == m_penStyle) return;
    m_penStyle = ps;
    m_curve = PressureCurve::forStyle(ps, m_curve.ceiling);
    markAllChunksDirty();
    emit styleChanged();
    update();
}

void InkCanvas::checkHoldStill()
{
    if (m_gesture != Gesture::Ink || !m_shapeSnap || m_shapeSnapped || m_holdSinceMs < 0) return;
    if (m_clock.elapsed() - m_holdSinceMs < 450) return;
    QVector<InkPoint> shaped;
    const ShapeKind kind = recognizeShape(m_active.points, shaped);
    if (kind == ShapeKind::None) { m_holdSinceMs = -1; return; }
    m_rawBeforeSnap = m_active.points;
    m_active.points = shaped;
    m_shapeSnapped = true;
    switch (kind) {
    case ShapeKind::Line: m_lastShape = QStringLiteral("line"); break;
    case ShapeKind::Arrow: m_lastShape = QStringLiteral("arrow"); break;
    case ShapeKind::Ellipse: m_lastShape = QStringLiteral("ellipse"); break;
    case ShapeKind::Rectangle: m_lastShape = QStringLiteral("rectangle"); break;
    case ShapeKind::Triangle: m_lastShape = QStringLiteral("triangle"); break;
    default: break;
    }
    emit statsChanged();
    update();
}

// ---------------------------------------------------------------- properties

QString InkCanvas::tool() const
{
    switch (m_tool) {
    case Tool::Pen: return QStringLiteral("pen");
    case Tool::Highlighter: return QStringLiteral("highlighter");
    case Tool::Eraser: return QStringLiteral("eraser");
    case Tool::Lasso: return QStringLiteral("lasso");
    case Tool::Text: return QStringLiteral("text");
    case Tool::TextBlock: return QStringLiteral("textblock");
    case Tool::Shape: return QStringLiteral("shape");
    }
    return {};
}

void InkCanvas::setTool(const QString &t)
{
    Tool nt = m_tool;
    if (t == "pen") nt = Tool::Pen;
    else if (t == "highlighter") nt = Tool::Highlighter;
    else if (t == "eraser") nt = Tool::Eraser;
    else if (t == "lasso") nt = Tool::Lasso;
    else if (t == "text") nt = Tool::Text;
    else if (t == "textblock") nt = Tool::TextBlock;
    else if (t == "shape") nt = Tool::Shape;
    if (nt == m_tool) return;
    if (m_gesture == Gesture::Ink) { endStroke(); m_gesture = Gesture::None; setInking(false); }
    m_tool = nt;
    emit toolChanged();
    update();
}

void InkCanvas::setZoom(qreal z)
{
    z = std::clamp(z, 0.1, 16.0);
    if (qFuzzyCompare(z, m_zoom)) return;
    m_fitted = false;
    m_zoom = z;
    m_viewDirty = true;
    emit viewChanged();
    update();
}

void InkCanvas::setPan(QPointF p)
{
    if (p == m_pan) return;
    m_fitted = false;
    m_pan = p;
    m_viewDirty = true;
    emit viewChanged();
    update();
}

void InkCanvas::setInfinite(bool v)
{
    if (v == m_infinite) return;
    m_infinite = v;
    m_viewDirty = true;
    emit viewChanged();
    update();
}

void InkCanvas::setPageSize(QSizeF s)
{
    if (s == m_pageSize) return;
    m_pageSize = s;
    m_viewDirty = true;
    emit viewChanged();
    update();
}

QString InkCanvas::pageStyle() const
{
    switch (m_pageStyle) {
    case PageStyle::Plain: return QStringLiteral("plain");
    case PageStyle::Lined: return QStringLiteral("lined");
    case PageStyle::Dotted: return QStringLiteral("dotted");
    case PageStyle::Grid: return QStringLiteral("grid");
    case PageStyle::Cornell: return QStringLiteral("cornell");
    case PageStyle::Graph: return QStringLiteral("graph");
    case PageStyle::Isometric: return QStringLiteral("isometric");
    case PageStyle::Music: return QStringLiteral("music");
    }
    return {};
}

void InkCanvas::setPageStyle(const QString &style)
{
    PageStyle ps = PageStyle::Dotted;
    if (style == "plain") ps = PageStyle::Plain; else if (style == "lined") ps = PageStyle::Lined;
    else if (style == "grid") ps = PageStyle::Grid; else if (style == "cornell") ps = PageStyle::Cornell;
    else if (style == "graph") ps = PageStyle::Graph; else if (style == "isometric") ps = PageStyle::Isometric;
    else if (style == "music") ps = PageStyle::Music;
    if (ps == m_pageStyle) return;
    m_pageStyle = ps;
    m_dotsRegion = QRectF();   // force the guides to rebuild
    m_viewDirty = true;
    emit viewChanged();
    update();
}

void InkCanvas::resetHistory()
{
    if (m_gesture != Gesture::None) pointerRelease(m_hoverLocal, 0);
    m_words.clear(); m_textSelA = m_textSelB = -1; m_textSelDirty = true;
    m_undo.clear();
    m_selection.clear(); m_selectionDirty = true;
    m_hidden.clear(); m_eraseIds.clear(); m_pixelWork.clear(); m_previewDirty = true;
    // The clipboard deliberately survives a page change: copying working from one page onto
    // another is the whole point of having one (OneNote does the same).
    emit selectionChanged();
    emit undoChanged();
    update();
}

void InkCanvas::setPressureCeiling(qreal c)
{
    c = std::clamp(c, 0.2, 1.0);
    if (qFuzzyCompare(c, m_curve.ceiling)) return;
    m_curve.ceiling = float(c);
    markAllChunksDirty();
    emit styleChanged();
    update();
}

void InkCanvas::zoomAt(qreal factor, QPointF screenCenter)
{
    const qreal nz = std::clamp(m_zoom * factor, 0.1, 16.0);
    if (qFuzzyCompare(nz, m_zoom)) return;
    m_fitted = false;
    m_pan = screenCenter - (screenCenter - m_pan) * (nz / m_zoom);
    m_zoom = nz;
    m_viewDirty = true;
    emit viewChanged();
    update();
}

void InkCanvas::fitPage()
{
    if (width() <= 0 || height() <= 0) { m_fitPending = true; return; }   // laid out later: geometryChange() fits then
    m_fitPending = false;
    m_fitted = true;
    const qreal top = std::max(8.0, m_topInset), bottom = std::max(8.0, m_bottomInset);
    if (m_infinite) { m_zoom = 1; m_pan = QPointF(40, top); }
    else {
        m_zoom = std::clamp(std::min((width() - 40) / m_pageSize.width(),
                                     (height() - top - bottom) / m_pageSize.height()), 0.1, 16.0);
        m_pan = QPointF((width() - m_pageSize.width() * m_zoom) / 2, top);
    }
    m_viewDirty = true;
    emit viewChanged();
    update();
}

void InkCanvas::fitWidth()
{
    if (width() <= 0) return;
    const qreal pw = m_infinite ? 1000.0 : m_pageSize.width();
    m_fitted = false;
    m_zoom = std::clamp((width() - 40) / pw, 0.1, 16.0);
    m_pan = QPointF((width() - pw * m_zoom) / 2, m_infinite ? m_topInset : std::min(m_topInset, m_pan.y()));
    m_viewDirty = true;
    emit viewChanged();
    update();
}

float InkCanvas::smoothingSpacing() const
{
    // Finer resampling when zoomed in so ribbons stay smooth; coarser when zoomed out.
    return float(std::clamp(2.0 / m_zoom, 0.5, 4.0));
}

void InkCanvas::setInking(bool v)
{
    if (m_inking == v) return;
    m_inking = v;
    emit inkingChanged();
}

// ---------------------------------------------------------------- tablet input

void InkCanvas::tabletProximity(bool entering, const TabletSample &)
{
    if (entering) {
        m_penNear = true;
        m_touchPts.clear(); // any touch in flight is a palm from now on
    } else {
        m_penNear = false;
        m_penLeftAt = m_clock.elapsed();
        if (m_gesture != Gesture::None) pointerRelease(m_hoverLocal, 0);
        m_buttonHeld = false;
    }
    emit inkingChanged();
    update();
}

bool InkCanvas::tabletSample(const TabletSample &s)
{
    if (!isVisible() || !isEnabled()) { m_penNear = false; return false; }   // no page open: let Qt synthesize a mouse press for the chrome
    const QPointF local = mapFromScene(s.windowPos);
    m_hoverLocal = local;
    m_hoverValid = true;
    m_lastSampleArrivalUs = m_clock.nsecsElapsed() / 1000;
    ++m_samplesThisSecond;
    m_penNear = true;

    m_straightEdge = s.modifiers.testFlag(Qt::ShiftModifier);
    const bool side = s.buttons & (Qt::MiddleButton | Qt::RightButton);
    if (side && !m_buttonHeld) {
        const qint64 now = m_clock.elapsed();
        if (m_lastButtonPressMs >= 0 && now - m_lastButtonPressMs < 350 && !m_buttonUsedForErase) {
            undo();                    // double-press (Q14)
            m_lastButtonPressMs = -1;
        } else {
            m_lastButtonPressMs = now;
        }
        m_buttonHeld = true;
        m_buttonUsedForErase = false;
        if (m_gesture == Gesture::Ink) { endStroke(); m_gesture = Gesture::None; setInking(false); }
    } else if (!side && m_buttonHeld) {
        m_buttonHeld = false;
        if (m_gesture == Gesture::Erase) { finishErase(); m_gesture = Gesture::None; setInking(false); }
    }

    bool inside = contains(local);
    if (inside && m_gesture == Gesture::None && pointerBelongsToChrome(s.windowPos)) {
        m_hoverValid = false;
        update();
        return false;
    }
    // Tools the canvas does not itself draw — shapes are placed by the layer above it — must not be
    // eaten here. Consuming the event was why the pen could not draw a shape while a finger could:
    // touch reaches QML through a different path, the pen only reaches it if we decline.
    if (effectiveTool() == Tool::Shape && m_gesture == Gesture::None) {
        m_hoverValid = false;
        update();
        return false;
    }

    const bool tipDown = s.buttons & Qt::LeftButton;
    switch (s.kind) {
    case TabletSample::Kind::Press:
        if (!inside) return false;
        pointerPress(local, float(s.pressure), float(s.xTilt), float(s.yTilt), s.timestampMs, s.eraser);
        return true;
    case TabletSample::Kind::Move:
        pointerMove(local, float(s.pressure), float(s.xTilt), float(s.yTilt), s.timestampMs, tipDown, s.eraser);
        return inside || m_gesture != Gesture::None;
    case TabletSample::Kind::Release:
        pointerRelease(local, s.timestampMs);
        return true;
    default:
        return false;
    }
}

// ---------------------------------------------------------------- gestures

void InkCanvas::pointerPress(QPointF local, float pressure, float tiltX, float tiltY, quint64 tMs, bool eraserNow)
{
    emit pagePressed();          // whatever object was selected is put down by touching the page
    const QPointF page = toPage(local);
    // The page has edges: writing on the desk beside it makes ink that no export can show.
    if (!m_infinite && (effectiveTool() == Tool::Pen || effectiveTool() == Tool::Highlighter)) {
        const QRectF sheet(QPointF(0, 0), m_pageSize);
        if (!sheet.adjusted(-2, -2, 2, 2).contains(page)) { m_gesture = Gesture::None; return; }
    }
    Tool t = effectiveTool();
    if (eraserNow) t = Tool::Eraser;

    if (m_seekMode) {
        for (quint64 id : m_doc.hitCircle(page, float(8.0 / m_zoom))) {
            const Stroke *st = m_doc.stroke(id);
            if (!st || st->recordingId == 0 || st->points.isEmpty()) continue;
            // the point nearest the tap gives the moment it was written
            int best = 0; float bd = 1e30f;
            for (int i = 0; i < st->points.size(); ++i) { const float dx = st->points[i].x - float(page.x()), dy = st->points[i].y - float(page.y()); if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; best = i; } }
            emit strokeTapped(qint64(st->recordingId), qint64(st->startMs) + st->points[best].tMs);
            return;
        }
        return;
    }
    if (hasSelection() && t != Tool::Eraser) {
        const int h = handleAt(local);
        if (h >= 0) { m_gesture = Gesture::ScaleSelection; m_activeHandle = h; m_dragStartPage = page; setInking(true); update(); return; }
        if (m_selectionXf.mapRect(m_selectionBounds).contains(page)) { m_gesture = Gesture::MoveSelection; m_dragStartPage = page; setInking(true); update(); return; }
        selectNone();
    }
    switch (t) {
    case Tool::Pen:
    case Tool::Highlighter:
        beginStroke(page, pressure, tiltX, tiltY, tMs);
        m_gesture = Gesture::Ink;
        break;
    case Tool::Eraser:
        m_gesture = Gesture::Erase;
        m_buttonUsedForErase = m_buttonHeld;
        m_eraseLastValid = false;
        eraseAt(page);
        break;
    case Tool::Lasso:
        m_lasso.clear();
        m_lasso << page;
        m_gesture = Gesture::Lasso;
        break;
    case Tool::TextBlock:
        m_pressPage = page; m_pressMoved = false;
        m_gesture = Gesture::None;
        return;
    case Tool::Shape:
        m_gesture = Gesture::None;      // the shape layer above owns this drag
        return;
    case Tool::Text: {
        const int w = wordAt(page);
        m_textSelA = m_textSelB = w;
        m_textSelDirty = true;
        m_gesture = Gesture::TextSelect;
        emit selectionChanged();
        break;
    }
    }
    setInking(true);
    update();
}

void InkCanvas::pointerMove(QPointF local, float pressure, float tiltX, float tiltY, quint64 tMs, bool tipDown, bool)
{
    if (!tipDown || m_gesture == Gesture::None) {
        update(); // the hover cursor (ring or dot) follows the tip
        return;
    }
    const QPointF page = toPage(local);
    switch (m_gesture) {
    case Gesture::Ink: extendStroke(page, pressure, tiltX, tiltY, tMs); break;
    case Gesture::Erase: eraseAt(page); break;
    case Gesture::Lasso: m_lasso << page; break;
    case Gesture::MoveSelection:
        m_selectionXf = QTransform::fromTranslate(page.x() - m_dragStartPage.x(), page.y() - m_dragStartPage.y());
        m_selectionDirty = true;
        break;
    case Gesture::ScaleSelection: {
        const QRectF b = m_selectionBounds;
        const QPointF corners[4] = {b.topLeft(), b.topRight(), b.bottomRight(), b.bottomLeft()};
        const QPointF anchor = corners[(m_activeHandle + 2) % 4];
        const QPointF start = corners[m_activeHandle];
        const qreal sx = std::abs(start.x() - anchor.x()) > 1e-6 ? (page.x() - anchor.x()) / (start.x() - anchor.x()) : 1;
        const qreal sy = std::abs(start.y() - anchor.y()) > 1e-6 ? (page.y() - anchor.y()) / (start.y() - anchor.y()) : 1;
        const qreal k = std::clamp(std::max(sx, sy), 0.05, 20.0); // uniform: ink keeps its shape
        m_selectionXf = QTransform::fromTranslate(-anchor.x(), -anchor.y()) * QTransform::fromScale(k, k) * QTransform::fromTranslate(anchor.x(), anchor.y());
        m_selectionDirty = true;
        break;
    }
    case Gesture::TextSelect: textSelectTo(page); break;
    case Gesture::None: break;
    }
    update();
}

void InkCanvas::pointerRelease(QPointF local, quint64)
{
    if (effectiveTool() == Tool::TextBlock && m_gesture == Gesture::None) {
        const QPointF page = toPage(local);
        if (std::hypot(page.x() - m_pressPage.x(), page.y() - m_pressPage.y()) < 8 / m_zoom) emit tapped(page);
        setInking(false);
        return;
    }
    switch (m_gesture) {
    case Gesture::Ink: endStroke(); break;
    case Gesture::Erase: finishErase(); break;
    case Gesture::Lasso: finishLasso(); break;
    case Gesture::MoveSelection:
    case Gesture::ScaleSelection: commitSelectionTransform(); break;
    case Gesture::TextSelect: emit selectionChanged(); break;
    case Gesture::None: break;
    }
    m_gesture = Gesture::None;
    m_activeHandle = -1;
    setInking(false);
    update();
}

void InkCanvas::beginStroke(QPointF page, float pressure, float tiltX, float tiltY, quint64 tMs)
{
    m_active = Stroke{};
    // Input conditioning: 1€ on x/y (strength from the smoothing setting), EMA on pressure.
    const double minCutoff = m_smoothing <= 0 ? 1e6 : 6.0 - 4.5 * std::clamp(m_smoothing, 0.0, 1.0); // 6 Hz … 1.5 Hz
    m_fx = OneEuroFilter(minCutoff, 0.03); m_fy = OneEuroFilter(minCutoff, 0.03);
    m_fx.filter(page.x(), tMs / 1000.0); m_fy.filter(page.y(), tMs / 1000.0);
    m_pressureEma = pressure;
    m_shapeSnapped = false;
    m_holdAnchor = page;
    m_holdSinceMs = m_clock.elapsed();
    m_holdTimer->start();
    m_active.tool = m_tool == Tool::Highlighter ? InkTool::Highlighter : InkTool::Pen;
    if (m_active.tool == InkTool::Highlighter) {
        QColor c = m_highlighterColor; c.setAlphaF(m_highlighterOpacity);
        m_active.color = c.rgba();
        m_active.width = float(m_highlighterWidth);
    } else {
        m_active.color = m_penColor.rgba();
        m_active.width = float(m_penWidth);
    }
    m_strokeT0 = tMs;
    if (m_recordingId) {
        m_active.recordingId = quint64(m_recordingId);
        m_active.startMs = quint32(std::max<qint64>(0, QDateTime::currentMSecsSinceEpoch() - m_recordingEpochMs));
    }
    m_active.points.append({float(page.x()), float(page.y()), pressure, tiltX, tiltY, 0});
}

void InkCanvas::extendStroke(QPointF page, float pressure, float tiltX, float tiltY, quint64 tMs)
{
    if (m_active.points.isEmpty()) { beginStroke(page, pressure, tiltX, tiltY, tMs); return; }
    // Hold-still bookkeeping happens on the raw position, before filtering.
    const float hx = float(page.x() - m_holdAnchor.x()), hy = float(page.y() - m_holdAnchor.y());
    if (hx * hx + hy * hy > 9.f) {
        m_holdAnchor = page;
        m_holdSinceMs = m_clock.elapsed();
        if (m_shapeSnapped) { m_active.points = m_rawBeforeSnap; m_shapeSnapped = false; m_lastShape.clear(); } // kept drawing: back to freehand
    }
    // Shift is the ruler: snap to the nearest eighth of a turn from the stroke's first point.
    // OneNote's cheap straight edge, and it costs nothing when Shift is not held.
    if (m_straightEdge && m_active.points.size() >= 1) {
        const InkPoint &first = m_active.points.first();
        const double dx = page.x() - first.x, dy = page.y() - first.y;
        const double len = std::hypot(dx, dy);
        if (len > 1e-3) {
            const double step = M_PI / 4.0;
            const double angle = std::round(std::atan2(dy, dx) / step) * step;
            page = QPointF(first.x + std::cos(angle) * len, first.y + std::sin(angle) * len);
        }
        // A ruled stroke is two points, not a trail of them, or the snap fights the samples.
        if (m_active.points.size() > 1) m_active.points.remove(1, m_active.points.size() - 1);
    }
    // Raw samples by default (smoothing 0): the digitizer at 330 Hz is already clean and the
    // maker approved that feel; the 1€ filter is opt-in.
    const bool filtered = m_smoothing > 0;
    const float fx = filtered ? float(m_fx.filter(page.x(), tMs / 1000.0)) : float(page.x());
    const float fy = filtered ? float(m_fy.filter(page.y(), tMs / 1000.0)) : float(page.y());
    const float pr = filtered ? (m_pressureEma += (pressure - m_pressureEma) * 0.5f) : pressure;
    const InkPoint &last = m_active.points.last();
    const float dx = fx - last.x, dy = fy - last.y;
    if (dx * dx + dy * dy < 0.01f && std::abs(pr - last.pressure) < 0.01f) return; // duplicate sample
    m_active.points.append({fx, fy, pr, tiltX, tiltY, quint32(tMs - m_strokeT0)});
}

void InkCanvas::endStroke()
{
    m_holdTimer->stop();
    m_holdSinceMs = -1;
    if (m_active.points.isEmpty()) return;
    if (m_active.tool == InkTool::Highlighter && !m_words.isEmpty() && snapHighlightToWords()) { m_active = Stroke{}; emit documentChanged(); return; }
    if (m_scratchOut && m_active.tool == InkTool::Pen && !m_shapeSnapped && scratchOutErases()) { m_active = Stroke{}; emit documentChanged(); return; }
    m_undo.push(std::make_unique<AddStrokeCommand>(m_active));
    m_active = Stroke{};
    emit documentChanged();
}

// Crossing something out is how you correct maths, so a deliberate scribble rubs out what it
// covers instead of leaving a scribble on the page. It only fires when the scribble actually
// crosses existing ink, so a scribbled drawing on blank paper stays.
bool InkCanvas::scratchOutErases()
{
    if (!looksLikeScratchOut(m_active.points)) return false;
    const float r = std::max(4.f, m_active.width * 1.5f);

    // How many of the scribble's own samples sit on each stroke. Counting crossings rather than
    // containment matters: a ruled line is two points, so "most of its points are inside my box"
    // is never true, but a five-sweep scribble crosses it five times.
    QHash<quint64, int> crossings;
    for (const InkPoint &p : m_active.points)
        for (quint64 id : m_doc.hitCircle(QPointF(p.x, p.y), r)) ++crossings[id];
    if (crossings.isEmpty()) return false;

    const QRectF band = m_active.bounds.adjusted(-r, -r, r, r);
    QVector<quint64> doomed;
    for (auto it = crossings.cbegin(); it != crossings.cend(); ++it) {
        const Stroke *s = m_doc.stroke(it.key());
        if (!s || s->points.isEmpty()) continue;
        int inside = 0;
        for (const InkPoint &p : s->points) if (band.contains(p.x, p.y)) ++inside;
        const bool mostlyCovered = float(inside) / float(s->points.size()) >= 0.6f;
        const bool struckThrough = it.value() >= 3;      // the scribble went over it again and again
        if (mostlyCovered || struckThrough) doomed.append(it.key());
    }
    if (doomed.isEmpty()) return false;
    std::sort(doomed.begin(), doomed.end());
    m_undo.push(std::make_unique<RemoveStrokesCommand>(doomed));
    return true;
}

// A fast wipe must not hop over a thin line between two samples: sweep the circle along the
// segment from the previous eraser position (found by tst_canvas before any hand did).
void InkCanvas::eraseAt(QPointF page)
{
    const float r = float(m_eraserRadius / m_zoom);
    if (m_eraseLastValid) {
        const QPointF d = page - m_eraseLast;
        const double len = std::hypot(d.x(), d.y());
        const int steps = std::clamp(int(std::ceil(len / std::max(double(r) * 0.75, 1.0))), 1, 48);   // a proximity jump across the page must not scan 500 times
        for (int i = 1; i <= steps; ++i) eraseCircle(m_eraseLast + d * (double(i) / steps), r);
    } else {
        eraseCircle(page, r);
    }
    m_eraseLast = page;
    m_eraseLastValid = true;
}

void InkCanvas::eraseCircle(QPointF page, float r)
{
    if (!m_pixelEraser) {
        for (quint64 id : m_doc.hitCircle(page, r)) {
            if (m_hidden.contains(id)) continue;
            m_hidden.insert(id);
            m_eraseIds.append(id);
            if (m_chunkOf.contains(id)) m_dirtyChunks.insert(m_chunkOf.value(id));
        }
        return;
    }
    // Pixel eraser: originals are hidden, their pieces are re-split as the eraser moves.
    const QRectF probe(page.x() - r, page.y() - r, 2 * r, 2 * r);
    for (quint64 id : m_doc.hitCircle(page, r)) {
        if (m_hidden.contains(id)) continue;
        const Stroke *s = m_doc.stroke(id);
        if (!s) continue;
        m_pixelWork.insert(id, {*s});
        m_hidden.insert(id);
        if (m_chunkOf.contains(id)) m_dirtyChunks.insert(m_chunkOf.value(id));
    }
    for (auto it = m_pixelWork.begin(); it != m_pixelWork.end(); ++it) {
        QVector<Stroke> next;
        for (const Stroke &piece : it.value()) {
            if (piece.bounds.intersects(probe)) next += InkDocument::splitByCircle(piece, page, r);
            else next.append(piece);
        }
        it.value() = next;
    }
    m_previewDirty = true;
}

void InkCanvas::finishErase()
{
    m_eraseLastValid = false;
    if (!m_pixelEraser) {
        if (m_eraseIds.isEmpty()) return;
        for (quint64 id : m_eraseIds) m_hidden.remove(id);
        m_undo.push(std::make_unique<RemoveStrokesCommand>(m_eraseIds));
        m_eraseIds.clear();
    } else {
        if (m_pixelWork.isEmpty()) return;
        QVector<quint64> ids; QVector<Stroke> pieces;
        for (auto it = m_pixelWork.cbegin(); it != m_pixelWork.cend(); ++it) { ids.append(it.key()); pieces += it.value(); }
        for (quint64 id : ids) m_hidden.remove(id);
        for (Stroke &p : pieces) p.id = 0;
        m_pixelWork.clear();
        m_previewDirty = true;
        m_undo.push(std::make_unique<ReplaceStrokesCommand>(ids, pieces));
    }
    emit documentChanged();
}

void InkCanvas::finishLasso()
{
    if (m_lasso.size() >= 3) setSelection(m_doc.insidePolygon(m_lasso));
    m_lasso.clear();
}

void InkCanvas::setSelection(QVector<quint64> ids)
{
    for (quint64 id : m_selection) if (m_chunkOf.contains(id)) m_dirtyChunks.insert(m_chunkOf.value(id));
    m_selection = std::move(ids);
    for (quint64 id : m_selection) if (m_chunkOf.contains(id)) m_dirtyChunks.insert(m_chunkOf.value(id));
    m_selectionBounds = m_doc.boundsOf(m_selection);
    m_selectionXf = QTransform();
    m_selectionDirty = true;
    emit selectionChanged();
    update();
}

int InkCanvas::handleAt(QPointF local) const
{
    if (!hasSelection()) return -1;
    const QRectF b = m_selectionXf.mapRect(m_selectionBounds);
    const QPointF corners[4] = {b.topLeft(), b.topRight(), b.bottomRight(), b.bottomLeft()};
    for (int i = 0; i < 4; ++i) {
        const QPointF s = toScreen(corners[i]);
        if (std::abs(s.x() - local.x()) <= 14 && std::abs(s.y() - local.y()) <= 14) return i;
    }
    return -1;
}

void InkCanvas::commitSelectionTransform()
{
    if (m_selectionXf.isIdentity() || m_selection.isEmpty()) { m_selectionXf = QTransform(); return; }
    const QTransform t = m_selectionXf;
    m_selectionXf = QTransform();
    m_undo.push(std::make_unique<TransformStrokesCommand>(m_selection, t));
    m_selectionBounds = t.mapRect(m_selectionBounds);
    m_selectionDirty = true;
    emit documentChanged();
}

// ---------------------------------------------------------------- invokables

void InkCanvas::undo() { if (m_gesture != Gesture::None) return; selectNone(); m_undo.undo(); emit documentChanged(); update(); }
void InkCanvas::redo() { if (m_gesture != Gesture::None) return; selectNone(); m_undo.redo(); emit documentChanged(); update(); }

void InkCanvas::clearAll()
{
    if (m_doc.count() == 0) return;
    selectNone();
    QVector<quint64> ids; ids.reserve(m_doc.count());
    for (const Stroke &s : m_doc.strokes()) ids.append(s.id);
    m_undo.push(std::make_unique<RemoveStrokesCommand>(ids));
    emit documentChanged();
    update();
}

void InkCanvas::deleteSelection()
{
    if (!hasSelection()) return;
    const QVector<quint64> ids = m_selection;
    m_selection.clear();
    emit selectionChanged();
    m_undo.push(std::make_unique<RemoveStrokesCommand>(ids));
    emit documentChanged();
    update();
}

// Absolute, not relative: the thickness buttons used to *multiply*, so tapping "4" twice made ink
// eight times too thick and there was no way back to a known weight (research #3).
void InkCanvas::setSelectionWidth(qreal w)
{
    restyleSelection(QColor(), 0, std::clamp(w, 0.4, 40.0));
}

// The one width shared by the whole selection, or 0 if they differ — so the bar can show which
// weight is actually on the ink instead of nothing.
qreal InkCanvas::selectionWidth() const
{
    qreal w = 0;
    for (quint64 id : m_selection) {
        const Stroke *s = m_doc.stroke(id);
        if (!s) continue;
        if (w == 0) w = s->width;
        else if (!qFuzzyCompare(qreal(s->width), w)) return 0;
    }
    return w;
}

QString InkCanvas::selectionColour() const
{
    QString hex;
    for (quint64 id : m_selection) {
        const Stroke *s = m_doc.stroke(id);
        if (!s) continue;
        QColor c = QColor::fromRgba(s->color); c.setAlpha(255);
        const QString h = c.name(QColor::HexRgb).toUpper();
        if (hex.isEmpty()) hex = h;
        else if (hex != h) return QString();
    }
    return hex;
}

void InkCanvas::restyleSelection(const QColor &colour, qreal widthScale, qreal widthAbs)
{
    if (!hasSelection()) return;
    QVector<Stroke> after;
    after.reserve(m_selection.size());
    for (quint64 id : m_selection) {
        const Stroke *s = m_doc.stroke(id);
        if (!s) continue;
        Stroke copy = *s;
        if (colour.isValid()) {
            QColor c = colour;
            if (copy.tool == InkTool::Highlighter) c.setAlphaF(QColor::fromRgba(copy.color).alphaF());
            copy.color = c.rgba();
        }
        if (widthAbs > 0) copy.width = float(widthAbs);
        else if (widthScale > 0 && !qFuzzyCompare(widthScale, 1.0)) copy.width = float(std::clamp(copy.width * widthScale, 0.4, 40.0));
        after.append(copy);
    }
    if (after.isEmpty()) return;
    const QVector<quint64> ids = m_selection;
    m_undo.push(std::make_unique<ReplaceStrokesCommand>(ids, after));
    // The replacement strokes are new ids; select those so a second restyle works on the same ink.
    QVector<quint64> restyled;
    const int n = m_doc.count();
    for (int i = std::max(0, n - int(after.size())); i < n; ++i) restyled.append(m_doc.strokes()[i].id);
    setSelection(restyled);
    emit documentChanged();
    update();
}

void InkCanvas::copySelection()
{
    m_clipboard.clear();
    for (quint64 id : m_selection)
        if (const Stroke *s = m_doc.stroke(id)) { Stroke c = *s; c.id = 0; m_clipboard.append(c); }
}

void InkCanvas::cutSelection() { copySelection(); deleteSelection(); }

void InkCanvas::paste()
{
    if (m_clipboard.isEmpty()) return;
    QVector<Stroke> add = m_clipboard;
    QVector<quint64> ids;
    for (Stroke &s : add) {
        for (InkPoint &p : s.points) { p.x += 20; p.y += 20; }
        s.updateBounds();
        s.id = m_doc.nextId();
        ids.append(s.id);
    }
    m_clipboard = add; // the next paste lands another 20 units further
    for (Stroke &s : m_clipboard) s.id = 0;
    m_undo.push(std::make_unique<ReplaceStrokesCommand>(QVector<quint64>{}, add));
    setSelection(ids);
    emit documentChanged();
}

void InkCanvas::selectAll()
{
    QVector<quint64> ids;
    for (const Stroke &s : m_doc.strokes()) ids.append(s.id);
    setSelection(ids);
}

void InkCanvas::selectNone() { if (hasSelection()) setSelection({}); }

void InkCanvas::addBenchmarkStrokes(int count)
{
    auto *rng = QRandomGenerator::global();
    const QRgb palette[] = {0xff18212b, 0xff1f3a93, 0xffab1d19, 0xff126044, 0xffb04a12, 0xff5b2a86, 0xff555555, 0xff0f7c8a};
    for (int i = 0; i < count; ++i) {
        Stroke s;
        s.color = palette[rng->bounded(8)];
        s.width = 1.5f;
        float x = float(rng->bounded(m_pageSize.width() - 60)) + 10, y = float(rng->bounded(m_pageSize.height() - 40)) + 10;
        const int n = 12 + rng->bounded(30);
        for (int k = 0; k < n; ++k) {
            x += float(rng->bounded(5.0)) - 1.5f; y += float(rng->bounded(6.0)) - 3.f;
            s.points.append({x, y, 0.3f + 0.5f * float(rng->bounded(1.0)), 0, 0, quint32(k * 3)});
        }
        m_doc.addStroke(s);
    }
    m_undo.clear();
    emit documentChanged();
    update();
}

// ---------------------------------------------------------------- touch / mouse / wheel

void InkCanvas::touchEvent(QTouchEvent *event)
{
    const qint64 now = m_clock.elapsed();
    const bool palm = m_penNear || (m_penLeftAt >= 0 && now - m_penLeftAt < m_palmRejectMs) || m_gesture != Gesture::None;
    if (palm) {
        ++m_touchIgnored;
        m_touchPts.clear();
        event->accept();
        emit statsChanged();
        return;
    }
    // A finger that lands on chrome (a shape's frame, a handle, a picture) belongs to that chrome,
    // exactly as the pen and the mouse do.
    if (event->points().size() == 1 && event->points().first().state() == QEventPoint::Pressed
        && pointerBelongsToChrome(event->points().first().scenePosition())) {
        event->ignore();
        return;
    }
    const int before = m_touchPts.size();
    for (const QEventPoint &p : event->points()) {
        if (p.state() == QEventPoint::Released) m_touchPts.remove(p.id());
        else m_touchPts[p.id()] = mapFromScene(p.scenePosition());
    }
    const int n = m_touchPts.size();
    // Two-finger double-tap = undo (a quick, still, two-finger touch twice within 400 ms).
    if (event->type() == QEvent::TouchBegin && n == 2) { m_twoFingerDownMs = now; m_twoFingerMoved = false; QPointF c; for (const QPointF &p : m_touchPts) c += p; m_twoFingerDownAt = c / 2; }
    if (n == 2 && m_twoFingerDownMs >= 0 && event->type() == QEvent::TouchUpdate) { QPointF c; for (const QPointF &p : m_touchPts) c += p; if ((c / 2 - m_twoFingerDownAt).manhattanLength() > 12) m_twoFingerMoved = true; }
    if (event->type() == QEvent::TouchEnd && m_twoFingerDownMs >= 0) {
        const bool quickStill = !m_twoFingerMoved && now - m_twoFingerDownMs < 250;
        if (quickStill && m_lastTwoFingerTapMs >= 0 && now - m_lastTwoFingerTapMs < 400) { undo(); m_lastTwoFingerTapMs = -1; }
        else m_lastTwoFingerTapMs = quickStill ? now : -1;
        m_twoFingerDownMs = -1;
    }
    auto centroidAndDist = [&](QPointF &c, qreal &d) {
        c = QPointF(); for (const QPointF &p : m_touchPts) c += p; if (n) c /= n;
        d = 0;
        if (n >= 2) { auto it = m_touchPts.cbegin(); const QPointF a = it.value(); ++it; d = std::hypot(it.value().x() - a.x(), it.value().y() - a.y()); }
    };
    if (n == 0) { event->accept(); return; }
    if (n != before || event->type() == QEvent::TouchBegin) {
        centroidAndDist(m_touchStartCentroid, m_touchStartDist);
        m_touchStartPan = m_pan;
        m_touchStartZoom = m_zoom;
        event->accept();
        return;
    }
    QPointF c; qreal d;
    centroidAndDist(c, d);
    qreal nz = m_touchStartZoom;
    if (n >= 2 && m_touchStartDist > 10) nz = std::clamp(m_touchStartZoom * d / m_touchStartDist, 0.1, 16.0);
    const QPointF pagePt = (m_touchStartCentroid - m_touchStartPan) / m_touchStartZoom;
    m_fitted = false;
    m_zoom = nz;
    m_pan = c - pagePt * nz;
    m_viewDirty = true;
    emit viewChanged();
    update();
    event->accept();
}

void InkCanvas::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const qreal steps = event->angleDelta().y() / 120.0;
        zoomAt(std::pow(1.05, steps), event->position());
    } else {
        QPointF d = event->pixelDelta().isNull() ? QPointF(event->angleDelta()) / 2.0 : QPointF(event->pixelDelta());
        if (event->modifiers() & Qt::ShiftModifier) d = QPointF(d.y(), d.x());
        setPan(m_pan + d);
    }
    event->accept();
}

// Mouse drawing exists for development and for a trackpad; a pen never arrives here (D-002).
// Is UI chrome stacked over this point? Handlers on chrome take only a passive grab, so without
// this the canvas would start a lasso (or a stroke) underneath the shape you were trying to drag.
//
// This walks the whole scene rather than following QQuickItem::childAt(). childAt() only ever
// descends into the topmost child, and it cannot enter an item of zero size at all — and every
// shape, picture and text block lives inside an unsized, transformed `pageSpace` that sits under a
// full-page sibling layer. The old descent therefore stopped at TextLayer every time and reported
// "not chrome", which is why the pen could never grab an object: it lassoed straight through them
// while a finger, which reaches QML by another path entirely, worked fine (D-064).
static bool chromeUnder(const QQuickItem *item, const QPointF &scenePos, const QQuickItem *skip)
{
    if (item == skip || !item->isVisible() || item->opacity() <= 0.0) return false;
    const bool inside = item->contains(item->mapFromScene(scenePos));
    if (item->clip() && !inside) return false;     // clipped away, so nothing inside it can be hit
    const QList<QQuickItem *> kids = item->childItems();
    for (auto it = kids.crbegin(); it != kids.crend(); ++it)   // front to back
        if (chromeUnder(*it, scenePos, skip)) return true;
    if (!inside) return false;
    // The popup overlay spans the whole window for the life of the app. It is chrome only while
    // something is up on it — a popup, a modal dimmer, the keyboard — never just for existing.
    if (item->inherits("QQuickOverlay")) {
        for (const QQuickItem *kid : kids)
            if (kid->isVisible() && kid->opacity() > 0.0) return true;
        return false;
    }
    return item->objectName() == QLatin1String("chrome") || item->inherits("QQuickPopupItem");
}

bool InkCanvas::pointerBelongsToChrome(QPointF windowPos) const
{
    if (!window()) return false;
    return chromeUnder(window()->contentItem(), windowPos, this);
}

// A pointer handler above us — a shape frame, a picture, a text block — is allowed to steal the
// exclusive grab after we have taken the press. Qt then simply stops delivering to us, so without
// this the canvas sat in a gesture for ever: `inking` stayed true (which hides the toolbar, both
// page arrows and the page counter), every later touch was discarded as a palm, and the phantom
// point left in m_touchPts made the next single finger read as a pinch.
void InkCanvas::abandonGesture()
{
    if (m_gesture != Gesture::None) pointerRelease(m_hoverLocal, 0);
    m_gesture = Gesture::None;
    m_touchPts.clear();
    m_mouseDown = false;
    m_hoverValid = false;
    setInking(false);
    update();
}

void InkCanvas::touchUngrabEvent() { abandonGesture(); }
void InkCanvas::mouseUngrabEvent() { abandonGesture(); }

void InkCanvas::mousePressEvent(QMouseEvent *event)
{
    if (m_penNear) { event->ignore(); return; }
    if (pointerBelongsToChrome(mapToScene(event->position()))) { event->ignore(); return; }
    m_mouseDown = true;
    pointerPress(event->position(), 0.6f, 0, 0, quint64(event->timestamp()), false);
    event->accept();
}
void InkCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_mouseDown) return;
    pointerMove(event->position(), 0.6f, 0, 0, quint64(event->timestamp()), true, false);
}
void InkCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_mouseDown) return;
    m_mouseDown = false;
    pointerRelease(event->position(), quint64(event->timestamp()));
}
void InkCanvas::hoverMoveEvent(QHoverEvent *event)
{
    if (m_penNear) return;
    m_hoverLocal = event->position();
    m_hoverValid = true;
    if (effectiveTool() == Tool::Eraser) update();
}

void InkCanvas::geometryChange(const QRectF &n, const QRectF &o)
{
    QQuickItem::geometryChange(n, o);
    m_viewDirty = true;
    if ((m_fitPending || m_fitted) && n.width() > 0 && n.height() > 0 && n.size() != o.size()) fitPage();
    update();
}

void InkCanvas::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickItem::itemChange(change, value);
    if (change == ItemSceneChange && value.window) {
        // Render-thread signal: only touch atomics here; the GUI thread reads them next frame.
        connect(value.window, &QQuickWindow::frameSwapped, this, [this] {
            m_lastSwapUs.store(m_clock.nsecsElapsed() / 1000);
        }, Qt::DirectConnection);
    }
}

// ---------------------------------------------------------------- PDF background and text

void InkCanvas::setBackground(const QString &file, qreal renderedScale)
{
    const int gen = ++m_backgroundGeneration;
    // Decode off the GUI thread; the scene graph picks the image up next frame.
    QPointer<InkCanvas> self(this);
    QThreadPool::globalInstance()->start([self, file, renderedScale, gen] {
        QImageReader reader(file);
        QImage img = reader.read();
        if (img.isNull() || !self) return;
        img = img.convertToFormat(QImage::Format_RGB32);
        InkCanvas *target = self.data();
        if (!target) return;
        QMetaObject::invokeMethod(target, [self, img, renderedScale, gen] {
            InkCanvas *th = self.data(); if (!th) return;
            th->applyBackground(img, renderedScale, gen);
        }, Qt::QueuedConnection);
    });
}

void InkCanvas::setImages(const QVariantList &images)
{
    QVector<PageImage> next;
    next.reserve(images.size());
    for (const QVariant &v : images) {
        const QVariantMap m = v.toMap();
        PageImage img;
        img.id = m.value(QStringLiteral("id")).toLongLong();
        img.path = m.value(QStringLiteral("path")).toString();
        img.rect = QRectF(m.value(QStringLiteral("x")).toDouble(), m.value(QStringLiteral("y")).toDouble(),
                          m.value(QStringLiteral("w")).toDouble(), m.value(QStringLiteral("h")).toDouble());
        // Keep the texture we already have for this picture; only its rectangle may have moved.
        for (PageImage &old : m_images) {
            if (old.id != img.id || old.path != img.path) continue;
            img.texture = old.texture; img.node = old.node; old.texture = nullptr; old.node = nullptr;
            break;
        }
        next.append(img);
    }
    for (PageImage &gone : m_images) {          // whatever was not claimed above is no longer on the page
        if (gone.node) m_orphanNodes.append(gone.node);
        delete gone.texture;
    }
    m_images = next;
    m_imagesDirty = true;
    QPointer<InkCanvas> self(this);
    for (const PageImage &img : m_images) {
        if (img.texture || img.path.isEmpty()) continue;
        const qint64 id = img.id;
        const QString path = img.path;
        QThreadPool::globalInstance()->start([self, id, path] {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            QImage loaded = reader.read();
            if (loaded.isNull() || !self) return;
            loaded = loaded.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            InkCanvas *target = self.data();
            if (!target) return;
            QMetaObject::invokeMethod(target, [self, id, loaded] { if (self) self->applyImage(id, loaded); }, Qt::QueuedConnection);
        });
    }
    update();
}

void InkCanvas::applyImage(qint64 id, const QImage &img)
{
    for (PageImage &p : m_images) {
        if (p.id != id) continue;
        p.pending = img;
        p.needsTexture = true;
        m_imagesDirty = true;
        update();
        return;
    }
}

void InkCanvas::applyBackground(const QImage &img, qreal renderedScale, int gen)
{
    if (gen != m_backgroundGeneration) return;   // a newer render superseded this one
    m_pendingBackground = img;
    m_backgroundScale = renderedScale;
    m_hasBackground = true;
    m_backgroundDirty = true;
    emit backgroundChanged();
    update();
}

void InkCanvas::clearBackground()
{
    ++m_backgroundGeneration;
    m_pendingBackground = QImage();
    m_hasBackground = false;
    m_backgroundScale = 0;
    m_backgroundDirty = true;
    emit backgroundChanged();
    update();
}

void InkCanvas::setWords(const QVariantList &words)
{
    m_words.clear();
    m_words.reserve(words.size());
    for (const QVariant &v : words) {
        const QVariantList w = v.toList();
        if (w.size() < 7) continue;
        m_words.append({QRectF(QPointF(w[0].toDouble(), w[1].toDouble()), QPointF(w[2].toDouble(), w[3].toDouble())), w[4].toString(), w[5].toInt(), w[6].toInt()});
    }
    m_textSelA = m_textSelB = -1;
    m_textSelDirty = true;
    emit selectionChanged();
    update();
}

int InkCanvas::wordAt(QPointF page) const
{
    for (int i = 0; i < m_words.size(); ++i) if (m_words[i].box.adjusted(-2, -2, 2, 2).contains(page)) return i;
    // Nearest word on the same line band, so a drag through whitespace still selects.
    int best = -1; double bestD = 1e18;
    for (int i = 0; i < m_words.size(); ++i) {
        const QRectF &b = m_words[i].box;
        if (page.y() < b.top() - 2 || page.y() > b.bottom() + 2) continue;
        const double d = std::abs(page.x() - b.center().x());
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void InkCanvas::textSelectTo(QPointF page)
{
    const int w = wordAt(page);
    if (w >= 0 && w != m_textSelB) { m_textSelB = w; m_textSelDirty = true; emit selectionChanged(); }
}

QString InkCanvas::selectedText() const
{
    if (!hasTextSelection()) return {};
    const int a = std::min(m_textSelA, m_textSelB), b = std::max(m_textSelA, m_textSelB);
    QString out;
    for (int i = a; i <= b; ++i) {
        if (!out.isEmpty()) out += (m_words[i].line != m_words[i - 1].line || m_words[i].block != m_words[i - 1].block) ? '\n' : ' ';
        out += m_words[i].text;
    }
    return out;
}

void InkCanvas::copyText()
{
    const QString t = selectedText();
    if (!t.isEmpty()) QGuiApplication::clipboard()->setText(t);
}

// A highlighter stroke dragged across PDF text becomes clean per-line bands over the words it
// touched (Goodnotes' text highlight). Returns false when the stroke did not cross text.
bool InkCanvas::snapHighlightToWords()
{
    if (m_active.points.size() < 2) return false;
    QRectF sb; for (const InkPoint &p : m_active.points) sb |= QRectF(p.x, p.y, 0.01, 0.01);
    QHash<int, QRectF> lines;   // (block<<16 | line) → union of touched word boxes
    float lineH = 0; int hits = 0;
    for (const Word &w : m_words) {
        if (!w.box.intersects(sb.adjusted(-1, -1, 1, 1))) continue;
        bool crossed = false;
        for (const InkPoint &p : m_active.points) if (w.box.adjusted(-1, -3, 1, 3).contains(QPointF(p.x, p.y))) { crossed = true; break; }
        if (!crossed) continue;
        const int key = (w.block << 16) | w.line;
        lines[key] |= w.box;
        lineH = std::max(lineH, float(w.box.height()));
        ++hits;
    }
    if (hits == 0 || sb.height() > lineH * 2.5) return false;
    QVector<Stroke> bands;
    for (auto it = lines.cbegin(); it != lines.cend(); ++it) {
        Stroke s = m_active; s.id = 0; s.points.clear();
        const QRectF b = it.value();
        s.width = float(b.height() * 1.15);
        s.points.append({float(b.left()), float(b.center().y()), 1.f, 0, 0, 0});
        s.points.append({float(b.right()), float(b.center().y()), 1.f, 0, 0, 1});
        bands.append(s);
    }
    m_undo.push(std::make_unique<ReplaceStrokesCommand>(QVector<quint64>{}, bands));   // one gesture, one undo
    return true;
}

// ---------------------------------------------------------------- replay

bool InkCanvas::isHidden(quint64 id) const
{
    if (m_hidden.contains(id) || m_selection.contains(id)) return true;
    if (m_replayMs >= 0 && m_replayRecordingId) {
        const Stroke *s = m_doc.stroke(id);
        if (s && qint64(s->recordingId) == m_replayRecordingId && qint64(s->startMs) + (s->points.isEmpty() ? 0 : s->points.last().tMs) > m_replayMs) return true;
    }
    return false;
}

void InkCanvas::markReplayChunksDirty()
{
    for (int i = 0; i < m_chunks.size(); ++i)
        for (quint64 id : m_chunks[i].ids) { const Stroke *s = m_doc.stroke(id); if (s && qint64(s->recordingId) == m_replayRecordingId) { m_dirtyChunks.insert(i); break; } }
    m_previewDirty = true;
    update();
}

void InkCanvas::setReplayRecordingId(qint64 id)
{
    if (id == m_replayRecordingId) return;
    const qint64 old = m_replayRecordingId;
    m_replayRecordingId = old; markReplayChunksDirty();
    m_replayRecordingId = id; markReplayChunksDirty();
    emit styleChanged();
}

void InkCanvas::setReplayMs(qint64 ms)
{
    if (ms == m_replayMs) return;
    m_replayMs = ms;
    markReplayChunksDirty();
    emit styleChanged();
}

QVariantList InkCanvas::selectionStrokeIds() const
{
    QVariantList out; for (quint64 id : m_selection) out.append(QVariant::fromValue(qint64(id))); return out;
}

QString InkCanvas::renderSelectionToPng(qreal scale) const
{
    if (m_selection.isEmpty()) return {};
    const QRectF b = m_doc.boundsOf(m_selection).adjusted(-12, -12, 12, 12);
    QImage img(int(b.width() * scale) + 1, int(b.height() * scale) + 1, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(scale, scale);
    p.translate(-b.topLeft());
    for (quint64 id : m_selection) {
        const Stroke *s = m_doc.stroke(id);
        if (!s) continue;
        const QVector<InkPoint> pts = smoothStroke(s->points, 1.f);
        const QColor c = QColor::fromRgba(s->color);      // highlighter alpha is baked in at draw time
        for (int i = 1; i < pts.size(); ++i) {
            const float w = s->tool == InkTool::Highlighter ? s->width : m_curve.widthFor(s->width, (pts[i - 1].pressure + pts[i].pressure) / 2);
            p.setPen(QPen(c, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawLine(QPointF(pts[i - 1].x, pts[i - 1].y), QPointF(pts[i].x, pts[i].y));
        }
        if (pts.size() == 1) { p.setBrush(c); p.setPen(Qt::NoPen); p.drawEllipse(QPointF(pts[0].x, pts[0].y), s->width / 2, s->width / 2); }
    }
    p.end();
    QDir().mkpath(paths::cacheDir() + "/selection");
    const QString path = QStringLiteral("%1/selection/sel-%2.png").arg(paths::cacheDir()).arg(QDateTime::currentMSecsSinceEpoch());
    return img.save(path) ? path : QString();
}

void InkCanvas::flashRect(const QRectF &pageRect, int ms)
{
    m_flashRect = pageRect;
    m_flashUntilMs = m_clock.elapsed() + ms;
    if (!m_flashTimer) { m_flashTimer = new QTimer(this); m_flashTimer->setInterval(40); connect(m_flashTimer, &QTimer::timeout, this, [this] { if (m_clock.elapsed() > m_flashUntilMs) m_flashTimer->stop(); update(); }); }
    m_flashTimer->start();
    // bring it into view if it is off-screen
    const QPointF c = toScreen(pageRect.center());
    if (!QRectF(0, 0, width(), height()).contains(c)) setPan(QPointF(width() / 2, height() / 2) - pageRect.center() * m_zoom);
    update();
}
