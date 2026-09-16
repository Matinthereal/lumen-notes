#include "themeiconprovider.h"
#include <QDir>
#include <QDirIterator>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QStandardPaths>
#include <QImageReader>

// Plasma's icon theme through QIcon when the platform theme provides it; otherwise the Breeze
// SVGs straight from disk (the offscreen platform and some sessions give QIcon nothing).
namespace {
QHash<QString, QString> &fileIndex()
{
    static QHash<QString, QString> index;
    static bool built = false;
    if (!built) {
        built = true;
        QStringList roots;
        for (const QString &d : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) roots << d + "/icons/breeze";
        roots << "/usr/share/icons/breeze";
        for (const QString &root : roots) {
            if (!QDir(root).exists()) continue;
            for (const char *size : {"22", "24", "32", "16", "48"}) {
                QDirIterator it(root, {"*.svg"}, QDir::Files, QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    const QString path = it.next();
                    if (!path.contains(QStringLiteral("/%1/").arg(size))) continue;
                    const QString name = QFileInfo(path).completeBaseName();
                    if (!index.contains(name)) index.insert(name, path);
                }
            }
            if (!index.isEmpty()) break;
        }
    }
    return index;
}
}

ThemeIconProvider::ThemeIconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap)
{
    if (QIcon::themeName().isEmpty()) QIcon::setThemeName(QStringLiteral("breeze"));
    QIcon::setFallbackThemeName(QStringLiteral("breeze"));
}

QPixmap ThemeIconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    const int px = requestedSize.isValid() ? std::max(requestedSize.width(), requestedSize.height()) : 22;
    QPixmap pm = QIcon::fromTheme(id).pixmap(QSize(px, px));
    if (pm.isNull()) {
        const QString file = fileIndex().value(id, fileIndex().value(id + "-symbolic"));
        if (!file.isEmpty()) {
            QImageReader reader(file);           // the SVG image-format plugin renders it; no Qt6Svg link needed
            reader.setScaledSize(QSize(px, px));
            const QImage img = reader.read();
            if (!img.isNull()) pm = QPixmap::fromImage(img);
        }
    }
    if (pm.isNull()) {   // last resort: a neutral placeholder rather than a broken image
        pm = QPixmap(px, px); pm.fill(Qt::transparent);
        QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing); p.setPen(QPen(QColor(120, 120, 120), 1.5)); p.drawRoundedRect(3, 3, px - 6, px - 6, 3, 3);
    }
    if (size) *size = pm.size();
    return pm;
}
