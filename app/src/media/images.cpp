#include "images.h"
#include "picturepixels.h"
#include "storage/attachments.h"
#include "storage/database.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryFile>
#include <algorithm>

namespace {
// What Lumen puts on the clipboard beside the pixels: which stored file, and how it was trimmed,
// turned and sized, so a paste on another page is the same picture and not a re-saved copy of it.
const QString kOwnFormat = QStringLiteral("application/x-lumen-picture");
// Encoded pictures another program may offer, best first: PNG loses nothing, and a JPEG's own
// bytes are better than the same JPEG decoded and saved again.
const char *const kEncoded[] = {"image/png", "image/jpeg", "image/webp", "image/tiff", "image/gif", "image/bmp"};

// hasImage() alone is not enough: outside the window system's own clipboard it knows only Qt's
// decoded pixels, not a PNG offered as PNG.
bool hasPicture(const QMimeData *mime)
{
    return mime->hasImage() || std::any_of(std::begin(kEncoded), std::end(kEncoded), [mime](const char *f) { return mime->hasFormat(QLatin1String(f)); });
}

bool isPicture(QByteArray bytes)
{
    QBuffer buffer(&bytes);
    return buffer.open(QIODevice::ReadOnly) && !QImageReader::imageFormat(&buffer).isEmpty();
}

bool isPictureFile(const QUrl &url)
{
    return url.isLocalFile() && !QImageReader::imageFormat(url.toLocalFile()).isEmpty();
}
}

Images::Images(Database &db, QObject *parent) : QObject(parent), m_db(db) {}

QVariantList Images::list(qint64 pageId) const
{
    QVariantList out;
    Database::Query q(m_db, "SELECT i.id, i.attachment, a.mime, i.x, i.y, i.w, i.h, i.crop_x, i.crop_y, i.crop_w, i.crop_h, i.rotation"
                            " FROM image i JOIN attachment a ON a.sha256=i.attachment WHERE i.page_id=? ORDER BY i.id");
    q.bind(1, pageId);
    while (q.step())
        out.append(QVariantMap{{"id", q.i64(0)}, {"path", attachments::pathFor(q.text(1), q.text(2))},
                               {"x", q.f64(3)}, {"y", q.f64(4)}, {"w", q.f64(5)}, {"h", q.f64(6)},
                               {"cropX", q.f64(7)}, {"cropY", q.f64(8)}, {"cropW", q.f64(9)}, {"cropH", q.f64(10)},
                               {"rotation", q.i32(11)}});
    return out;
}

int Images::count(qint64 pageId) const
{
    Database::Query q(m_db, "SELECT COUNT(*) FROM image WHERE page_id=?");
    q.bind(1, pageId);
    return q.step() ? q.i32(0) : 0;
}

QVariantMap Images::image(qint64 id) const
{
    Database::Query q(m_db, "SELECT i.id, i.attachment, a.mime, i.x, i.y, i.w, i.h, i.page_id, i.crop_x, i.crop_y, i.crop_w, i.crop_h, i.rotation"
                            " FROM image i JOIN attachment a ON a.sha256=i.attachment WHERE i.id=?");
    q.bind(1, id);
    if (!q.step()) return {};
    return QVariantMap{{"id", q.i64(0)}, {"path", attachments::pathFor(q.text(1), q.text(2))}, {"attachment", q.text(1)},
                       {"x", q.f64(3)}, {"y", q.f64(4)}, {"w", q.f64(5)}, {"h", q.f64(6)}, {"pageId", q.i64(7)},
                       {"cropX", q.f64(8)}, {"cropY", q.f64(9)}, {"cropW", q.f64(10)}, {"cropH", q.f64(11)},
                       {"rotation", q.i32(12)}};
}

QSize Images::uprightSize(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QSize size = reader.size();
    // size() is the file's own orientation; a photo the camera marked as turned shows the other way.
    if (reader.transformation() & QImageIOHandler::TransformationRotate90) size.transpose();
    return size;
}

qint64 Images::store(qint64 pageId, const QString &sha, double x, double y, double w, double h)
{
    Database::Query q(m_db, "INSERT INTO image(page_id, attachment, x, y, w, h) VALUES (?,?,?,?,?,?)");
    q.bind(1, pageId).bind(2, sha).bind(3, x).bind(4, y).bind(5, w).bind(6, h);
    if (!q.run()) { emit failed(QStringLiteral("could not place the picture")); return 0; }
    const qint64 id = m_db.lastInsertId();
    emit changed(pageId);
    return id;
}

