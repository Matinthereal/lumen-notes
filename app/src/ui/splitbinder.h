#pragma once
#include <QObject>
#include <QPointer>

class InkCanvas;
class PageStore;
class TabletEventFilter;

// Split view's right-hand canvas exists only while the split is open, so it cannot be wired up at
// start like the main one. QML hands it over here when it is made and takes it back before it goes:
// the pen is routed to it, and its ink is journaled and saved by its own PageStore.
class SplitBinder : public QObject {
    Q_OBJECT
public:
    SplitBinder(TabletEventFilter &filter, PageStore &store, QObject *parent = nullptr);
    Q_INVOKABLE void attach(QObject *canvas);
    Q_INVOKABLE void detach(QObject *canvas = nullptr);   // only if it is still the attached one

private:
    TabletEventFilter &m_filter;
    PageStore &m_store;
    QPointer<InkCanvas> m_canvas;
};
