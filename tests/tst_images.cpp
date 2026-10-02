#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include "media/images.h"
#include "storage/database.h"
#include "storage/library.h"
#include "storage/schema.h"

// Pictures in and out by the clipboard: what is stored must be what was offered, bit for bit where
// the other program offered a file's own bytes, and what goes out must be the picture as the page
// shows it at the file's own resolution. None of this needs a network or a helper.
class TstImages : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    Database m_db;
    std::unique_ptr<Library> m_library;
    std::unique_ptr<Images> m_images;
    qint64 m_page = 0, m_otherPage = 0;

    static QByteArray bytesOf(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
    static QImage detailed(int w, int h) {
        QImage img(w, h, QImage::Format_RGB32);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) img.setPixel(x, y, (x + y) % 2 ? qRgb(250, 250, 250) : qRgb(x % 200, 30, y % 200));
        return img;
    }
    static QByteArray encoded(const QImage &img, const char *format, int quality = -1) {
        QByteArray out;
        QBuffer buffer(&out);
        buffer.open(QIODevice::WriteOnly);
        img.save(&buffer, format, quality);
        return out;
    }
    QVariantMap only() const { const QVariantList l = m_images->list(m_page); return l.size() == 1 ? m_images->image(l.first().toMap().value("id").toLongLong()) : QVariantMap(); }
    void clearPage(qint64 page) { for (const QVariant &v : m_images->list(page)) m_images->remove(v.toMap().value("id").toLongLong()); }