qint64 Images::insertStored(qint64 pageId, const QString &sha, const QString &, QSize pixels, double x, double y, double maxWidth)
{
    if (sha.isEmpty() || pixels.isEmpty()) return 0;
    // Land at a sensible size: never wider than maxWidth, never upscaled past its own pixels.
    const double w = std::min<double>(maxWidth, pixels.width());
    return store(pageId, sha, x, y, w, w * double(pixels.height()) / double(pixels.width()));
}

QString Images::storeFile(const QString &path, QSize *pixels)
{
    QImageReader reader(path);
    *pixels = uprightSize(path);
    if (!reader.canRead() || pixels->isEmpty()) { emit failed(QStringLiteral("that file is not a picture I can read")); return {}; }
    QString err;
    const QString sha = attachments::store(m_db, path, QMimeDatabase().mimeTypeForFile(path).name(), &err);
    if (sha.isEmpty()) emit failed(err.isEmpty() ? QStringLiteral("could not copy the picture") : err);
    return sha;
}

qint64 Images::insertFile(qint64 pageId, const QUrl &file, double x, double y, double maxWidth)
{
    if (!pageId) return 0;
    QSize pixels;
    const QString sha = storeFile(file.isLocalFile() ? file.toLocalFile() : file.toString(), &pixels);
    return sha.isEmpty() ? 0 : insertStored(pageId, sha, {}, pixels, x, y, maxWidth);
}

static QSizeF fitted(QSize pixels, double maxW, double maxH)
{
    const double k = std::min({1.0, maxW / pixels.width(), maxH / pixels.height()});
    return QSizeF(pixels.width() * k, pixels.height() * k);
}

qint64 Images::place(qint64 pageId, const QUrl &file, double cx, double cy, double maxW, double maxH)
{
    if (!pageId) return 0;
    QSize pixels;
    const QString sha = storeFile(file.isLocalFile() ? file.toLocalFile() : file.toString(), &pixels);
    if (sha.isEmpty()) return 0;
    const QSizeF s = fitted(pixels, maxW, maxH);
    return store(pageId, sha, cx - s.width() / 2, cy - s.height() / 2, s.width(), s.height());
}

qint64 Images::placeStored(qint64 pageId, const QString &sha, QSize pixels, double cx, double cy, double maxW, double maxH)
{
    if (sha.isEmpty() || pixels.isEmpty()) return 0;
    const QSizeF s = fitted(pixels, maxW, maxH);
    return store(pageId, sha, cx - s.width() / 2, cy - s.height() / 2, s.width(), s.height());
}

qint64 Images::placeClipboard(qint64 pageId, double cx, double cy, double maxW, double maxH)
{
    const QVariantList placed = pasteClipboard(pageId, cx, cy, maxW, maxH);
    return placed.isEmpty() ? 0 : placed.last().toLongLong();
}

QVariantList Images::pasteClipboard(qint64 pageId, double cx, double cy, double maxW, double maxH)
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!pageId || !mime) return {};
    // Lumen's own copy: the same stored file again, as it was trimmed and turned.
    if (mime->hasFormat(kOwnFormat)) {
        QVariantMap pic = QJsonDocument::fromJson(mime->data(kOwnFormat)).object().toVariantMap();
        Database::Query known(m_db, "SELECT mime FROM attachment WHERE sha256=?");
        known.bind(1, pic.value(QStringLiteral("attachment")).toString());
        if (known.step() && QFileInfo::exists(attachments::pathFor(pic.value(QStringLiteral("attachment")).toString(), known.text(0)))) {
            pic.insert(QStringLiteral("pageId"), pageId);
            pic.insert(QStringLiteral("x"), cx - pic.value(QStringLiteral("w")).toDouble() / 2);
            pic.insert(QStringLiteral("y"), cy - pic.value(QStringLiteral("h")).toDouble() / 2);
            if (const qint64 id = restore(pic)) return {id};
        }
        // Copied from another library, or its file has gone: the pixels are on the clipboard too.
    }
    if (hasPicture(mime)) {
        QSize pixels;
        const QString sha = storeMime(mime, &pixels);
        const qint64 id = placeStored(pageId, sha, pixels, cx, cy, maxW, maxH);
        return id ? QVariantList{id} : QVariantList{};
    }
    QVariantList placed;
    for (const QUrl &url : mime->urls()) {
        if (!isPictureFile(url)) continue;
        // More than one at once: each a little below and right of the one before, like a fan of cards.
        const double step = 28.0 * placed.size();
        if (const qint64 id = place(pageId, url, cx + step, cy + step, maxW, maxH)) placed.append(id);
    }
    return placed;
}

