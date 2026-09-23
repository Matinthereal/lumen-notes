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

private:
    qint64 insertStored(qint64 pageId, const QString &sha, const QString &mime, QSize pixels, double x, double y, double maxWidth);
    Database &m_db;
};
