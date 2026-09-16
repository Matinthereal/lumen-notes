#pragma once
#include <QObject>
#include <QTransform>
#include <QVector>
#include <memory>
#include <vector>
#include "inktypes.h"

class InkDocument;

class InkCommand {
public:
    virtual ~InkCommand() = default;
    virtual void redo(InkDocument &doc) = 0;
    virtual void undo(InkDocument &doc) = 0;
};

class AddStrokeCommand : public InkCommand {
public:
    explicit AddStrokeCommand(Stroke s) : m_stroke(std::move(s)) {}
    void redo(InkDocument &doc) override;
    void undo(InkDocument &doc) override;
    quint64 id() const { return m_stroke.id; }
private:
    Stroke m_stroke;
    int m_index = -1;
};

class RemoveStrokesCommand : public InkCommand {
public:
    explicit RemoveStrokesCommand(QVector<quint64> ids) : m_ids(std::move(ids)) {}
    void redo(InkDocument &doc) override;
    void undo(InkDocument &doc) override;
private:
    QVector<quint64> m_ids;
    QVector<std::pair<int, Stroke>> m_removed; // ascending index
};

// Pixel eraser: originals out, pieces in. Ids of the pieces are stable across undo/redo.
class ReplaceStrokesCommand : public InkCommand {
public:
    ReplaceStrokesCommand(QVector<quint64> removeIds, QVector<Stroke> add) : m_ids(std::move(removeIds)), m_added(std::move(add)) {}
    void redo(InkDocument &doc) override;
    void undo(InkDocument &doc) override;
private:
    QVector<quint64> m_ids;
    QVector<std::pair<int, Stroke>> m_removed;
    QVector<Stroke> m_added;
};

class TransformStrokesCommand : public InkCommand {
public:
    TransformStrokesCommand(QVector<quint64> ids, const QTransform &t) : m_ids(std::move(ids)), m_t(t) {}
    void redo(InkDocument &doc) override;
    void undo(InkDocument &doc) override;
private:
    QVector<quint64> m_ids;
    QTransform m_t;
};

class UndoStack : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
public:
    explicit UndoStack(InkDocument *doc, QObject *parent = nullptr);
    void push(std::unique_ptr<InkCommand> cmd); // executes it
    void undo();
    void redo();
    void clear();
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    int undoDepth() const { return int(m_undo.size()); }
signals:
    void changed();
private:
    InkDocument *m_doc;
    std::vector<std::unique_ptr<InkCommand>> m_undo, m_redo;
};