bool Images::copy(qint64 id)
{
    const QVariantMap pic = image(id);
    if (pic.isEmpty()) return false;
    const QString path = pic.value(QStringLiteral("path")).toString();
    const QRectF crop(pic.value(QStringLiteral("cropX")).toDouble(), pic.value(QStringLiteral("cropY")).toDouble(),
                      pic.value(QStringLiteral("cropW")).toDouble(), pic.value(QStringLiteral("cropH")).toDouble());
    const int rotation = pic.value(QStringLiteral("rotation")).toInt();
    const QImage shown = picturepixels::load(path, rotation, crop);
    if (shown.isNull()) { emit failed(QStringLiteral("could not read the picture to copy it")); return false; }
    auto *mime = new QMimeData;
    mime->setImageData(shown);
    // Untouched, the stored file is the best copy there is: a program that asks for that format
    // gets the photo's own bytes, not a second encoding of it.
    if (rotation == 0 && crop == QRectF(0, 0, 1, 1)) {
        const QString type = QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchContent).name();
        QFile file(path);
        if (std::find(std::begin(kEncoded), std::end(kEncoded), type) != std::end(kEncoded) && file.open(QIODevice::ReadOnly))
            mime->setData(type, file.readAll());
    }
    QJsonObject own;
    for (const char *key : {"attachment", "cropX", "cropY", "cropW", "cropH", "rotation", "w", "h"})
        own.insert(QLatin1String(key), QJsonValue::fromVariant(pic.value(QLatin1String(key))));
    mime->setData(kOwnFormat, QJsonDocument(own).toJson(QJsonDocument::Compact));
    QGuiApplication::clipboard()->setMimeData(mime);
    return true;
}

qint64 Images::duplicate(qint64 id, double dx, double dy)
{
    QVariantMap pic = image(id);
    if (pic.isEmpty()) return 0;
    pic.insert(QStringLiteral("x"), pic.value(QStringLiteral("x")).toDouble() + dx);
    pic.insert(QStringLiteral("y"), pic.value(QStringLiteral("y")).toDouble() + dy);
    return restore(pic);
}

qint64 Images::placeData(qint64 pageId, const QByteArray &bytes, double cx, double cy, double maxW, double maxH)
{
    if (!pageId) return 0;
    QSize pixels;
    const QString sha = storeBytes(bytes, &pixels);
    return placeStored(pageId, sha, pixels, cx, cy, maxW, maxH);
}

