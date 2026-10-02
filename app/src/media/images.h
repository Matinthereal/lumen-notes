#pragma once
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class Database;

// Pictures placed on a page: a photo of the board, a diagram, a screenshot. Stored by content hash
// in attachments (so the same diagram in ten pages costs one file) and positioned in page units.
// They render under the ink, because you annotate on top of a picture, never behind it.
//
// Cropping and turning never touch the file: each picture carries a quarter-turn count and the
// part of the (turned) picture to show, as a fraction of it, so the original is always one Reset
// away and the same file can be cropped differently on two pages.
class Images : public QObject {
    Q_OBJECT
public:
    explicit Images(Database &db, QObject *parent = nullptr);

    Q_INVOKABLE QVariantList list(qint64 pageId) const;          // [{id, path, x, y, w, h}]
    Q_INVOKABLE qint64 insertFile(qint64 pageId, const QUrl &file, double x, double y, double maxWidth = 420);
    Q_INVOKABLE qint64 insertClipboard(qint64 pageId, double x, double y, double maxWidth = 420);
    // Centred on (cx, cy), as big as fits the box without being blown up past its own pixels, and
    // the right way up for a phone photo whose camera said it was turned.
    Q_INVOKABLE qint64 place(qint64 pageId, const QUrl &file, double cx, double cy, double maxW, double maxH);
    Q_INVOKABLE qint64 placeClipboard(qint64 pageId, double cx, double cy, double maxW, double maxH);
    // Whatever on the clipboard can become a picture: one copied in Lumen (it keeps its trim, turn
    // and size), a picture copied in a browser or taken by a screenshot tool, or picture files
    // copied in a file manager. Returns the ids placed, fanned out from (cx, cy).
    Q_INVOKABLE QVariantList pasteClipboard(qint64 pageId, double cx, double cy, double maxW, double maxH);
    // The picture as the page shows it — turned and trimmed, at the file's own resolution — for
    // any other program, and for Lumen itself the note of which stored file it is.
    Q_INVOKABLE bool copy(qint64 id);
    Q_INVOKABLE qint64 duplicate(qint64 id, double dx = 24, double dy = 24);
    // A picture that arrived as bytes: dragged out of a browser, or just fetched.
    Q_INVOKABLE qint64 placeData(qint64 pageId, const QByteArray &bytes, double cx, double cy, double maxW, double maxH);
    // A picture dragged in as a link. Fetched in the background, up to kFetchLimit bytes; fetched()
    // says where it landed and failed() why it did not. data: links are read on the spot.
    Q_INVOKABLE void fetch(qint64 pageId, const QUrl &url, double cx, double cy, double maxW, double maxH);
    static constexpr qint64 kFetchLimit = 40 * 1024 * 1024;
    Q_INVOKABLE qint64 restore(const QVariantMap &picture);      // a removed picture back as it was, trim and turn too
    static QSize uprightSize(const QString &path);                // pixels as shown, after the camera's own turn
    Q_INVOKABLE bool clipboardHasImage() const;
    Q_INVOKABLE void setGeometry(qint64 id, double x, double y, double w, double h);
    // The crop is in the turned picture's own coordinates, 0..1; the rect is where it goes on the page.
    Q_INVOKABLE void setCrop(qint64 id, double cropX, double cropY, double cropW, double cropH,
                             double x, double y, double w, double h, int rotation = -1);   // -1: leave the turn alone
    Q_INVOKABLE void rotate(qint64 id, int quarterTurns = 1);     // turns the crop with it, keeps the centre
    Q_INVOKABLE void resetCrop(qint64 id);                        // the whole picture, the right way up
    Q_INVOKABLE void remove(qint64 id);
    Q_INVOKABLE QVariantMap image(qint64 id) const;
    Q_INVOKABLE int count(qint64 pageId) const;
    QVariantList listForExport(qint64 pageId) const { return list(pageId); }

signals:
    void changed(qint64 pageId);
    void failed(const QString &message);
    void fetched(qint64 pageId, qint64 id);

private:
    qint64 insertStored(qint64 pageId, const QString &sha, const QString &mime, QSize pixels, double x, double y, double maxWidth);
    qint64 store(qint64 pageId, const QString &sha, double x, double y, double w, double h);
    QString storeFile(const QString &path, QSize *pixels);
    QString storeClipboard(QSize *pixels);
    QString storeBytes(const QByteArray &bytes, QSize *pixels);
    QString storeMime(const class QMimeData *mime, QSize *pixels);
    qint64 placeStored(qint64 pageId, const QString &sha, QSize pixels, double cx, double cy, double maxW, double maxH);
    Database &m_db;
    class QNetworkAccessManager *m_network = nullptr;
};
