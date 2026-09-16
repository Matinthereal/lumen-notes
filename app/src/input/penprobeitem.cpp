#include "penprobeitem.h"
#include <QDateTime>
#include <QDir>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QStandardPaths>
#include <QTimer>
#include <QTouchEvent>
#include <cmath>

PenProbeItem::PenProbeItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setAcceptTouchEvents(true);
    m_rateTimer = new QTimer(this);
    m_rateTimer->setInterval(250);
    connect(m_rateTimer, &QTimer::timeout, this, [this] {
        m_eventsPerSecond = m_eventsThisWindow * 4.0;
        m_eventsThisWindow = 0;
        emit changed();
    });
    m_rateTimer->start();
    openLog();
}

PenProbeItem::~PenProbeItem()
{
    writeSummary();
}

void PenProbeItem::openLog()
{
    // ~/.local/state/lumen regardless of organisation/app naming (StateLocation nests them).
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/lumen");
    QDir().mkpath(dir);
    m_logPath = QDir(dir).filePath(QStringLiteral("pen-probe.log"));
    m_log.setFileName(m_logPath);
    if (m_log.open(QIODevice::Append | QIODevice::Text)) {
        m_logStream.setDevice(&m_log);
        m_logStream << "\n# session " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
        m_logStream.flush();
    }
}

void PenProbeItem::logLine(const QString &line)
{
    if (!m_log.isOpen()) return;
    m_logStream << line << '\n';
    if (++m_linesSinceFlush >= 100) { m_logStream.flush(); m_linesSinceFlush = 0; }
}

void PenProbeItem::writeSummary()
{
    if (!m_log.isOpen()) return;
    m_logStream << "# summary device=\"" << m_deviceName << "\" caps=\"" << m_capabilities << "\""
                << " pressure=" << m_minPressure << ".." << m_maxPressure
                << " xtilt=" << m_minXTilt << ".." << m_maxXTilt
                << " ytilt=" << m_minYTilt << ".." << m_maxYTilt
                << " z=" << m_minZ << ".." << m_maxZ
                << " eraserSeen=" << (m_eraserSeen ? 1 : 0)
                << " stylusButtons=\"" << m_stylusButtonsSeen << "\""
                << " strokes=" << m_strokeCount << " points=" << m_pointCount
                << " touchWhilePen=" << m_touchWhilePen << "\n";
    m_logStream.flush();
}

static QString buttonsToString(Qt::MouseButtons b)
{
    QStringList out;
    if (b & Qt::LeftButton) out << "tip";
    if (b & Qt::MiddleButton) out << "stylus1";
    if (b & Qt::RightButton) out << "stylus2";
    if (b & Qt::BackButton) out << "back";
    if (b & Qt::ForwardButton) out << "forward";
    return out.isEmpty() ? QStringLiteral("none") : out.join('+');
}

void PenProbeItem::recordRanges(const TabletSample &s)
{
    if (!m_anyRange) {
        m_minPressure = m_maxPressure = s.pressure;
        m_minXTilt = m_maxXTilt = s.xTilt;
        m_minYTilt = m_maxYTilt = s.yTilt;
        m_minZ = m_maxZ = s.z;
        m_anyRange = true;
    } else {
        m_minPressure = std::min(m_minPressure, s.pressure); m_maxPressure = std::max(m_maxPressure, s.pressure);
        m_minXTilt = std::min(m_minXTilt, s.xTilt);          m_maxXTilt = std::max(m_maxXTilt, s.xTilt);
        m_minYTilt = std::min(m_minYTilt, s.yTilt);          m_maxYTilt = std::max(m_maxYTilt, s.yTilt);
        m_minZ = std::min(m_minZ, s.z);                      m_maxZ = std::max(m_maxZ, s.z);
    }
    if (s.eraser) m_eraserSeen = true;
    const Qt::MouseButtons side = s.buttons & (Qt::MiddleButton | Qt::RightButton | Qt::BackButton | Qt::ForwardButton);
    if (side) {
        const QString name = buttonsToString(side);
        if (!m_stylusButtonsSeen.contains(name))
            m_stylusButtonsSeen += (m_stylusButtonsSeen.isEmpty() ? "" : " ") + name;
    }
}