void Images::fetch(qint64 pageId, const QUrl &url, double cx, double cy, double maxW, double maxH)
{
    if (!pageId) return;
    if (url.scheme() == QLatin1String("data")) {
        // data:image/png;base64,…  — the picture is the link.
        const QByteArray body = url.toString(QUrl::FullyEncoded).toLatin1().mid(5);
        const int comma = body.indexOf(',');
        const QByteArray payload = QByteArray::fromPercentEncoding(body.mid(comma + 1));
        const qint64 id = placeData(pageId, body.left(std::max(0, comma)).endsWith(";base64") ? QByteArray::fromBase64(payload) : payload, cx, cy, maxW, maxH);
        if (id) emit fetched(pageId, id);
        return;
    }
    if (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")) {
        emit failed(QStringLiteral("that link is not a picture I can fetch"));
        return;
    }
    if (!m_network) m_network = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(20000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Lumen/%1").arg(QCoreApplication::applicationVersion()));
    QNetworkReply *reply = m_network->get(request);
    // Stop at the limit rather than fill memory with whatever the link turns out to be: as soon
    // as the server says how much is coming, and again as it arrives in case it did not say.
    const auto stopIfTooBig = [reply](qint64 bytes) {
        if (bytes <= kFetchLimit || reply->property("tooBig").toBool()) return;
        reply->setProperty("tooBig", true);
        reply->abort();
    };
    connect(reply, &QNetworkReply::metaDataChanged, reply, [reply, stopIfTooBig] { stopIfTooBig(reply->header(QNetworkRequest::ContentLengthHeader).toLongLong()); });
    connect(reply, &QNetworkReply::downloadProgress, reply, [stopIfTooBig](qint64 received, qint64 total) { stopIfTooBig(std::max(received, total)); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, pageId, cx, cy, maxW, maxH] {
        reply->deleteLater();
        if (reply->property("tooBig").toBool()) { emit failed(QStringLiteral("that picture is too big to fetch (over 40 MB)")); return; }
        if (reply->error() != QNetworkReply::NoError) { emit failed(QStringLiteral("could not fetch that picture: %1").arg(reply->errorString())); return; }
        const QByteArray bytes = reply->readAll();
        if (!isPicture(bytes)) { emit failed(QStringLiteral("that link is a web page, not a picture")); return; }
        if (const qint64 id = placeData(pageId, bytes, cx, cy, maxW, maxH)) emit fetched(pageId, id);
    });
}

qint64 Images::restore(const QVariantMap &picture)
{
    const qint64 pageId = picture.value(QStringLiteral("pageId")).toLongLong();
    const QString sha = picture.value(QStringLiteral("attachment")).toString();
    if (!pageId || sha.isEmpty()) return 0;
    Database::Query q(m_db, "INSERT INTO image(page_id, attachment, x, y, w, h, crop_x, crop_y, crop_w, crop_h, rotation) VALUES (?,?,?,?,?,?,?,?,?,?,?)");
    q.bind(1, pageId).bind(2, sha)
     .bind(3, picture.value(QStringLiteral("x")).toDouble()).bind(4, picture.value(QStringLiteral("y")).toDouble())
     .bind(5, picture.value(QStringLiteral("w")).toDouble()).bind(6, picture.value(QStringLiteral("h")).toDouble())
     .bind(7, picture.value(QStringLiteral("cropX"), 0).toDouble()).bind(8, picture.value(QStringLiteral("cropY"), 0).toDouble())
     .bind(9, picture.value(QStringLiteral("cropW"), 1).toDouble()).bind(10, picture.value(QStringLiteral("cropH"), 1).toDouble())
     .bind(11, picture.value(QStringLiteral("rotation"), 0).toInt());
    if (!q.run()) return 0;
    const qint64 id = m_db.lastInsertId();
    emit changed(pageId);
    return id;
}

bool Images::clipboardHasImage() const
{
    const QClipboard *cb = QGuiApplication::clipboard();
    const QMimeData *mime = cb ? cb->mimeData() : nullptr;
    if (!mime) return false;
    if (mime->hasFormat(kOwnFormat) || hasPicture(mime)) return true;
    const QList<QUrl> urls = mime->urls();
    return std::any_of(urls.begin(), urls.end(), isPictureFile);
}

QString Images::storeBytes(const QByteArray &bytes, QSize *pixels)
{
    // Kept exactly as it came: the same bytes, so nothing is decoded and encoded again.
    QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/lumen-picture-XXXXXX"));
    if (bytes.isEmpty() || !tmp.open() || tmp.write(bytes) != bytes.size()) { emit failed(QStringLiteral("could not read that picture")); return {}; }
    tmp.close();
    *pixels = uprightSize(tmp.fileName());
    if (pixels->isEmpty()) { emit failed(QStringLiteral("that is not a picture I can read")); return {}; }
    QString err;
    const QString sha = attachments::store(m_db, tmp.fileName(), QMimeDatabase().mimeTypeForData(bytes).name(), &err);
    if (sha.isEmpty()) emit failed(err.isEmpty() ? QStringLiteral("could not save the picture") : err);
    return sha;
}

QString Images::storeMime(const QMimeData *mime, QSize *pixels)
{
    for (const char *format : kEncoded) {
        if (!mime->hasFormat(QLatin1String(format))) continue;
        const QByteArray bytes = mime->data(QLatin1String(format));
        if (isPicture(bytes)) return storeBytes(bytes, pixels);
    }
    // Only pixels were offered (a copy made inside a Qt program): PNG keeps every one of them.
    const QImage img = qvariant_cast<QImage>(mime->imageData());
    QByteArray png;
    QBuffer out(&png);
    if (img.isNull() || !out.open(QIODevice::WriteOnly) || !img.save(&out, "PNG")) { emit failed(QStringLiteral("could not read the clipboard picture")); return {}; }
    return storeBytes(png, pixels);
}

QString Images::storeClipboard(QSize *pixels)
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime || !hasPicture(mime)) { emit failed(QStringLiteral("there is no picture on the clipboard")); return {}; }
    return storeMime(mime, pixels);
}

qint64 Images::insertClipboard(qint64 pageId, double x, double y, double maxWidth)
{
    if (!pageId) return 0;
    QSize pixels;
    const QString sha = storeClipboard(&pixels);
    return sha.isEmpty() ? 0 : insertStored(pageId, sha, {}, pixels, x, y, maxWidth);
}

