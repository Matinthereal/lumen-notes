#include "lateximageprovider.h"
#include "storage/paths.h"
#include "workers/workersupervisor.h"
#include <QImage>
#include <QJsonObject>
#include <QMetaObject>
#include <QQuickTextureFactory>
#include <QUrl>
#include <QUrlQuery>

namespace {
class Response : public QQuickImageResponse {
public:
    Response(WorkerSupervisor *worker, const QString &tex, const QString &color, const QSize &size) : m_size(size)
    {
        // The worker lives on the GUI thread; hop there to send, results come back as a signal.
        QMetaObject::invokeMethod(worker, [this, worker, tex, color] {
            m_conn = QObject::connect(worker, &WorkerSupervisor::response, worker, [this, worker](int id, const QJsonObject &r, const QJsonObject &e) {
                if (id != m_id) return;
                QObject::disconnect(m_conn);
                if (e.isEmpty()) m_image = QImage(r.value("file").toString());
                if (m_image.isNull()) m_error = e.isEmpty() ? QStringLiteral("render failed") : e.value("message").toString();
                if (!m_image.isNull() && m_size.isValid()) m_image = m_image.scaled(m_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                emit finished();
            });
            m_id = worker->request(QStringLiteral("render"), {{"tex", tex}, {"out_dir", paths::cacheDir() + "/latex"}, {"dpi", 200}, {"color", color}});
        }, Qt::QueuedConnection);
    }
    QQuickTextureFactory *textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }
    QString errorString() const override { return m_error; }
private:
    QSize m_size;
    QImage m_image;
    QString m_error;
    int m_id = -1;
    QMetaObject::Connection m_conn;
};
}

LatexImageProvider::LatexImageProvider(WorkerSupervisor *worker) : m_worker(worker) {}

QQuickImageResponse *LatexImageProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    // id = "<percent-encoded tex>?c=<hex colour>"
    const int q = id.lastIndexOf("?c=");
    const QString tex = QUrl::fromPercentEncoding(id.left(q < 0 ? id.size() : q).toUtf8());
    const QString color = q < 0 ? QStringLiteral("#000000") : id.mid(q + 3);
    return new Response(m_worker, tex, color, requestedSize);
}
