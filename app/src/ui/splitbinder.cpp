#include "splitbinder.h"
#include "canvas/inkcanvas.h"
#include "input/tableteventfilter.h"
#include "storage/pagestore.h"

SplitBinder::SplitBinder(TabletEventFilter &filter, PageStore &store, QObject *parent)
    : QObject(parent), m_filter(filter), m_store(store) {}

void SplitBinder::attach(QObject *canvas)
{
    auto *c = qobject_cast<InkCanvas *>(canvas);
    if (!c || c == m_canvas) return;
    detach();
    m_canvas = c;
    m_filter.addSink(c);
    m_store.attach(c->document());
}

void SplitBinder::detach(QObject *canvas)
{
    if (!m_canvas || (canvas && canvas != m_canvas)) return;
    m_filter.removeSink(m_canvas);
    m_store.detach();
    m_canvas = nullptr;
}