void Images::setGeometry(qint64 id, double x, double y, double w, double h)
{
    const QVariantMap before = image(id);
    if (before.isEmpty()) return;
    Database::Query q(m_db, "UPDATE image SET x=?, y=?, w=?, h=? WHERE id=?");
    q.bind(1, x).bind(2, y).bind(3, std::max(8.0, w)).bind(4, std::max(8.0, h)).bind(5, id);
    if (q.run()) emit changed(before.value("pageId").toLongLong());
}

void Images::setCrop(qint64 id, double cropX, double cropY, double cropW, double cropH,
                     double x, double y, double w, double h, int rotation)
{
    const QVariantMap before = image(id);
    if (before.isEmpty()) return;
    const double cw = std::clamp(cropW, 0.02, 1.0), ch = std::clamp(cropH, 0.02, 1.0);
    Database::Query q(m_db, "UPDATE image SET crop_x=?, crop_y=?, crop_w=?, crop_h=?, x=?, y=?, w=?, h=?, rotation=? WHERE id=?");
    q.bind(1, std::clamp(cropX, 0.0, 1.0 - cw)).bind(2, std::clamp(cropY, 0.0, 1.0 - ch)).bind(3, cw).bind(4, ch)
     .bind(5, x).bind(6, y).bind(7, std::max(8.0, w)).bind(8, std::max(8.0, h))
     .bind(9, rotation < 0 ? before.value("rotation").toInt() : ((rotation % 360) + 360) % 360).bind(10, id);
    if (q.run()) emit changed(before.value("pageId").toLongLong());
}

void Images::rotate(qint64 id, int quarterTurns)
{
    const QVariantMap before = image(id);
    if (before.isEmpty()) return;
    int turns = ((quarterTurns % 4) + 4) % 4;
    if (turns == 0) return;
    // The crop lives in the turned picture's coordinates, so it turns with it: a quarter turn
    // clockwise sends (x, y, w, h) to (1 - y - h, x, h, w).
    double cx = before.value("cropX").toDouble(), cy = before.value("cropY").toDouble();
    double cw = before.value("cropW").toDouble(), chh = before.value("cropH").toDouble();
    for (int i = 0; i < turns; ++i) {
        const double nx = 1.0 - cy - chh, ny = cx, nw = chh, nh = cw;
        cx = nx; cy = ny; cw = nw; chh = nh;
    }
    const int rotation = (before.value("rotation").toInt() + 90 * turns) % 360;
    // The picture keeps its middle and swaps its sides on an odd turn, so it does not wander.
    const double x = before.value("x").toDouble(), y = before.value("y").toDouble();
    const double w = before.value("w").toDouble(), h = before.value("h").toDouble();
    const bool sideways = turns % 2 == 1;
    const double nw = sideways ? h : w, nh = sideways ? w : h;
    Database::Query q(m_db, "UPDATE image SET rotation=?, crop_x=?, crop_y=?, crop_w=?, crop_h=?, x=?, y=?, w=?, h=? WHERE id=?");
    q.bind(1, rotation).bind(2, cx).bind(3, cy).bind(4, cw).bind(5, chh)
     .bind(6, x + (w - nw) / 2).bind(7, y + (h - nh) / 2).bind(8, nw).bind(9, nh).bind(10, id);
    if (q.run()) emit changed(before.value("pageId").toLongLong());
}

void Images::resetCrop(qint64 id)
{
    const QVariantMap before = image(id);
    if (before.isEmpty()) return;
    // Back to the whole picture, the right way up, around the middle of where it sits now.
    const double cw = before.value("cropW").toDouble(), ch = before.value("cropH").toDouble();
    const bool sideways = before.value("rotation").toInt() % 180 != 0;
    const double w = before.value("w").toDouble(), h = before.value("h").toDouble();
    double fullW = w / std::max(0.02, cw), fullH = h / std::max(0.02, ch);
    if (sideways) std::swap(fullW, fullH);
    Database::Query q(m_db, "UPDATE image SET crop_x=0, crop_y=0, crop_w=1, crop_h=1, rotation=0, x=?, y=?, w=?, h=? WHERE id=?");
    q.bind(1, before.value("x").toDouble() + (w - fullW) / 2).bind(2, before.value("y").toDouble() + (h - fullH) / 2)
     .bind(3, fullW).bind(4, fullH).bind(5, id);
    if (q.run()) emit changed(before.value("pageId").toLongLong());
}

void Images::remove(qint64 id)
{
    const QVariantMap before = image(id);
    if (before.isEmpty()) return;
    Database::Query q(m_db, "DELETE FROM image WHERE id=?");
    q.bind(1, id);
    if (q.run()) emit changed(before.value("pageId").toLongLong());
}
