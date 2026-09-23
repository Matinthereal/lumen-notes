#pragma once
#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QPointF>
#include <QPolygonF>
#include <QQuickItem>
#include <QTimer>
#include <QSet>
#include <QSizeF>
#include <QTransform>
#include <QVector>
#include <QImage>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include "ink/inkdocument.h"
#include "ink/onefilter.h"
#include "ink/tessellate.h"
#include "ink/undostack.h"
#include "input/tabletsample.h"

class QSGGeometryNode;
class QSGNode;
class QSGTransformNode;
class QTimer;

// The ink engine's view: one page of strokes on the scene graph (D-003).
//   page units → screen: s = p * zoom + pan
//   committed strokes live in chunk nodes (≤ 64 strokes each) that rebuild only when a stroke in
//   them changes; the active stroke is its own node rebuilt per frame with one frame of prediction.
class InkCanvas : public QQuickItem, public TabletSink {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(QColor penColor MEMBER m_penColor NOTIFY styleChanged)
    Q_PROPERTY(qreal penWidth MEMBER m_penWidth NOTIFY styleChanged)
    Q_PROPERTY(QColor highlighterColor MEMBER m_highlighterColor NOTIFY styleChanged)
    Q_PROPERTY(qreal highlighterWidth MEMBER m_highlighterWidth NOTIFY styleChanged)
    Q_PROPERTY(qreal highlighterOpacity MEMBER m_highlighterOpacity NOTIFY styleChanged)
    Q_PROPERTY(qreal eraserRadius MEMBER m_eraserRadius NOTIFY styleChanged)     // screen px
    Q_PROPERTY(bool pixelEraser MEMBER m_pixelEraser NOTIFY styleChanged)
    Q_PROPERTY(QColor paperColor MEMBER m_paperColor NOTIFY styleChanged)
    Q_PROPERTY(QColor dotColor MEMBER m_dotColor NOTIFY styleChanged)
    Q_PROPERTY(QColor frameColor MEMBER m_frameColor NOTIFY styleChanged)
    Q_PROPERTY(QColor accentColor MEMBER m_accentColor NOTIFY styleChanged)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY viewChanged)
    Q_PROPERTY(QPointF pan READ pan WRITE setPan NOTIFY viewChanged)
    Q_PROPERTY(bool infinite READ infinite WRITE setInfinite NOTIFY viewChanged)
    Q_PROPERTY(QSizeF pageSize READ pageSize WRITE setPageSize NOTIFY viewChanged)
    Q_PROPERTY(QString pageStyle READ pageStyle WRITE setPageStyle NOTIFY viewChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(bool inking READ inking NOTIFY inkingChanged)
    Q_PROPERTY(bool penNear READ penNear NOTIFY inkingChanged)
    Q_PROPERTY(int strokeCount READ strokeCount NOTIFY documentChanged)
    Q_PROPERTY(int palmRejectMs MEMBER m_palmRejectMs NOTIFY styleChanged)
    Q_PROPERTY(bool predictionEnabled MEMBER m_predictionEnabled NOTIFY styleChanged)
    Q_PROPERTY(qreal predictionMs MEMBER m_predictionMs NOTIFY styleChanged)
    Q_PROPERTY(qreal pressureCeiling READ pressureCeiling WRITE setPressureCeiling NOTIFY styleChanged)
    Q_PROPERTY(QString penStyle READ penStyle WRITE setPenStyle NOTIFY styleChanged)
    Q_PROPERTY(qreal smoothing MEMBER m_smoothing NOTIFY styleChanged)        // 0 = raw, 1 = calm
    Q_PROPERTY(bool shapeSnap MEMBER m_shapeSnap NOTIFY styleChanged)
    Q_PROPERTY(bool scratchOut MEMBER m_scratchOut NOTIFY styleChanged)   // scribble over ink to rub it out
    Q_PROPERTY(QString lastShape READ lastShape NOTIFY statsChanged)
    Q_PROPERTY(QString stats READ stats NOTIFY statsChanged)
    Q_PROPERTY(bool hasBackground READ hasBackground NOTIFY backgroundChanged)
    Q_PROPERTY(qreal backgroundScale READ backgroundScale NOTIFY backgroundChanged)
    Q_PROPERTY(bool hasTextSelection READ hasTextSelection NOTIFY selectionChanged)
    Q_PROPERTY(int wordCount READ wordCount NOTIFY selectionChanged)
    Q_PROPERTY(qint64 recordingId MEMBER m_recordingId NOTIFY styleChanged)      // stamps new strokes while recording
    Q_PROPERTY(qint64 recordingEpochMs MEMBER m_recordingEpochMs NOTIFY styleChanged)
    Q_PROPERTY(bool seekMode MEMBER m_seekMode NOTIFY styleChanged)             // tap ink → strokeTapped()
    Q_PROPERTY(qint64 replayRecordingId READ replayRecordingId WRITE setReplayRecordingId NOTIFY styleChanged)
    Q_PROPERTY(qint64 replayMs READ replayMs WRITE setReplayMs NOTIFY styleChanged)
    Q_PROPERTY(int touchIgnored READ touchIgnored NOTIFY statsChanged)
public:
    explicit InkCanvas(QQuickItem *parent = nullptr);
    ~InkCanvas() override;

    QString tool() const;
    void setTool(const QString &t);
    qreal zoom() const { return m_zoom; }
    void setZoom(qreal z);
    QPointF pan() const { return m_pan; }
    void setPan(QPointF p);
    bool infinite() const { return m_infinite; }
    void setInfinite(bool v);
    QSizeF pageSize() const { return m_pageSize; }
    void setPageSize(QSizeF s);
    QString pageStyle() const;
    void setPageStyle(const QString &style);
    bool canUndo() const { return m_undo.canUndo(); }
    bool canRedo() const { return m_undo.canRedo(); }
    bool hasSelection() const { return !m_selection.isEmpty(); }
    bool inking() const { return m_inking; }
    bool penNear() const { return m_penNear; }
    int strokeCount() const { return m_doc.count(); }
    qreal pressureCeiling() const { return m_curve.ceiling; }
    void setPressureCeiling(qreal c);
    QString penStyle() const;
    void setPenStyle(const QString &s);
    QString lastShape() const { return m_lastShape; }
    QString stats() const { return m_stats; }
    int touchIgnored() const { return m_touchIgnored; }
    InkDocument *document() { return &m_doc; }

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE void deleteSelection();
    Q_INVOKABLE void restyleSelection(const QColor &colour, qreal widthScale = 1.0, qreal widthAbs = 0);
    Q_INVOKABLE void setSelectionWidth(qreal w);
    Q_INVOKABLE qreal selectionWidth() const;
    Q_INVOKABLE QString selectionColour() const;   // recolour / thicken what the lasso caught
    Q_INVOKABLE void copySelection();
    Q_INVOKABLE void cutSelection();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void selectNone();
    Q_INVOKABLE void fitPage();
    Q_INVOKABLE void fitWidth();
    Q_INVOKABLE void zoomAt(qreal factor, QPointF screenCenter);
    Q_INVOKABLE void addBenchmarkStrokes(int count);   // 10 000-stroke budget check
    Q_INVOKABLE void resetHistory();                    // before a page load: undo stack, selection, gesture state
    Q_INVOKABLE void setBackground(const QString &file, qreal renderedScale);   // PDF page raster (page-sized)
    Q_INVOKABLE void clearBackground();
    Q_INVOKABLE void setWords(const QVariantList &words); // [[x0,y0,x1,y1,text,block,line,word], …] page units
    Q_INVOKABLE QString selectedText() const;
    Q_INVOKABLE QString renderSelectionToPng(qreal scale = 2.0) const;   // lasso selection → cache PNG
    Q_INVOKABLE QVariantList selectionStrokeIds() const;
    Q_INVOKABLE QRectF selectionBounds() const { return m_selectionXf.mapRect(m_selectionBounds); }
    Q_INVOKABLE void flashRect(const QRectF &pageRect, int ms = 2500);   // highlight a search/OCR hit
    Q_INVOKABLE void copyText();
    bool hasBackground() const { return m_hasBackground; }
    qint64 replayRecordingId() const { return m_replayRecordingId; }
    void setReplayRecordingId(qint64 id);
    qint64 replayMs() const { return m_replayMs; }
    void setReplayMs(qint64 ms);
    qreal backgroundScale() const { return m_backgroundScale; }
    bool hasTextSelection() const { return m_textSelA >= 0 && m_textSelB >= 0; }
    int wordCount() const { return m_words.size(); }
    Q_INVOKABLE QPointF toPage(QPointF screen) const { return (screen - m_pan) / m_zoom; }
    Q_INVOKABLE QPointF toScreen(QPointF page) const { return page * m_zoom + m_pan; }
    Q_INVOKABLE QPointF viewCentrePage() const { return toPage(QPointF(width() / 2, height() / 2)); }
    Q_INVOKABLE void setImages(const QVariantList &images);
    // The floating toolbar and the audio bar cover the page; fitting has to allow for them or the
    // top and bottom of every page — where the date and the question number go — sit under chrome.
    // Their own signal, not viewChanged: fitPage() emits viewChanged, and a re-fit on that would
    // call itself forever (the control sweep caught exactly that).
    Q_PROPERTY(qreal topInset MEMBER m_topInset NOTIFY insetsChanged)
    Q_PROPERTY(qreal bottomInset MEMBER m_bottomInset NOTIFY insetsChanged)   // [{id, path, x, y, w, h}] in page units

    // TabletSink
    bool tabletSample(const TabletSample &s) override;
    void tabletProximity(bool entering, const TabletSample &s) override;
    bool wantsPoint(const QPointF &windowPos) const override { return isVisible() && isEnabled() && contains(mapFromScene(windowPos)); }

signals:
    void toolChanged();
    void tapped(QPointF page);   // text-block tool: where to put a new block
    void strokeTapped(qint64 recordingId, qint64 absMs);   // seek mode
    void backgroundChanged();
    void styleChanged();
    void viewChanged();
    void insetsChanged();
    void pagePressed();          // a press the canvas itself owns: any object selection is over
    void undoChanged();
    void selectionChanged();
    void inkingChanged();
    void documentChanged();
    void statsChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void touchEvent(QTouchEvent *event) override;
    void touchUngrabEvent() override;
    void mouseUngrabEvent() override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void geometryChange(const QRectF &n, const QRectF &o) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    enum class Tool { Pen, Highlighter, Eraser, Lasso, Text, TextBlock, Shape };
    struct Word { QRectF box; QString text; int block, line; };
    enum class PageStyle { Plain, Lined, Dotted, Grid, Cornell, Graph, Isometric, Music };
    enum class Gesture { None, Ink, Erase, Lasso, MoveSelection, ScaleSelection, TextSelect };
    struct Chunk {
        QSGGeometryNode *node = nullptr;
        QVector<quint64> ids;
        int vertices = 0;
        bool dirty = true;
        InkTool layer = InkTool::Pen;
    };

    // input → tools
    void pointerPress(QPointF local, float pressure, float tiltX, float tiltY, quint64 tMs, bool eraserNow);
    void pointerMove(QPointF local, float pressure, float tiltX, float tiltY, quint64 tMs, bool tipDown, bool eraserNow);
    void pointerRelease(QPointF local, quint64 tMs);
    Tool effectiveTool() const { return m_buttonHeld ? Tool::Eraser : m_tool; }
    void beginStroke(QPointF page, float pressure, float tiltX, float tiltY, quint64 tMs);
    void extendStroke(QPointF page, float pressure, float tiltX, float tiltY, quint64 tMs);
    void endStroke();
    bool snapHighlightToWords();
    int wordAt(QPointF page) const;
    void textSelectTo(QPointF page);
    void eraseAt(QPointF page);          // sweeps from the previous eraser position
    void eraseCircle(QPointF page, float r);
    void finishErase();
    void finishLasso();
    void setSelection(QVector<quint64> ids);
    int handleAt(QPointF local) const; // -1 none, 0..3 corners
    void commitSelectionTransform();
    void setInking(bool v);
    void abandonGesture();
    void applyBackground(const QImage &img, qreal renderedScale, int gen);
    void applyImage(qint64 id, const QImage &img);
    bool scratchOutErases();
    bool pointerBelongsToChrome(QPointF windowPos) const;   // one rule for pen, finger and mouse
    void checkHoldStill();
    void markAllChunksDirty();
    float smoothingSpacing() const;

    // scene graph
    void onStrokeAdded(quint64 id, int index);
    void onStrokeRemoved(quint64 id, int index);
    void onStrokesChanged(const QVector<quint64> &ids);
    void onCleared();
    void rebuildChunk(Chunk &c);
    QSGGeometryNode *makeStripNode(bool vertexColor);
    void fillFlat(QSGGeometryNode *node, const QVector<InkVertex> &strip, QColor color);
    void buildPageNode(QSGNode *parent);
    void buildDots(QSGGeometryNode *node);
    bool isHidden(quint64 id) const;
    void markReplayChunksDirty();

    InkDocument m_doc;
    UndoStack m_undo;
    PressureCurve m_curve = PressureCurve::forStyle(PenStyle::Classic);
    PenStyle m_penStyle = PenStyle::Classic;
    qreal m_smoothing = 0.0;   // 0 = raw samples (what the maker approved); >0 enables the 1€ filter
    bool m_shapeSnap = true;
    bool m_scratchOut = true;
    bool m_straightEdge = false;      // Shift held: the stroke follows a ruler
    QString m_lastShape;
    OneEuroFilter m_fx, m_fy;
    float m_pressureEma = 0;
    QTimer *m_holdTimer = nullptr;
    QPointF m_holdAnchor;
    qint64 m_holdSinceMs = -1;
    bool m_shapeSnapped = false;
    QVector<InkPoint> m_rawBeforeSnap;

    // style
    Tool m_tool = Tool::Pen;
    QColor m_penColor = QColor(0x18, 0x21, 0x2B);
    qreal m_penWidth = 1.5;
    QColor m_highlighterColor = QColor(0xFF, 0xE0, 0x3E);
    qreal m_highlighterWidth = 22.0;
    qreal m_highlighterOpacity = 0.35;
    qreal m_eraserRadius = 12.0;
    bool m_pixelEraser = false;
    QColor m_paperColor = Qt::white;
    QColor m_dotColor = QColor(0, 0, 0, 40);
    QColor m_frameColor = QColor(0, 0, 0, 60);
    QColor m_accentColor = QColor(0x3D, 0x7B, 0xE0);
    int m_palmRejectMs = 500;
    bool m_predictionEnabled = true;
    qreal m_predictionMs = 16.7;

    // view
    qreal m_zoom = 1.0;
    QPointF m_pan;
    bool m_infinite = false;
    PageStyle m_pageStyle = PageStyle::Dotted;
    QSizeF m_pageSize = QSizeF(794, 1123); // A4 at 96 dpi
    bool m_viewDirty = true;
    bool m_fitPending = false;
    bool m_fitted = false;
    qreal m_topInset = 20;
    qreal m_bottomInset = 20;      // last view came from fitPage(): re-fit on resize until the user zooms/pans
    float m_builtSpacing = 2.f;

    // gesture state
    QTimer m_spacingSettle;             // re-tessellate once a zoom gesture has stopped moving
    Gesture m_gesture = Gesture::None;
    Stroke m_active;
    quint64 m_strokeT0 = 0;
    bool m_inking = false;
    bool m_penNear = false;
    qint64 m_penLeftAt = -1;
    bool m_buttonHeld = false;
    qint64 m_lastButtonPressMs = -1;
    bool m_buttonUsedForErase = false;
    QSet<quint64> m_hidden;            // hidden while a gesture decides their fate
    QVector<quint64> m_eraseIds;        // stroke eraser gesture
    QPointF m_eraseLast;
    bool m_eraseLastValid = false;
    QHash<quint64, QVector<Stroke>> m_pixelWork; // pixel eraser gesture: original → pieces
    QPolygonF m_lasso;
    QVector<quint64> m_selection;
    QRectF m_selectionBounds;           // page units, at selection time
    QTransform m_selectionXf;           // live transform preview
    QPointF m_dragStartPage;
    QPointF m_pressPage; bool m_pressMoved = false;
    int m_activeHandle = -1;
    QVector<Stroke> m_clipboard;
    bool m_mouseDown = false;
    QPointF m_hoverLocal;
    bool m_hoverValid = false;

    // touch
    QHash<int, QPointF> m_touchPts;     // id → local
    QPointF m_touchStartPan;
    qreal m_touchStartZoom = 1;
    QPointF m_touchStartCentroid;
    qreal m_touchStartDist = 0;
    int m_touchIgnored = 0;
    qint64 m_lastTwoFingerTapMs = -1; qint64 m_twoFingerDownMs = -1; QPointF m_twoFingerDownAt; bool m_twoFingerMoved = false;

    // scene graph
    QSGTransformNode *m_viewNode = nullptr;
    QSGNode *m_pageNode = nullptr;
    QSGGeometryNode *m_paperRect = nullptr;
    QSGGeometryNode *m_dotsNode = nullptr;
    QSGGeometryNode *m_pageFrame = nullptr;
    class QSGClipNode *m_inkClip = nullptr;
    QSGNode *m_highlighterLayer = nullptr;
    QSGNode *m_inkLayer = nullptr;
    QSGGeometryNode *m_previewNode = nullptr;   // pixel-eraser pieces
    QSGTransformNode *m_selectionNode = nullptr;
    QSGGeometryNode *m_selectionInk = nullptr;
    QSGGeometryNode *m_selectionBox = nullptr;
    QSGGeometryNode *m_selectionHandles = nullptr;
    QVector<QSGNode *> m_orphanNodes;
    QSGGeometryNode *m_liveNode = nullptr;
    QSGGeometryNode *m_lassoNode = nullptr;
    QSGGeometryNode *m_cursorNode = nullptr;
    QSGGeometryNode *m_textSelNode = nullptr;
    QSGGeometryNode *m_flashNode = nullptr;
    QRectF m_flashRect; qint64 m_flashUntilMs = 0; QTimer *m_flashTimer = nullptr;
    class QSGSimpleTextureNode *m_backgroundNode = nullptr;
    class QSGTexture *m_backgroundTexture = nullptr;
    QImage m_pendingBackground;
    bool m_backgroundDirty = false;
    bool m_hasBackground = false;
    struct PageImage {
        qint64 id = 0;
        QString path;
        QRectF rect;
        QImage pending;          // decoded, waiting for the render thread to make a texture
        bool needsTexture = false;
        class QSGTexture *texture = nullptr;
        class QSGSimpleTextureNode *node = nullptr;
    };
    QVector<PageImage> m_images;
    QSGNode *m_imagesNode = nullptr;
    bool m_imagesDirty = false;
    qreal m_backgroundScale = 0;
    int m_backgroundGeneration = 0;
    QVector<Word> m_words;
    qint64 m_recordingId = 0, m_recordingEpochMs = 0;
    bool m_seekMode = false;
    qint64 m_replayRecordingId = 0, m_replayMs = -1;
    int m_textSelA = -1, m_textSelB = -1;
    bool m_textSelDirty = true;
    QVector<Chunk> m_chunks;
    QHash<quint64, int> m_chunkOf;
    QSet<int> m_dirtyChunks;
    bool m_rebuildAll = true;
    bool m_selectionDirty = true;
    bool m_previewDirty = false;
    QRectF m_dotsRegion;                // page region the dots node currently covers
    qreal m_dotsZoom = 0;

    // stats
    QElapsedTimer m_clock;
    qint64 m_lastSampleArrivalUs = -1;
    qint64 m_pendingArrivalUs = -1;
    qint64 m_pendingSetUs = -1;
    std::atomic<qint64> m_lastSwapUs{-1};
    QVector<double> m_latencies;
    int m_samplesThisSecond = 0;
    QTimer *m_statsTimer = nullptr;
    QString m_stats;
    double m_lastRebuildMs = 0;
    int m_framesThisSecond = 0;
};
