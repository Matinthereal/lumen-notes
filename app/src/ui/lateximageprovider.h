#pragma once
#include <QQuickAsyncImageProvider>
#include <QMutex>

class WorkerSupervisor;

// image://latex/<url-encoded tex>?c=<colour>  → PNG from the latex worker, rendered off the GUI
// thread and cached on disk. Used by Markdown blocks: $…$ spans become inline images.
class LatexImageProvider : public QQuickAsyncImageProvider {
public:
    explicit LatexImageProvider(WorkerSupervisor *worker);
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
private:
    WorkerSupervisor *m_worker;
};