bool PenProbeItem::tabletSample(const TabletSample &s)
{
    const QPointF local = mapFromScene(s.windowPos);
    const bool inside = contains(local);

    ++m_eventsThisWindow;
    m_deviceName = s.deviceName;
    m_capabilities = s.capabilities;
    m_pointerType = s.eraser ? QStringLiteral("eraser") : QStringLiteral("pen");
    m_pressure = s.pressure;
    m_xTilt = s.xTilt;
    m_yTilt = s.yTilt;
    m_z = s.z;
    m_buttons = buttonsToString(s.buttons);
    m_inProximity = true;
    recordRanges(s);

    switch (s.kind) {
    case TabletSample::Kind::Press:   m_lastEvent = QStringLiteral("press"); break;
    case TabletSample::Kind::Release: m_lastEvent = QStringLiteral("release"); break;
    default:                          m_lastEvent = (s.buttons & Qt::LeftButton) ? QStringLiteral("move (tip down)") : QStringLiteral("hover"); break;
    }

    logLine(QStringLiteral("%1 %2 %3 %4 p=%5 xt=%6 yt=%7 z=%8 b=%9 %10")
                .arg(s.timestampMs).arg(m_lastEvent.section(' ', 0, 0))
                .arg(local.x(), 0, 'f', 2).arg(local.y(), 0, 'f', 2)
                .arg(s.pressure, 0, 'f', 4).arg(s.xTilt, 0, 'f', 1).arg(s.yTilt, 0, 'f', 1)
                .arg(s.z, 0, 'f', 2).arg(m_buttons).arg(m_pointerType));

    if (inside) {
        const bool tipNow = (s.kind == TabletSample::Kind::Press) || (s.kind == TabletSample::Kind::Move && (s.buttons & Qt::LeftButton));
        const float w = 1.5f + float(s.pressure) * 10.0f;
        if (s.kind == TabletSample::Kind::Press) {
            Stroke st; st.eraser = s.eraser;
            st.pts.append({float(local.x()), float(local.y()), w});
            m_strokes.append(st);
            m_firstDirtyStroke = std::min(m_firstDirtyStroke, int(m_strokes.size()) - 1);
            m_tipDown = true;
            ++m_strokeCount; ++m_pointCount;
        } else if (tipNow && !m_strokes.isEmpty() && m_tipDown) {
            m_strokes.last().pts.append({float(local.x()), float(local.y()), w});
            m_firstDirtyStroke = std::min(m_firstDirtyStroke, int(m_strokes.size()) - 1);
            ++m_pointCount;
        }
        if (s.kind == TabletSample::Kind::Release) {
            m_tipDown = false;
            m_logStream.flush();
        }
        update();
    } else if (s.kind == TabletSample::Kind::Release) {
        m_tipDown = false;
    }
    emit changed();
    return inside;
}

void PenProbeItem::tabletProximity(bool entering, const TabletSample &s)
{
    m_inProximity = entering;
    if (!entering) { m_tipDown = false; m_pressure = 0; m_buttons = QStringLiteral("none"); }
    if (!s.deviceName.isEmpty()) { m_deviceName = s.deviceName; m_capabilities = s.capabilities; }
    m_lastEvent = entering ? QStringLiteral("enter proximity") : QStringLiteral("leave proximity");
    logLine(QStringLiteral("%1 %2 %3").arg(s.timestampMs).arg(entering ? "proximity-enter" : "proximity-leave").arg(s.deviceName));
    m_logStream.flush();
    emit changed();
}