private slots:
    void initTestCase() {
        qputenv("LUMEN_DATA_DIR", m_dir.path().toUtf8());
        QVERIFY(m_db.open(m_dir.path() + "/t.db"));
        QVERIFY(ensureSchema(m_db) > 0);
        m_library = std::make_unique<Library>(m_db);
        const qint64 section = m_library->createSection(m_library->createNotebook("N", "#000"), "S");
        m_page = m_library->createPage(section);
        m_otherPage = m_library->createPage(section);
        m_images = std::make_unique<Images>(m_db);
    }
    void init() { clearPage(m_page); clearPage(m_otherPage); QGuiApplication::clipboard()->clear(); }

    void nothingToPasteIsNotAPicture() {
        QVERIFY(!m_images->clipboardHasImage());
        QGuiApplication::clipboard()->setText("just words");
        QVERIFY(!m_images->clipboardHasImage());
        QVERIFY(m_images->pasteClipboard(m_page, 300, 300, 500, 500).isEmpty());
    }
    // A screenshot tool or another Qt program: pixels only. Stored as PNG, which loses none.
    void pastedPixelsAreStoredWhole() {
        const QImage shot = detailed(1600, 900);
        QGuiApplication::clipboard()->setImage(shot);
        QVERIFY(m_images->clipboardHasImage());
        const QVariantList placed = m_images->pasteClipboard(m_page, 400, 500, 556, 617);
        QCOMPARE(placed.size(), 1);
        const QVariantMap pic = only();
        QCOMPARE(QImage(pic.value("path").toString()).convertToFormat(QImage::Format_RGB32), shot);
        // 556 is where it is shown, not what is kept: fitted into the box, centred where asked.
        QCOMPARE(pic.value("w").toDouble(), 556.0);
        QVERIFY(qAbs(pic.value("x").toDouble() + pic.value("w").toDouble() / 2 - 400) < 0.01);
        QVERIFY(qAbs(pic.value("y").toDouble() + pic.value("h").toDouble() / 2 - 500) < 0.01);
    }
    // A browser: the picture's own encoded bytes, with HTML beside them. The bytes are kept as
    // they are — a JPEG is not decoded and saved again.
    void aBrowsersBytesAreKeptAsTheyAre() {
        const QByteArray jpeg = encoded(detailed(1200, 800), "JPEG", 85);
        auto *mime = new QMimeData;
        mime->setData("image/jpeg", jpeg);
        mime->setHtml("<img src=\"https://example.invalid/a.jpg\">");
        QGuiApplication::clipboard()->setMimeData(mime);
        QVERIFY(m_images->clipboardHasImage());
        QCOMPARE(m_images->pasteClipboard(m_page, 300, 300, 500, 500).size(), 1);
        QCOMPARE(bytesOf(only().value("path").toString()), jpeg);
        // Offered both ways, the lossless one wins.
        clearPage(m_page);
        const QByteArray png = encoded(detailed(1200, 800), "PNG");
        mime = new QMimeData;
        mime->setData("image/jpeg", jpeg);
        mime->setData("image/png", png);
        QGuiApplication::clipboard()->setMimeData(mime);
        QCOMPARE(m_images->pasteClipboard(m_page, 300, 300, 500, 500).size(), 1);
        QCOMPARE(bytesOf(only().value("path").toString()), png);
    }
    // A file manager: addresses of files. Pictures come in, anything else is passed over.
    void copiedFilesArePastedAndOthersSkipped() {
        const QString a = m_dir.path() + "/a.png", b = m_dir.path() + "/b.jpg", c = m_dir.path() + "/c.txt";
        QVERIFY(detailed(300, 200).save(a, "PNG"));
        QVERIFY(detailed(640, 480).save(b, "JPEG", 90));
        { QFile f(c); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("words"); }
        auto *mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(a), QUrl::fromLocalFile(c), QUrl::fromLocalFile(b), QUrl("https://example.invalid/d.png")});
        QGuiApplication::clipboard()->setMimeData(mime);
        QVERIFY(m_images->clipboardHasImage());
        QSignalSpy failed(m_images.get(), &Images::failed);
        const QVariantList placed = m_images->pasteClipboard(m_page, 300, 300, 500, 500);
        QCOMPARE(placed.size(), 2);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(bytesOf(m_images->image(placed.at(0).toLongLong()).value("path").toString()), bytesOf(a));
        QCOMPARE(bytesOf(m_images->image(placed.at(1).toLongLong()).value("path").toString()), bytesOf(b));
        // The second sits a little below and right of the first, so neither hides the other.
        const QVariantMap first = m_images->image(placed.at(0).toLongLong()), second = m_images->image(placed.at(1).toLongLong());
        QVERIFY(second.value("x").toDouble() + second.value("w").toDouble() / 2 > first.value("x").toDouble() + first.value("w").toDouble() / 2 + 20);
        // Only a file that is not a picture: nothing to paste, so the ink paste gets its turn.
        mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(c)});
        QGuiApplication::clipboard()->setMimeData(mime);
        QVERIFY(!m_images->clipboardHasImage());
    }
    // Copy: the picture as shown — trimmed and turned — at the file's own resolution; and for
    // Lumen itself a note of which stored file it is, so a paste is the same picture again.
    void copyCarriesThePictureAsShownAtFullResolution() {
        const QString file = m_dir.path() + "/photo.jpg";
        const QImage photo = detailed(2000, 1500);
        QVERIFY(photo.save(file, "JPEG", 90));
        const qint64 id = m_images->place(m_page, QUrl::fromLocalFile(file), 300, 300, 400, 400);
        QVERIFY(id > 0);
        QCOMPARE(m_images->image(id).value("w").toDouble(), 400.0);      // shown small…

        QVERIFY(m_images->copy(id));
        const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
        QCOMPARE(QGuiApplication::clipboard()->image().size(), QSize(2000, 1500));   // …copied whole
        QCOMPARE(mime->data("image/jpeg"), bytesOf(file));                // untouched: the file's own bytes

        m_images->setCrop(id, 0.25, 0, 0.5, 1, 100, 100, 200, 300);
        m_images->rotate(id, 1);
        QVERIFY(m_images->copy(id));
        mime = QGuiApplication::clipboard()->mimeData();
        QCOMPARE(QGuiApplication::clipboard()->image().size(), QSize(1500, 1000));   // a quarter turn of the middle half
        QVERIFY(!mime->hasFormat("image/jpeg"));                          // the file is no longer what is shown

        const QVariantMap was = m_images->image(id);
        const QVariantList pasted = m_images->pasteClipboard(m_otherPage, 500, 600, 400, 400);
        QCOMPARE(pasted.size(), 1);
        const QVariantMap now = m_images->image(pasted.first().toLongLong());
        QCOMPARE(now.value("pageId").toLongLong(), m_otherPage);
        QCOMPARE(now.value("attachment"), was.value("attachment"));
        QCOMPARE(now.value("rotation"), was.value("rotation"));
        for (const char *key : {"cropX", "cropY", "cropW", "cropH", "w", "h"}) QCOMPARE(now.value(key).toDouble(), was.value(key).toDouble());
        QVERIFY(qAbs(now.value("x").toDouble() + now.value("w").toDouble() / 2 - 500) < 0.01);

        const qint64 twin = m_images->duplicate(id);
        QVERIFY(twin > 0 && twin != id);
        QCOMPARE(m_images->image(twin).value("x").toDouble(), was.value("x").toDouble() + 24);
        QCOMPARE(m_images->image(twin).value("cropW").toDouble(), was.value("cropW").toDouble());
    }
    // Dropped bytes and data: links take the same road; what is not a picture is refused in words.
    void droppedBytesAndDataLinks() {
        const QByteArray png = encoded(detailed(640, 360), "PNG");
        const qint64 id = m_images->placeData(m_page, png, 300, 300, 500, 500);
        QVERIFY(id > 0);
        QCOMPARE(bytesOf(m_images->image(id).value("path").toString()), png);

        QSignalSpy fetched(m_images.get(), &Images::fetched);
        QSignalSpy failed(m_images.get(), &Images::failed);
        m_images->fetch(m_otherPage, QUrl("data:image/png;base64," + QString::fromLatin1(png.toBase64())), 200, 200, 500, 500);
        QCOMPARE(fetched.size(), 1);
        QCOMPARE(bytesOf(m_images->image(fetched.first().at(1).toLongLong()).value("path").toString()), png);

        QCOMPARE(m_images->placeData(m_page, "<html>not a picture</html>", 300, 300, 500, 500), 0);
        QCOMPARE(failed.size(), 1);
        m_images->fetch(m_page, QUrl("ftp://example.invalid/a.png"), 200, 200, 500, 500);
        QCOMPARE(failed.size(), 2);
        QCOMPARE(m_images->list(m_page).size(), 1);
    }
};
QTEST_MAIN(TstImages)
#include "tst_images.moc"
