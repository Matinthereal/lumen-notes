#pragma once
#include <QObject>
#include <QList>
#include <QPointer>
#include "tabletsample.h"

class QWindow;
class QTabletEvent;

// Installs on the application (proximity events are sent to the app object, not to windows) and
// on the window (press/move/release). Forwards to the TabletSink under the pen: a press picks the
// sink, and the moves and release of that stroke follow it wherever the pen goes. Never lets a tablet
// event turn into a mouse event: main() also clears AA_SynthesizeMouseForUnhandledTabletEvents.
class TabletEventFilter : public QObject {
    Q_OBJECT
public:
    explicit TabletEventFilter(QObject *parent = nullptr);
    void attachWindow(QWindow *window);
    void setSink(TabletSink *sink) { m_sinks.clear(); if (sink) m_sinks.append(sink); m_active = nullptr; }
    void addSink(TabletSink *sink) { if (sink && !m_sinks.contains(sink)) m_sinks.append(sink); }
    void removeSink(TabletSink *sink) { m_sinks.removeAll(sink); if (m_active == sink) m_active = nullptr; }

    static TabletSample fromEvent(const QTabletEvent *e);

signals:
    void sampleSeen(const TabletSample &s); // for logging/observers; the sink gets it first

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPointer<QWindow> m_window;
    QList<TabletSink *> m_sinks;
    TabletSink *m_active = nullptr;     // the sink that took the press, until the release
};
