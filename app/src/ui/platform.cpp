#include "platform.h"
#include "storage/paths.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileOpenEvent>

Platform::Platform(QObject *parent) : QObject(parent)
{
    QCoreApplication::instance()->installEventFilter(this);
}

QUrl Platform::shareTarget(const QString &fileName) const
{
    const QString dir = paths::cacheDir() + QStringLiteral("/share");
    QDir().mkpath(dir);
    QString name = fileName;
    name.replace(QLatin1Char('/'), QLatin1Char('-'));
    return QUrl::fromLocalFile(dir + QLatin1Char('/') + name);
}

bool Platform::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FileOpen && watched == QCoreApplication::instance()) {
        const QUrl url = static_cast<QFileOpenEvent *>(event)->url();
        if (url.isValid()) {
            emit fileOpened(readableCopy(url));
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

#ifndef Q_OS_IOS
void Platform::attach(QWindow *window) { m_window = window; }
bool Platform::canShare() const { return false; }
bool Platform::share(const QUrl &, const QRectF &) { return false; }
QUrl Platform::readableCopy(const QUrl &url) const { return url; }
#endif
