// Scene-graph side of InkCanvas (D-003). Everything here runs in updatePaintNode with the render
// thread blocked, so document data and nodes may both be touched.
#include "inkcanvas.h"
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QSGClipNode>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QQuickWindow>
#include <QSGTransformNode>
#include <QSGVertexColorMaterial>
#include <cmath>
#include <cstring>
#include <numbers>

namespace {
struct RGBA { unsigned char r, g, b, a; };
RGBA premul(QRgb c)
{
    const float a = qAlpha(c) / 255.f;
    return {static_cast<unsigned char>(qRed(c) * a + 0.5f), static_cast<unsigned char>(qGreen(c) * a + 0.5f),
            static_cast<unsigned char>(qBlue(c) * a + 0.5f), static_cast<unsigned char>(qAlpha(c))};
}
// Append a ribbon to a coloured strip with a degenerate bridge (same trick as appendStrip()).
QSGGeometry::ColoredPoint2D colored(const InkVertex &v, RGBA c)
{
    // Premultiplied: the feather alpha scales every channel.
    QSGGeometry::ColoredPoint2D p;
    p.set(v.x, v.y, static_cast<unsigned char>(c.r * v.a + 0.5f), static_cast<unsigned char>(c.g * v.a + 0.5f),
          static_cast<unsigned char>(c.b * v.a + 0.5f), static_cast<unsigned char>(c.a * v.a + 0.5f));
    return p;
}
void appendColored(QVector<QSGGeometry::ColoredPoint2D> &out, const QVector<InkVertex> &piece, RGBA c)
{
    if (piece.isEmpty()) return;
    if (!out.isEmpty()) {
        out.append(out.last());
        const QSGGeometry::ColoredPoint2D first = colored(piece.first(), c);
        out.append(first);
        if (out.size() % 2 == 1) out.append(first);
    }
    for (const InkVertex &v : piece) out.append(colored(v, c));
}
void uploadColored(QSGGeometryNode *node, const QVector<QSGGeometry::ColoredPoint2D> &verts)
{
    QSGGeometry *g = node->geometry();
    g->allocate(verts.size());
    if (!verts.isEmpty()) std::memcpy(g->vertexData(), verts.constData(), size_t(verts.size()) * sizeof(QSGGeometry::ColoredPoint2D));
    node->markDirty(QSGNode::DirtyGeometry);
}
} // namespace

