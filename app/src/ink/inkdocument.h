#pragma once
#include <QHash>
#include <QObject>
#include <QPolygonF>
#include <QTransform>
#include <QVector>
#include "inktypes.h"

// The strokes of one page, in insertion order, with the spatial queries the tools need.
// Pure data + signals; rendering listens, the undo stack drives it. No I/O here (Phase 2 adds it).
class InkDocument : public QObject {
    Q_OBJECT
public:
    explicit InkDocument(QObject *parent = nullptr);

    int count() const { return m_strokes.size(); }
    const QVector<Stroke> &strokes() const { return m_strokes; }
    const Stroke *stroke(quint64 id) const;
    int indexOf(quint64 id) const { return m_index.value(id, -1); }
    quint64 nextId() { return m_nextId++; }

    quint64 addStroke(Stroke s);                       // appends; assigns an id when 0
    void insertStroke(int index, Stroke s);            // restores order on undo
    bool removeStroke(quint64 id, Stroke *removed = nullptr, int *index = nullptr);
    QVector<std::pair<int, Stroke>> removeStrokes(const QVector<quint64> &ids);   // one reindex for many
    void transformStrokes(const QVector<quint64> &ids, const QTransform &t);
    bool updateStroke(const Stroke &s);              // replace a stroke's data in place (journal replay)
    void clear();

    QVector<quint64> hitCircle(QPointF c, float r) const;                         // eraser / tap
    QVector<quint64> insidePolygon(const QPolygonF &poly, float minFraction = 0.6f) const; // lasso
    QRectF boundsOf(const QVector<quint64> &ids) const;
    static QVector<Stroke> splitByCircle(const Stroke &s, QPointF c, float r);   // pixel eraser

signals:
    void strokeAdded(quint64 id, int index);
    void strokeRemoved(quint64 id, int index);
    void strokesChanged(const QVector<quint64> &ids);
    void cleared();

private:
    void reindexFrom(int index);
    QVector<Stroke> m_strokes;
    QHash<quint64, int> m_index;
    quint64 m_nextId = 1;
};
