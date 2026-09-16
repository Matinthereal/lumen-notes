#pragma once
#include <QObject>
#include <QPointer>
#include "tabletsample.h"

class QWindow;
class QTabletEvent;

// Installs on the application (proximity events are sent to the app object, not to windows) and
// on the window (press/move/release). Forwards everything to one TabletSink. Never lets a tablet
// event turn into a mouse event: main() also clears AA_SynthesizeMouseForUnhandledTabletEvents.
class TabletEventFilter : public QObject {
    Q_OBJECT
public:
    explicit TabletEventFilter(QObject *parent = nullptr);
    void attachWindow(QWindow *window);
    void setSink(TabletSink *sink) { m_sink = sink; }

    static TabletSample fromEvent(const QTabletEvent *e);

signals:
    void sampleSeen(const TabletSample &s); // for logging/observers; the sink gets it first

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPointer<QWindow> m_window;
    TabletSink *m_sink = nullptr;
};
