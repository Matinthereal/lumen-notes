#include "undostack.h"
#include "inkdocument.h"
#include <algorithm>

void AddStrokeCommand::redo(InkDocument &doc)
{
    if (m_stroke.id == 0) m_stroke.id = doc.nextId();
    m_index = m_index < 0 ? doc.count() : std::min(m_index, doc.count());
    doc.insertStroke(m_index, m_stroke);
}
void AddStrokeCommand::undo(InkDocument &doc) { doc.removeStroke(m_stroke.id, nullptr, &m_index); }

void RemoveStrokesCommand::redo(InkDocument &doc)
{
    m_removed = doc.removeStrokes(m_ids);   // already in ascending index order
}
void RemoveStrokesCommand::undo(InkDocument &doc)
{
    for (const auto &[idx, s] : m_removed) doc.insertStroke(idx, s);
}

void ReplaceStrokesCommand::redo(InkDocument &doc)
{
    m_removed = doc.removeStrokes(m_ids);
    for (Stroke &s : m_added) {
        if (s.id == 0) s.id = doc.nextId();
        doc.addStroke(s);
    }
}
void ReplaceStrokesCommand::undo(InkDocument &doc)
{
    for (const Stroke &s : m_added) doc.removeStroke(s.id);
    for (const auto &[idx, s] : m_removed) doc.insertStroke(idx, s);
}

void TransformStrokesCommand::redo(InkDocument &doc) { doc.transformStrokes(m_ids, m_t); }
void TransformStrokesCommand::undo(InkDocument &doc) { doc.transformStrokes(m_ids, m_t.inverted()); }

UndoStack::UndoStack(InkDocument *doc, QObject *parent) : QObject(parent), m_doc(doc) {}

void UndoStack::push(std::unique_ptr<InkCommand> cmd)
{
    cmd->redo(*m_doc);
    m_undo.push_back(std::move(cmd));
    m_redo.clear();
    // Bounded, because a removal command holds full copies of the strokes it took out — but the
    // old 300 was small enough to hit in one lesson, against a spec that promises "unlimited
    // in-session". Xournal++ and Write are both unbounded; this is the compromise.
    if (m_undo.size() > 2000) m_undo.erase(m_undo.begin(), m_undo.begin() + 200);
    emit changed();
}
void UndoStack::undo()
{
    if (m_undo.empty()) return;
    auto cmd = std::move(m_undo.back()); m_undo.pop_back();
    cmd->undo(*m_doc);
    m_redo.push_back(std::move(cmd));
    emit changed();
}
void UndoStack::redo()
{
    if (m_redo.empty()) return;
    auto cmd = std::move(m_redo.back()); m_redo.pop_back();
    cmd->redo(*m_doc);
    m_undo.push_back(std::move(cmd));
    emit changed();
}
void UndoStack::clear() { m_undo.clear(); m_redo.clear(); emit changed(); }