void PenProbeItem::touchEvent(QTouchEvent *event)
{
    int active = 0;
    for (const QEventPoint &p : event->points())
        if (p.state() != QEventPoint::Released) ++active;
    m_touchPoints = active;
    if (m_inProximity && active > 0 && event->type() == QEvent::TouchBegin)
        ++m_touchWhilePen; // this is the palm: touch that arrived while the pen was near
    logLine(QStringLiteral("%1 touch n=%2 penNear=%3").arg(event->timestamp()).arg(active).arg(m_inProximity ? 1 : 0));
    m_lastEvent = QStringLiteral("touch ×%1%2").arg(active).arg(m_inProximity ? " while pen near" : "");
    event->accept();
    emit changed();
}

void PenProbeItem::clear()
{
    m_strokes.clear();
    m_firstDirtyStroke = 0;
    m_clearRequested = true;
    m_strokeCount = m_pointCount = 0;
    update();
    emit changed();
}

QSGNode *PenProbeItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    QSGNode *root = old;
    if (m_clearRequested) {
        delete root;
        root = nullptr;
        m_clearRequested = false;
    }
    if (!root)
        root = new QSGNode;

    // Variable-width ribbon as a triangle strip: two vertices per point along the segment normal.
    auto build = [](const Stroke &st, QSGGeometry *g) {
        const int n = st.pts.size();
        if (n == 0) { g->allocate(0); return; }
        if (n == 1) {
            const Point &p = st.pts[0];
            const float r = p.w / 2;
            g->allocate(4);
            QSGGeometry::Point2D *v = g->vertexDataAsPoint2D();
            v[0].set(p.x - r, p.y - r); v[1].set(p.x + r, p.y - r);
            v[2].set(p.x - r, p.y + r); v[3].set(p.x + r, p.y + r);
            return;
        }
        g->allocate(n * 2);
        QSGGeometry::Point2D *v = g->vertexDataAsPoint2D();
        for (int i = 0; i < n; ++i) {
            const Point &p = st.pts[i];
            float dx, dy;
            if (i == 0)          { dx = st.pts[1].x - p.x;              dy = st.pts[1].y - p.y; }
            else if (i == n - 1) { dx = p.x - st.pts[i - 1].x;          dy = p.y - st.pts[i - 1].y; }
            else                 { dx = st.pts[i + 1].x - st.pts[i - 1].x; dy = st.pts[i + 1].y - st.pts[i - 1].y; }
            float len = std::sqrt(dx * dx + dy * dy);
            if (len < 1e-4f) { dx = 1; dy = 0; len = 1; }
            const float nx = -dy / len * p.w / 2, ny = dx / len * p.w / 2;
            v[2 * i].set(p.x + nx, p.y + ny);
            v[2 * i + 1].set(p.x - nx, p.y - ny);
        }
    };

    int childCount = root->childCount();
    while (childCount > m_strokes.size()) {
        QSGNode *last = root->childAtIndex(childCount - 1);
        root->removeChildNode(last);
        delete last;
        --childCount;
    }
    for (int i = m_firstDirtyStroke; i < m_strokes.size(); ++i) {
        QSGGeometryNode *node;
        if (i < childCount) {
            node = static_cast<QSGGeometryNode *>(root->childAtIndex(i));
        } else {
            node = new QSGGeometryNode;
            auto *mat = new QSGFlatColorMaterial;
            mat->setColor(m_strokes[i].eraser ? QColor(0xE0, 0x4E, 0x6E) : m_penColor);
            node->setMaterial(mat);
            node->setFlag(QSGNode::OwnsMaterial);
            auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
            geom->setDrawingMode(QSGGeometry::DrawTriangleStrip);
            node->setGeometry(geom);
            node->setFlag(QSGNode::OwnsGeometry);
            root->appendChildNode(node);
            ++childCount;
        }
        build(m_strokes[i], node->geometry());
        node->markDirty(QSGNode::DirtyGeometry);
    }
    m_firstDirtyStroke = m_strokes.size();
    return root;
}