// Every node is vertex-coloured so feathered edges work everywhere (the `vertexColor` flag is kept
// for call sites; both paths build the same node).
QSGGeometryNode *InkCanvas::makeStripNode(bool)
{
    auto *node = new QSGGeometryNode;
    auto *g = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
    g->setDrawingMode(QSGGeometry::DrawTriangleStrip);
    node->setGeometry(g);
    node->setFlag(QSGNode::OwnsGeometry);
    node->setMaterial(new QSGVertexColorMaterial);
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

void InkCanvas::fillFlat(QSGGeometryNode *node, const QVector<InkVertex> &strip, QColor color)
{
    QVector<QSGGeometry::ColoredPoint2D> verts;
    verts.reserve(strip.size());
    const RGBA c = premul(color.rgba());
    for (const InkVertex &v : strip) verts.append(colored(v, c));
    uploadColored(node, verts);
}

// ---------------------------------------------------------------- document → chunks

void InkCanvas::onStrokeAdded(quint64 id, int)
{
    const Stroke *s = m_doc.stroke(id);
    if (!s) return;
    int idx = -1;
    for (int i = m_chunks.size() - 1; i >= 0; --i) {
        if (m_chunks[i].layer != s->tool) continue;
        if (m_chunks[i].ids.size() < 64 && m_chunks[i].vertices < 40000) idx = i;
        break;
    }
    if (idx < 0) { Chunk c; c.layer = s->tool; m_chunks.append(c); idx = m_chunks.size() - 1; }
    m_chunks[idx].ids.append(id);
    m_chunkOf.insert(id, idx);
    m_dirtyChunks.insert(idx);
    update();
}

void InkCanvas::onStrokeRemoved(quint64 id, int)
{
    const int idx = m_chunkOf.take(id);
    if (idx >= 0 && idx < m_chunks.size()) { m_chunks[idx].ids.removeOne(id); m_dirtyChunks.insert(idx); }
    m_hidden.remove(id);
    update();
}

void InkCanvas::onStrokesChanged(const QVector<quint64> &ids)
{
    for (quint64 id : ids) if (m_chunkOf.contains(id)) m_dirtyChunks.insert(m_chunkOf.value(id));
    m_selectionDirty = true;
    update();
}

void InkCanvas::onCleared()
{
    for (Chunk &c : m_chunks) if (c.node) m_orphanNodes.append(c.node);
    m_chunks.clear();
    m_chunkOf.clear();
    m_dirtyChunks.clear();
    m_hidden.clear();
    update();
}

void InkCanvas::markAllChunksDirty()
{
    for (int i = 0; i < m_chunks.size(); ++i) m_dirtyChunks.insert(i);
    m_selectionDirty = true;
}

void InkCanvas::rebuildChunk(Chunk &c)
{
    QVector<QSGGeometry::ColoredPoint2D> verts;
    QVector<InkVertex> piece;
    const float spacing = m_builtSpacing;
    const float feather = float(0.8 / m_zoom);
    for (quint64 id : c.ids) {
        const Stroke *s = m_doc.stroke(id);
        if (!s || isHidden(id)) continue;
        buildRibbon(smoothStroke(s->points, spacing), s->width, s->tool, m_curve, piece, feather);
        appendColored(verts, piece, premul(s->color));
    }
    uploadColored(c.node, verts);
    c.vertices = verts.size();
    c.dirty = false;
}

// ---------------------------------------------------------------- page background

// Page guides for the current style, regenerated for the visible region (+50 %) so infinite
// canvases stay cheap: dots at 5 mm, lines at 8 mm, grid at 5 mm, Cornell = lines + cue column +
// summary band. All in page units so zoom is just the transform.
void InkCanvas::buildDots(QSGGeometryNode *node)
{
    QVector<InkVertex> tri;
    const QRectF view(toPage(QPointF(0, 0)), toPage(QPointF(width(), height())));
    QRectF region = view.adjusted(-view.width() * 0.5, -view.height() * 0.5, view.width() * 0.5, view.height() * 0.5);
    if (!m_infinite) region &= QRectF(QPointF(0, 0), m_pageSize);
    m_dotsRegion = region;
    m_dotsZoom = m_zoom;
    const float hairline = float(std::max(0.75 / m_zoom, 0.35));
    auto quad = [&](float x0, float y0, float x1, float y1, float a = 1.f) {
        tri.append({x0, y0, a}); tri.append({x1, y0, a}); tri.append({x0, y1, a});
        tri.append({x1, y0, a}); tri.append({x1, y1, a}); tri.append({x0, y1, a});
    };
    auto hline = [&](float y, float x0, float x1, float thick, float a = 1.f) { quad(x0, y - thick / 2, x1, y + thick / 2, a); };
    auto vline = [&](float x, float y0, float y1, float thick, float a = 1.f) { quad(x - thick / 2, y0, x + thick / 2, y1, a); };
    const bool inPage = !m_infinite;
    const float left = inPage ? 0.f : float(region.left()), right = inPage ? float(m_pageSize.width()) : float(region.right());
    const float top = inPage ? 0.f : float(region.top()), bottom = inPage ? float(m_pageSize.height()) : float(region.bottom());
    if (m_zoom >= 0.3 && !region.isEmpty()) {
        switch (m_pageStyle) {
        case PageStyle::Plain: break;
        case PageStyle::Dotted: {
            const float pitch = 19.f, half = hairline;
            const int x0 = int(std::floor(region.left() / pitch)), x1 = int(std::ceil(region.right() / pitch));
            const int y0 = int(std::floor(region.top() / pitch)), y1 = int(std::ceil(region.bottom() / pitch));
            const long count = long(x1 - x0 + 1) * long(y1 - y0 + 1);
            if (count > 0 && count < 80000)
                for (int j = y0; j <= y1; ++j) for (int i = x0; i <= x1; ++i) {
                    const float cx = i * pitch, cy = j * pitch;
                    if (inPage && (cx <= 0 || cy <= 0 || cx >= m_pageSize.width() || cy >= m_pageSize.height())) continue;
                    quad(cx - half, cy - half, cx + half, cy + half);
                }
            break;
        }
        case PageStyle::Lined:
        case PageStyle::Cornell: {
            const float pitch = 30.f; // 8 mm
            const float y0 = inPage ? pitch : std::floor(region.top() / pitch) * pitch;
            const float ymax = m_pageStyle == PageStyle::Cornell && inPage ? bottom - 192.f : bottom;
            for (float y = y0; y < ymax; y += pitch) if (y >= region.top() - pitch && y <= region.bottom() + pitch) hline(y, left, right, hairline);
            if (m_pageStyle == PageStyle::Cornell && inPage) {
                vline(240.f, 0.f, bottom - 192.f, hairline * 2.2f);
                hline(bottom - 192.f, left, right, hairline * 2.2f);
            }
            break;
        }
        case PageStyle::Grid: {
            const float pitch = 19.f;
            const float x0 = inPage ? pitch : std::floor(region.left() / pitch) * pitch;
            const float y0 = inPage ? pitch : std::floor(region.top() / pitch) * pitch;
            for (float y = y0; y < bottom; y += pitch) if (y >= region.top() - pitch && y <= region.bottom() + pitch) hline(y, left, right, hairline, 0.7f);
            for (float x = x0; x < right; x += pitch) if (x >= region.left() - pitch && x <= region.right() + pitch) vline(x, top, bottom, hairline, 0.7f);
            break;
        }
        case PageStyle::Graph: {
            // Real graph paper: 2 mm squares with every fifth line darker, for plotting by hand.
            const float pitch = 7.5f;                 // 2 mm at 96 dpi
            const float x0 = std::floor(region.left() / pitch) * pitch;
            const float y0 = std::floor(region.top() / pitch) * pitch;
            const long lines = long((region.width() + region.height()) / pitch);
            if (lines > 0 && lines < 6000) {
                for (float y = y0; y < bottom; y += pitch) {
                    if (y < region.top() - pitch || y > region.bottom() + pitch || (inPage && y <= 0)) continue;
                    const bool major = std::fmod(std::round(y / pitch), 5.f) == 0.f;
                    hline(y, left, right, hairline * (major ? 1.4f : 1.f), major ? 0.85f : 0.45f);
                }
                for (float x = x0; x < right; x += pitch) {
                    if (x < region.left() - pitch || x > region.right() + pitch || (inPage && x <= 0)) continue;
                    const bool major = std::fmod(std::round(x / pitch), 5.f) == 0.f;
                    vline(x, top, bottom, hairline * (major ? 1.4f : 1.f), major ? 0.85f : 0.45f);
                }
            }
            break;
        }
        case PageStyle::Isometric: {
            // Three families of lines at 0°, +60°, −60°: crystal structures and 3-D sketches.
            const float pitch = 26.f;
            const float tan60 = 1.7320508f;
            for (float y = std::floor(region.top() / pitch) * pitch; y < bottom; y += pitch)
                if (y >= region.top() - pitch && y <= region.bottom() + pitch && !(inPage && y <= 0)) hline(y, left, right, hairline, 0.5f);
            const float span = (bottom - top);
            const float step = pitch * 2.f;
            for (float c = std::floor((region.left() - span / tan60) / step) * step; c < right + span / tan60; c += step) {
                for (int dir = -1; dir <= 1; dir += 2) {
                    const float xTop = c, xBottom = c + dir * span / tan60;
                    // a thin quad along the sloped line
                    const float t = hairline * 0.75f;
                    tri.append({xTop - t, top, 0.5f}); tri.append({xTop + t, top, 0.5f}); tri.append({xBottom - t, bottom, 0.5f});
                    tri.append({xTop + t, top, 0.5f}); tri.append({xBottom + t, bottom, 0.5f}); tri.append({xBottom - t, bottom, 0.5f});
                }
            }
            break;
        }
        case PageStyle::Music: {
            // Staves of five lines, grouped as they are printed.
            const float lineGap = 9.f, staffGap = 64.f;
            const float first = inPage ? 60.f : std::floor(region.top() / staffGap) * staffGap;
            for (float y = first; y < bottom - 4 * lineGap; y += staffGap)
                for (int i = 0; i < 5; ++i) {
                    const float ly = y + i * lineGap;
                    if (ly < region.top() - staffGap || ly > region.bottom() + staffGap) continue;
                    hline(ly, inPage ? 48.f : left, inPage ? float(m_pageSize.width()) - 48.f : right, hairline, 0.8f);
                }
            break;
        }
        }
    }
    fillFlat(node, tri, m_dotColor);
}

void InkCanvas::buildPageNode(QSGNode *)
{
    QVector<InkVertex> rect;
    if (!m_infinite) {
        const float w = float(m_pageSize.width()), h = float(m_pageSize.height());
        rect = {{0, 0}, {w, 0}, {0, h}, {w, h}};
    }
    fillFlat(m_paperRect, rect, m_paperColor);
    QVector<InkVertex> frame;
    if (!m_infinite) {
        const float w = float(m_pageSize.width()), h = float(m_pageSize.height()), t = float(1.0 / m_zoom);
        auto quad = [&](float x0, float y0, float x1, float y1) {
            frame.append({x0, y0, 1.f}); frame.append({x1, y0, 1.f}); frame.append({x0, y1, 1.f});
            frame.append({x1, y0, 1.f}); frame.append({x1, y1, 1.f}); frame.append({x0, y1, 1.f});
        };
        quad(-t, -t, w + t, 0); quad(-t, h, w + t, h + t); quad(-t, 0, 0, h); quad(w, 0, w + t, h);
    }
    fillFlat(m_pageFrame, frame, m_frameColor);
    buildDots(m_dotsNode);
}

// ---------------------------------------------------------------- the frame

QSGNode *InkCanvas::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    QSGNode *root = old;
    if (!root) {
        root = new QSGNode;
        m_viewNode = new QSGTransformNode;
        root->appendChildNode(m_viewNode);
        m_pageNode = new QSGNode;
        m_viewNode->appendChildNode(m_pageNode);
        m_paperRect = makeStripNode(false);
        m_pageNode->appendChildNode(m_paperRect);
        m_imagesNode = new QSGNode;                    // pictures sit above the paper and under the ink
        m_pageNode->appendChildNode(m_imagesNode);
        m_backgroundNode = new QSGSimpleTextureNode;   // attached to the tree only while it has a texture
        m_backgroundNode->setFiltering(QSGTexture::Linear);
        m_backgroundNode->setOwnsTexture(false);
        m_dotsNode = makeStripNode(false);
        m_dotsNode->geometry()->setDrawingMode(QSGGeometry::DrawTriangles);
        m_pageNode->appendChildNode(m_dotsNode);
        m_pageFrame = makeStripNode(false);
        m_pageFrame->geometry()->setDrawingMode(QSGGeometry::DrawTriangles);
        m_pageNode->appendChildNode(m_pageFrame);
        // Ink is cut off at the paper's edge, like a real page: a stroke may start on the page
        // and carry on past it, but nothing past the edge is drawn.
        m_inkClip = new QSGClipNode;
        m_inkClip->setIsRectangular(true);
        auto *clipGeometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 4);
        clipGeometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
        m_inkClip->setGeometry(clipGeometry);
        m_inkClip->setFlag(QSGNode::OwnsGeometry);
        m_viewNode->appendChildNode(m_inkClip);
        m_highlighterLayer = new QSGNode;
        m_inkClip->appendChildNode(m_highlighterLayer);
        m_inkLayer = new QSGNode;
        m_inkClip->appendChildNode(m_inkLayer);
        m_previewNode = makeStripNode(true);
        m_inkClip->appendChildNode(m_previewNode);
        m_selectionNode = new QSGTransformNode;
        m_viewNode->appendChildNode(m_selectionNode);
        m_selectionInk = makeStripNode(true);
        m_selectionNode->appendChildNode(m_selectionInk);
        m_selectionBox = makeStripNode(false);
        m_selectionBox->geometry()->setDrawingMode(QSGGeometry::DrawLineStrip);
        m_selectionBox->geometry()->setLineWidth(1);
        m_selectionNode->appendChildNode(m_selectionBox);
        m_selectionHandles = makeStripNode(false);
        m_selectionHandles->geometry()->setDrawingMode(QSGGeometry::DrawTriangles);
        m_selectionNode->appendChildNode(m_selectionHandles);
        m_liveNode = makeStripNode(false);
        m_inkClip->appendChildNode(m_liveNode);
        m_lassoNode = makeStripNode(false);
        m_viewNode->appendChildNode(m_lassoNode);
        m_textSelNode = makeStripNode(false);
        m_textSelNode->geometry()->setDrawingMode(QSGGeometry::DrawTriangles);
        m_viewNode->appendChildNode(m_textSelNode);
        m_flashNode = makeStripNode(false);
        m_flashNode->geometry()->setDrawingMode(QSGGeometry::DrawTriangles);
        m_viewNode->appendChildNode(m_flashNode);
        m_cursorNode = makeStripNode(false);
        m_cursorNode->geometry()->setDrawingMode(QSGGeometry::DrawLineStrip);   // LineLoop is not a Vulkan topology
        m_cursorNode->geometry()->setLineWidth(1);
        root->appendChildNode(m_cursorNode);
        m_rebuildAll = true;
        m_viewDirty = true;
    }
    ++m_framesThisSecond;

    // Latency: the swap after the previous frame showed the sample we noted then.
    if (m_pendingArrivalUs >= 0) {
        const qint64 swap = m_lastSwapUs.load();
        if (swap > m_pendingSetUs) {
            m_latencies.append(double(swap - m_pendingArrivalUs) / 1000.0);
            if (m_latencies.size() > 240) m_latencies.remove(0, m_latencies.size() - 240);
        }
        m_pendingArrivalUs = -1;
    }
    if (m_gesture == Gesture::Ink && m_lastSampleArrivalUs >= 0) {
        m_pendingArrivalUs = m_lastSampleArrivalUs;
        m_pendingSetUs = m_clock.nsecsElapsed() / 1000;
        m_lastSampleArrivalUs = -1;
    }

    for (QSGNode *n : m_orphanNodes) { if (n->parent()) n->parent()->removeChildNode(n); delete n; }
    m_orphanNodes.clear();

    // PDF background: a page-sized textured quad under the guides.
    if (m_backgroundDirty) {
        if (m_backgroundNode->parent()) m_pageNode->removeChildNode(m_backgroundNode);
        delete m_backgroundTexture; m_backgroundTexture = nullptr;
        if (!m_pendingBackground.isNull() && window()) {
            m_backgroundTexture = window()->createTextureFromImage(m_pendingBackground);
            m_backgroundTexture->setFiltering(QSGTexture::Linear);
            m_backgroundNode->setTexture(m_backgroundTexture);
            m_backgroundNode->setRect(QRectF(QPointF(0, 0), m_pageSize));
            m_pageNode->insertChildNodeAfter(m_backgroundNode, m_paperRect);
            m_backgroundNode->markDirty(QSGNode::DirtyMaterial | QSGNode::DirtyGeometry);
            m_pendingBackground = QImage();
        }
        m_backgroundDirty = false;
    }
    if (m_backgroundTexture && m_backgroundNode->rect().size() != m_pageSize) m_backgroundNode->setRect(QRectF(QPointF(0, 0), m_pageSize));

    // Pictures on the page: one textured quad each, in page coordinates under the ink.
    if (m_imagesDirty) {
        for (PageImage &img : m_images) {
            if (img.needsTexture && window() && !img.pending.isNull()) {
                delete img.texture;
                img.texture = window()->createTextureFromImage(img.pending);
                if (img.texture) img.texture->setFiltering(QSGTexture::Linear);
                img.pending = QImage();
                img.needsTexture = false;
                if (img.node) { img.node->setTexture(img.texture); img.node->markDirty(QSGNode::DirtyMaterial); }
            }
            if (!img.texture) continue;
            if (!img.node) {
                img.node = new QSGSimpleTextureNode;
                img.node->setFiltering(QSGTexture::Linear);
                img.node->setOwnsTexture(false);
                img.node->setTexture(img.texture);
                m_imagesNode->appendChildNode(img.node);
            }
            if (img.node->rect() != img.rect) { img.node->setRect(img.rect); img.node->markDirty(QSGNode::DirtyGeometry); }
        }
        m_imagesDirty = false;
    }

    const QRectF inkBounds = m_infinite ? QRectF(-1e7, -1e7, 2e7, 2e7) : QRectF(QPointF(0, 0), m_pageSize);
    if (m_inkClip->clipRect() != inkBounds) {
        m_inkClip->setClipRect(inkBounds);
        QSGGeometry::updateRectGeometry(m_inkClip->geometry(), inkBounds);
        m_inkClip->markDirty(QSGNode::DirtyGeometry);
    }

    if (m_viewDirty) {
        QMatrix4x4 m;
        m.translate(float(m_pan.x()), float(m_pan.y()));
        m.scale(float(m_zoom));
        m_viewNode->setMatrix(m);
        m_viewNode->markDirty(QSGNode::DirtyMatrix);
        // Re-tessellating every stroke on the page mid-pinch is why zooming hitched: the spacing
        // crosses the threshold several times in one gesture. Wait for the gesture to settle, as
        // Rnote (400 ms) and Xournal++ (300 ms) both do.
        if (std::abs(smoothingSpacing() - m_builtSpacing) > 0.25f && !m_spacingSettle.isActive())
            m_spacingSettle.start();
        const QRectF view(toPage(QPointF(0, 0)), toPage(QPointF(width(), height())));
        if (!m_dotsRegion.contains(view) || std::abs(m_dotsZoom - m_zoom) > 1e-6) buildPageNode(m_pageNode);
        m_viewDirty = false;
    }

    // Chunk nodes exist lazily; dirty ones rebuild.
    QElapsedTimer t; t.start();
    for (int i = 0; i < m_chunks.size(); ++i) {
        Chunk &c = m_chunks[i];
        if (!c.node) {
            c.node = makeStripNode(true);
            (c.layer == InkTool::Highlighter ? m_highlighterLayer : m_inkLayer)->appendChildNode(c.node);
            m_dirtyChunks.insert(i);
        }
    }
    if (m_rebuildAll) { markAllChunksDirty(); m_rebuildAll = false; }
    for (int i : std::as_const(m_dirtyChunks)) if (i >= 0 && i < m_chunks.size()) rebuildChunk(m_chunks[i]);
    if (!m_dirtyChunks.isEmpty()) m_lastRebuildMs = t.nsecsElapsed() / 1e6;
    m_dirtyChunks.clear();

    // Pixel-eraser preview: the pieces of every stroke being cut, in their own colours.
    if (m_previewDirty) {
        QVector<QSGGeometry::ColoredPoint2D> verts;
        QVector<InkVertex> piece;
        for (const auto &pieces : std::as_const(m_pixelWork))
            for (const Stroke &p : pieces) {
                buildRibbon(smoothStroke(p.points, m_builtSpacing), p.width, p.tool, m_curve, piece, float(0.8 / m_zoom));
                appendColored(verts, piece, premul(p.color));
            }
        // Replay: the stroke being "written" at replayMs is drawn up to that moment.
        if (m_replayMs >= 0 && m_replayRecordingId) {
            for (const Stroke &s : m_doc.strokes()) {
                if (qint64(s.recordingId) != m_replayRecordingId || s.points.isEmpty()) continue;
                const qint64 rel = m_replayMs - qint64(s.startMs);
                if (rel < 0 || rel >= s.points.last().tMs) continue;
                QVector<InkPoint> part;
                for (const InkPoint &p : s.points) { if (p.tMs > rel) break; part.append(p); }
                if (part.isEmpty()) continue;
                buildRibbon(smoothStroke(part, m_builtSpacing), s.width, s.tool, m_curve, piece, float(0.8 / m_zoom));
                appendColored(verts, piece, premul(s.color));
            }
        }
        uploadColored(m_previewNode, verts);
        m_previewDirty = false;
    }

    // Selection: the selected strokes drawn under the live transform, a box and four handles.
    if (m_selectionDirty) {
        QVector<QSGGeometry::ColoredPoint2D> verts;
        QVector<InkVertex> piece;
        for (quint64 id : std::as_const(m_selection)) {
            const Stroke *s = m_doc.stroke(id);
            if (!s) continue;
            buildRibbon(smoothStroke(s->points, m_builtSpacing), s->width, s->tool, m_curve, piece, float(0.8 / m_zoom));
            appendColored(verts, piece, premul(s->color));
        }
        uploadColored(m_selectionInk, verts);
        QVector<InkVertex> box, handles;
        if (!m_selection.isEmpty()) {
            const QRectF b = m_selectionBounds;
            box = {{float(b.left()), float(b.top())}, {float(b.right()), float(b.top())}, {float(b.right()), float(b.bottom())},
                   {float(b.left()), float(b.bottom())}, {float(b.left()), float(b.top())}};
            const float h = float(6.0 / m_zoom);
            const QPointF corners[4] = {b.topLeft(), b.topRight(), b.bottomRight(), b.bottomLeft()};
            for (const QPointF &c : corners) {
                const float x = float(c.x()), y = float(c.y());
                handles.append({x - h, y - h}); handles.append({x + h, y - h}); handles.append({x - h, y + h});
                handles.append({x + h, y - h}); handles.append({x + h, y + h}); handles.append({x - h, y + h});
            }
        }
        fillFlat(m_selectionBox, box, m_accentColor);
        fillFlat(m_selectionHandles, handles, m_accentColor);
        m_selectionNode->setMatrix(QMatrix4x4(m_selectionXf));
        m_selectionNode->markDirty(QSGNode::DirtyMatrix);
        m_selectionDirty = false;
    } else if (m_gesture == Gesture::MoveSelection || m_gesture == Gesture::ScaleSelection) {
        m_selectionNode->setMatrix(QMatrix4x4(m_selectionXf));
        m_selectionNode->markDirty(QSGNode::DirtyMatrix);
    }

    // Live stroke: raw points + one frame of prediction, smoothed, rebuilt every frame.
    {
        QVector<InkVertex> strip;
        if (m_gesture == Gesture::Ink && !m_active.points.isEmpty()) {
            QVector<InkPoint> pts = m_active.points;
            if (m_predictionEnabled && pts.size() >= 3) pts.append(predictPoint(pts, float(m_predictionMs)));
            buildRibbon(smoothStroke(pts, m_builtSpacing), m_active.width, m_active.tool, m_curve, strip, float(0.8 / m_zoom));
        }
        QSGNode *wantParent = (m_gesture == Gesture::Ink && m_active.tool == InkTool::Highlighter)
                            ? static_cast<QSGNode *>(m_highlighterLayer) : static_cast<QSGNode *>(m_viewNode);
        if (m_liveNode->parent() != wantParent) {
            m_liveNode->parent()->removeChildNode(m_liveNode);
            wantParent->appendChildNode(m_liveNode);
        }
        fillFlat(m_liveNode, strip, QColor::fromRgba(m_active.color));
    }

    // Lasso path
    {
        QVector<InkVertex> strip;
        if (m_gesture == Gesture::Lasso && m_lasso.size() >= 2) {
            QVector<InkPoint> pts; pts.reserve(m_lasso.size());
            for (const QPointF &p : std::as_const(m_lasso)) pts.append({float(p.x()), float(p.y()), 1.f, 0, 0, 0});
            PressureCurve flat; flat.minScale = flat.maxScale = 1.f;
            buildRibbon(pts, float(1.5 / m_zoom), InkTool::Pen, flat, strip);
        }
        fillFlat(m_lassoNode, strip, m_accentColor);
    }

    // Text selection over PDF words: one translucent band per selected word.
    if (m_textSelDirty) {
        QVector<InkVertex> tri;
        if (hasTextSelection()) {
            const int a = std::min(m_textSelA, m_textSelB), b = std::max(m_textSelA, m_textSelB);
            for (int i = a; i <= b && i < m_words.size(); ++i) {
                const QRectF r = m_words[i].box.adjusted(-1, -1, 1, 1);
                const float x0 = float(r.left()), y0 = float(r.top()), x1 = float(r.right()), y1 = float(r.bottom());
                tri.append({x0, y0, 0.3f}); tri.append({x1, y0, 0.3f}); tri.append({x0, y1, 0.3f});
                tri.append({x1, y0, 0.3f}); tri.append({x1, y1, 0.3f}); tri.append({x0, y1, 0.3f});
            }
        }
        fillFlat(m_textSelNode, tri, m_accentColor);
        m_textSelDirty = false;
    }

    // Search / OCR hit flash: a translucent accent rect that fades out.
    {
        QVector<InkVertex> tri;
        const qint64 left = m_flashUntilMs - m_clock.elapsed();
        if (left > 0 && !m_flashRect.isEmpty()) {
            const float a = float(std::min(1.0, left / 800.0)) * 0.35f;
            const QRectF r = m_flashRect.adjusted(-4, -4, 4, 4);
            const float x0 = float(r.left()), y0 = float(r.top()), x1 = float(r.right()), y1 = float(r.bottom());
            tri.append({x0, y0, a}); tri.append({x1, y0, a}); tri.append({x0, y1, a});
            tri.append({x1, y0, a}); tri.append({x1, y1, a}); tri.append({x0, y1, a});
        }
        fillFlat(m_flashNode, tri, m_accentColor);
    }

    // Cursor, in screen space: a ring for the eraser, a small dot for the pen while it hovers.
    {
        QVector<InkVertex> shape;
        const bool hovering = m_hoverValid && m_gesture == Gesture::None;   // pen or mouse: the dot is the cursor
        if (effectiveTool() == Tool::Eraser && m_hoverValid) {
            const float r = float(m_eraserRadius);
            m_cursorNode->geometry()->setDrawingMode(QSGGeometry::DrawLineStrip);
            for (int i = 0; i <= 32; ++i) {
                const float a = float(i % 32) / 32 * 2 * std::numbers::pi_v<float>;
                shape.append({float(m_hoverLocal.x()) + r * std::cos(a), float(m_hoverLocal.y()) + r * std::sin(a), 1.f});
            }
        } else if (hovering && (m_tool == Tool::Pen || m_tool == Tool::Highlighter)) {
            m_cursorNode->geometry()->setDrawingMode(QSGGeometry::DrawTriangleStrip);
            const float r = 2.f;
            for (int i = 0; i <= 12; ++i) {
                const float a = float(i) / 12 * 2 * std::numbers::pi_v<float>;
                shape.append({float(m_hoverLocal.x()), float(m_hoverLocal.y()), 1.f});
                shape.append({float(m_hoverLocal.x()) + r * std::cos(a), float(m_hoverLocal.y()) + r * std::sin(a), 0.4f});
            }
        }
        fillFlat(m_cursorNode, shape, m_accentColor);
    }
    return root;
}
