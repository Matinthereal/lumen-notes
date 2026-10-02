#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include "storage/database.h"
#include "storage/library.h"
#include "storage/schema.h"
#include "update/updater.h"

// The updater: which release counts as newer, which file each kind of install takes, and the whole
// AppImage swap against local files. Nothing here touches the network: the feed and the downloads
// are file:// URLs.
using update::Host;
using update::Install;

namespace {

void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate), qPrintable(path));
    f.write(bytes);
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QJsonObject asset(const QString &name, const QUrl &url, const QByteArray &sha256)
{
    QJsonObject a{{"name", name}, {"browser_download_url", url.toString()}, {"size", 0}};
    if (!sha256.isEmpty()) a.insert("digest", QString::fromLatin1("sha256:" + sha256));
    return a;
}

// A library, a feed file and the folders an install writes to, all in one temporary place.
struct Bench {
    QTemporaryDir dir;
    Database db;
    std::unique_ptr<Library> lib;
    Host host;
    Bench()
    {
        QVERIFY(db.open(dir.path() + "/t.db"));
        QVERIFY(ensureSchema(db) > 0);
        lib = std::make_unique<Library>(db);
        host.version = QStringLiteral("0.2.0");
        host.os = QStringLiteral("other");
        host.feed = QUrl::fromLocalFile(dir.path() + "/latest.json");
        host.downloadDir = dir.path() + "/downloads";
        QDir().mkpath(host.downloadDir);
    }
    void publish(const QString &tag, const QJsonArray &assets = {})
    {
        writeFile(dir.path() + "/latest.json", QJsonDocument(QJsonObject{
            {"tag_name", tag}, {"body", "## New\n- things"}, {"html_url", "https://example.invalid/release"}, {"assets", assets}}).toJson());
    }
    // The file a release offers, and the asset entry that points at it.
    QJsonObject offer(const QString &name, const QByteArray &bytes, const QByteArray &sha256)
    {
        writeFile(dir.path() + "/" + name + ".published", bytes);
        return asset(name, QUrl::fromLocalFile(dir.path() + "/" + name + ".published"), sha256);
    }
};

bool settled(const Updater &u) { return u.stage() != QLatin1String("checking") && u.stage() != QLatin1String("downloading"); }

}

class TstUpdate : public QObject {
    Q_OBJECT
private slots:
    void whichTagIsNewer()
    {
        QVERIFY(update::isNewer("v0.3.0", "0.2.0"));
        QVERIFY(update::isNewer("0.2.1", "0.2.0"));
        QVERIFY(update::isNewer("V1.0", "0.9.9"));
        QVERIFY(update::isNewer("v0.10.0", "0.9.0"));        // numbers, not text
        QVERIFY(!update::isNewer("v0.2.0", "0.2.0"));
        QVERIFY(!update::isNewer("v0.2", "0.2.0"));
        QVERIFY(!update::isNewer("v0.1.9", "0.2.0"));
        QVERIFY(!update::isNewer("v0.3.0-rc1", "0.2.0"));
        QVERIFY(!update::isNewer("nightly", "0.2.0"));
        QVERIFY(!update::isNewer("", "0.2.0"));
    }

    void readsGitHubsAnswer()
    {
        const update::Release r = update::parseRelease(readFile(QStringLiteral(LUMEN_TEST_DATA "/release.json")));
        QVERIFY(r.valid());
        QCOMPARE(r.version, QStringLiteral("0.3.0"));
        QVERIFY(r.notes.contains(QLatin1String("## New in this release")));
        QCOMPARE(r.page, QUrl("https://github.com/Matinthereal/lumen-notes/releases/tag/v0.3.0"));
        QCOMPARE(r.assets.size(), 4);
        QCOMPARE(r.assets.last().name, QStringLiteral("Lumen-x86_64.AppImage"));
        QCOMPARE(r.assets.last().size, 49977848);
        QCOMPARE(r.assets.last().sha256, QByteArray("4b49e5a8af9a8d904c1bba2ea3bbf0e2d98557bb9484f629099337390a3ee035"));
    }

    void refusesWhatIsNotARelease()
    {
        for (const char *json : {"", "{", "[]", "null", "{\"message\": \"API rate limit exceeded\"}", "{\"tag_name\": 5}",
                                 "{\"tag_name\": \"latest\"}", "{\"tag_name\": \"v0.3.0\", \"draft\": true}",
                                 "{\"tag_name\": \"v0.3.0\", \"prerelease\": true}"})
            QVERIFY2(!update::parseRelease(json).valid(), json);
        // A release with nothing else in it is still a release; a digest that is not one is no digest.
        QVERIFY(update::parseRelease("{\"tag_name\": \"v0.3.0\"}").valid());
        for (const char *digest : {"", "md5:abcd", "sha256:abcd", "sha256:zz49e5a8af9a8d904c1bba2ea3bbf0e2d98557bb9484f629099337390a3ee035"}) {
            const QJsonObject a{{"name", "Lumen-x86_64.AppImage"}, {"browser_download_url", "https://example.invalid/a"}, {"digest", digest}};
            const update::Release r = update::parseRelease(QJsonDocument(QJsonObject{{"tag_name", "v0.3.0"}, {"assets", QJsonArray{a}}}).toJson());
            QCOMPARE(r.assets.size(), 1);
            QVERIFY2(r.assets.first().sha256.isEmpty(), digest);
        }
    }

    void eachInstallTakesItsOwnFile()
    {
        const update::Release r = update::parseRelease(readFile(QStringLiteral(LUMEN_TEST_DATA "/release.json")));
        QCOMPARE(update::assetFor(r, Install::AppImage)->name, QStringLiteral("Lumen-x86_64.AppImage"));
        QCOMPARE(update::assetFor(r, Install::WindowsSetup)->name, QStringLiteral("Lumen-Setup-0.3.0.exe"));
        QCOMPARE(update::assetFor(r, Install::AndroidApk)->name, QStringLiteral("Lumen-0.3.0-android.apk"));
        QVERIFY(!update::assetFor(r, Install::ReleasePage));
        // One .apk for each processor, and never the other's: without its own, the release page.
        QVERIFY(!update::assetFor(r, Install::AndroidApk, QStringLiteral("x86_64")));
        update::Release both = r;
        both.assets.append(update::Asset{QStringLiteral("Lumen-0.3.0-android-x86_64.apk"), QUrl("https://example.invalid/x"), 1, {}});
        QCOMPARE(update::assetFor(both, Install::AndroidApk, QStringLiteral("x86_64"))->name, QStringLiteral("Lumen-0.3.0-android-x86_64.apk"));
        QCOMPARE(update::assetFor(both, Install::AndroidApk, QStringLiteral("arm64"))->name, QStringLiteral("Lumen-0.3.0-android.apk"));
        // v0.2.0 carried only an unsigned .apk, which no tablet will install.
        update::Release unsignedOnly;
        unsignedOnly.version = "0.2.0";
        unsignedOnly.assets = {update::Asset{QStringLiteral("Lumen-0.2.0-android-unsigned.apk"), QUrl("https://example.invalid/a"), 1, {}}};
        QVERIFY(!update::assetFor(unsignedOnly, Install::AndroidApk));
        QVERIFY(!update::assetFor(unsignedOnly, Install::AppImage));
    }

    void theInstallFollowsTheMachine()
    {
        QTemporaryDir dir;
        const QString image = dir.path() + "/Lumen-x86_64.AppImage";
        writeFile(image, "old");
        Host host;
        host.os = "android";                       QCOMPARE(update::installFor(host), Install::AndroidApk);
        host.os = "windows";                       QCOMPARE(update::installFor(host), Install::WindowsSetup);
        host.os = "ios";                           QCOMPARE(update::installFor(host), Install::ReleasePage);
        host.os = "linux";                         QCOMPARE(update::installFor(host), Install::ReleasePage);     // cmake --install, or a package
        host.appImage = image;                     QCOMPARE(update::installFor(host), Install::AppImage);
        host.appImage = dir.path() + "/gone";      QCOMPARE(update::installFor(host), Install::ReleasePage);
        host.appImage = dir.path();                QCOMPARE(update::installFor(host), Install::ReleasePage);     // not a file
        host.os = "android"; host.appImage = image; QCOMPARE(update::installFor(host), Install::AndroidApk);
    }

    // $APPIMAGE reaches every program an AppImage starts. Only the Lumen running from that image's
    // mount may replace the file; one started from another AppImage's terminal must leave it alone.
    void anInheritedAppImageIsNotOurs()
    {
        QTemporaryDir dir;
        const QString image = dir.path() + "/Other.AppImage", mount = dir.path() + "/mount", elsewhere = dir.path() + "/usr/bin";
        QVERIFY(QDir().mkpath(mount + "/usr/bin") && QDir().mkpath(elsewhere));
        writeFile(image, "another program");
        writeFile(mount + "/usr/bin/lumen", "x");
        writeFile(elsewhere + "/lumen", "x");
        QCOMPARE(update::ownAppImage(image, mount, mount + "/usr/bin/lumen"), image);
        QCOMPARE(update::ownAppImage(image, mount + "/", mount + "/usr/bin/../bin/lumen"), image);
        QCOMPARE(update::ownAppImage(image, mount, elsewhere + "/lumen"), QString());      // cmake --install, from that terminal
        QCOMPARE(update::ownAppImage(image, QString(), mount + "/usr/bin/lumen"), QString());
        QCOMPARE(update::ownAppImage(image, dir.path() + "/unmounted", elsewhere + "/lumen"), QString());
        QCOMPARE(update::ownAppImage(image, dir.path() + "/mou", mount + "/usr/bin/lumen"), QString());    // a prefix is not a parent
        QCOMPARE(update::ownAppImage(QString(), mount, mount + "/usr/bin/lumen"), QString());
    }

    // Showing a picture would fetch it, and the release check is the only request Lumen makes.
    void notesComeWithoutTheirPictures()
    {
        QCOMPARE(update::withoutPictures("See ![the new sheet](https://example.invalid/a.png) here"), QStringLiteral("See the new sheet here"));
        QCOMPARE(update::withoutPictures("![](https://example.invalid/a.png \"title\")"), QString());
        QCOMPARE(update::withoutPictures("![shot][one]\n\n[one]: https://example.invalid/a.png"), QStringLiteral("shot\n\n[one]: https://example.invalid/a.png"));
        QCOMPARE(update::withoutPictures("a <img src=\"https://example.invalid/a.png\" width=\"400\"> b <IMG SRC='x'/> c"), QStringLiteral("a  b  c"));
        QCOMPARE(update::withoutPictures("[![build](https://example.invalid/badge.svg)](https://example.invalid/ci)"), QStringLiteral("[build](https://example.invalid/ci)"));
        const QString plain = QStringLiteral("## New\n- **Updates.** [the guide](https://example.invalid/guide) and `![not code]`");
        QCOMPARE(update::withoutPictures(plain), plain);
        const update::Release r = update::parseRelease("{\"tag_name\": \"v0.3.0\", \"body\": \"New: ![shot](https://example.invalid/a.png)\"}");
        QCOMPARE(r.notes, QStringLiteral("New: shot"));
    }

    void whatAFailedCheckSays()
    {
        const QUrl github("https://api.github.com/repos/Matinthereal/lumen-notes/releases/latest");
        QCOMPARE(update::checkFailure(github, true, 0), QStringLiteral("Could not reach GitHub. Are you online?"));
        QCOMPARE(update::checkFailure(github, true, 403), QStringLiteral("GitHub is turning requests away just now. Try again in an hour."));
        QCOMPARE(update::checkFailure(github, true, 429), QStringLiteral("GitHub is turning requests away just now. Try again in an hour."));
        QCOMPARE(update::checkFailure(github, true, 500), QStringLiteral("GitHub answered with an error (500). Try again later."));
        // No TLS backend: nothing was ever sent, whatever else is the matter.
        const QString noTls = QStringLiteral("This build of Lumen cannot make a secure connection. New releases are on its GitHub page.");
        QCOMPARE(update::checkFailure(github, false, 0), noTls);
        QCOMPARE(update::checkFailure(github, false, 403), noTls);
        QCOMPARE(update::checkFailure(QUrl("file:///tmp/latest.json"), false, 0), QStringLiteral("Could not reach GitHub. Are you online?"));
    }

    void onceADay()
    {
        const qint64 now = 1790000000;
        QVERIFY(update::checkDue(0, now));                    // never checked
        QVERIFY(!update::checkDue(now - 60, now));
        QVERIFY(!update::checkDue(now - 24 * 3600 + 1, now));
        QVERIFY(update::checkDue(now - 24 * 3600, now));
        QVERIFY(update::checkDue(now + 3600, now));           // the clock went back
    }

    void digestOfAFile()
    {
        QTemporaryDir dir;
        writeFile(dir.path() + "/abc", "abc");
        QCOMPARE(update::sha256Of(dir.path() + "/abc"), QByteArray("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
        QVERIFY(update::sha256Of(dir.path() + "/missing").isEmpty());
    }

    void aCheckOnDemandSaysWhatItFound()
    {
        Bench b;
        Updater u(*b.lib, b.host);
        QCOMPARE(u.currentVersion(), QStringLiteral("0.2.0"));
        b.publish("v0.3.0");
        u.check();
        QTRY_VERIFY(settled(u));
        QVERIFY(u.available());
        QVERIFY(u.offered());
        QCOMPARE(u.latestVersion(), QStringLiteral("0.3.0"));
        QCOMPARE(u.status(), QStringLiteral("Lumen 0.3.0 is available."));
        QVERIFY(u.notes().contains("things"));
        QVERIFY(b.lib->setting("update.lastCheck", "0").toLongLong() >= QDateTime::currentSecsSinceEpoch() - 60);

        for (const char *tag : {"v0.2.0", "v0.1.0"}) {
            b.publish(tag);
            u.check();
            QTRY_VERIFY(settled(u));
            QVERIFY2(!u.available(), tag);
            QVERIFY(!u.offered());
            QCOMPARE(u.status(), QStringLiteral("Lumen 0.2.0 is the newest version."));
        }
    }

    void theLaunchCheckIsDailyAndCanBeTurnedOff()
    {
        Bench b;
        Updater u(*b.lib, b.host);
        QVERIFY(u.automatic());                               // on unless turned off
        b.publish("v0.2.0");
        u.checkOnLaunch();
        QCOMPARE(u.stage(), QStringLiteral("checking"));
        QTRY_VERIFY(settled(u));
        QVERIFY(!u.available());

        b.publish("v0.3.0");                                  // released a minute later
        u.checkOnLaunch();
        QCOMPARE(u.stage(), QString());                       // not asked again today
        QTest::qWait(100);
        QVERIFY(!u.available());

        const QString twoDaysAgo = QString::number(QDateTime::currentSecsSinceEpoch() - 2 * 24 * 3600);
        b.lib->setSetting("update.lastCheck", twoDaysAgo);
        QSignalSpy toggled(&u, &Updater::automaticChanged);
        u.setAutomatic(false);
        QCOMPARE(toggled.count(), 1);
        QCOMPARE(b.lib->setting("update.auto", "1"), QStringLiteral("0"));
        u.checkOnLaunch();
        QCOMPARE(u.stage(), QString());
        QTest::qWait(100);
        QVERIFY(!u.available());
        u.check();                                            // asking by hand still works
        QTRY_VERIFY(settled(u));
        QVERIFY(u.available());

        u.setAutomatic(true);
        b.lib->setSetting("update.lastCheck", twoDaysAgo);
        Updater next(*b.lib, b.host);                         // the next launch
        next.checkOnLaunch();
        QTRY_VERIFY(next.available());
        QVERIFY(next.offered());
    }

    void laterAndSkip()
    {
        Bench b;
        b.publish("v0.3.0");
        {
            Updater u(*b.lib, b.host);
            u.checkOnLaunch();
            QTRY_VERIFY(u.offered());
            u.later();
            QVERIFY(!u.offered());
            QVERIFY(u.available());
            QCOMPARE(b.lib->setting("update.skipped", ""), QString());
        }
        b.lib->setSetting("update.lastCheck", "0");
        {
            Updater u(*b.lib, b.host);                        // Later lasts until the next check
            u.checkOnLaunch();
            QTRY_VERIFY(u.offered());
            u.skip();
            QVERIFY(!u.offered());
            QCOMPARE(b.lib->setting("update.skipped", ""), QStringLiteral("0.3.0"));
            QCOMPARE(u.status(), QStringLiteral("Lumen 0.3.0 will not be offered again."));
            u.unskip();                                       // the toast's Undo
            QVERIFY(u.offered());
            QCOMPARE(b.lib->setting("update.skipped", ""), QString());
            QCOMPARE(u.status(), QStringLiteral("Lumen 0.3.0 is available."));
            u.skip();
        }
        b.lib->setSetting("update.lastCheck", "0");
        {
            Updater u(*b.lib, b.host);
            u.checkOnLaunch();
            QTRY_VERIFY(u.available());
            QVERIFY(!u.offered());                            // skipped: known, not mentioned
            u.check();                                        // asked for: mentioned
            QTRY_VERIFY(settled(u));
            QVERIFY(u.offered());
        }
        b.lib->setSetting("update.lastCheck", "0");
        b.publish("v0.3.1");
        {
            Updater u(*b.lib, b.host);
            u.checkOnLaunch();
            QTRY_VERIFY(u.offered());                         // the one after the skipped one
            QCOMPARE(u.latestVersion(), QStringLiteral("0.3.1"));
        }
    }

    void failuresAreQuietUnlessAsked()
    {
        Bench b;
        Updater u(*b.lib, b.host);
        QSignalSpy urls(&u, &Updater::openUrl);
        u.checkOnLaunch();                                    // no feed file: as good as offline
        QTRY_VERIFY(settled(u));
        QCOMPARE(u.status(), QString());
        QVERIFY(!u.available());
        QCOMPARE(b.lib->setting("update.lastCheck", "0"), QStringLiteral("0"));    // so the next launch tries again
        u.check();
        QTRY_VERIFY(settled(u));
        QCOMPARE(u.status(), QStringLiteral("Could not reach GitHub. Are you online?"));

        writeFile(b.dir.path() + "/latest.json", "<html>rate limited</html>");
        u.checkOnLaunch();
        QTRY_VERIFY(settled(u));
        QCOMPARE(u.status(), QStringLiteral("Could not reach GitHub. Are you online?"));    // still the manual answer
        u.check();
        QTRY_VERIFY(settled(u));
        QCOMPARE(u.status(), QStringLiteral("GitHub's answer could not be read. Try again later."));
        QVERIFY(!u.available());
        u.install();                                          // nothing to install: nothing happens
        u.restart();
        QCOMPARE(urls.count(), 0);

        b.host.feed = QUrl();                                 // a test run: never asks
        Updater mute(*b.lib, b.host);
        mute.checkOnLaunch();
        QCOMPARE(mute.stage(), QString());
        mute.check();
        QCOMPARE(mute.stage(), QString());
        QCOMPARE(mute.status(), QStringLiteral("Updates are not checked during a test run."));
    }

    void anAppImageReplacesItself()
    {
        Bench b;
        const QString image = b.dir.path() + "/apps/Lumen-x86_64.AppImage";
        QDir().mkpath(b.dir.path() + "/apps");
        writeFile(image, "the old lumen");
        QFile::setPermissions(image, QFile::ReadOwner | QFile::WriteOwner);
        b.host.os = "linux";
        b.host.appImage = image;
        const QByteArray fresh = QByteArray("the new lumen\n").repeated(40000);       // 560 kB: more than one read
        const QByteArray sum = QCryptographicHash::hash(fresh, QCryptographicHash::Sha256).toHex();
        b.publish("v0.3.0", {b.offer("Lumen-Setup-0.3.0.exe", "not this one", sum), b.offer("Lumen-x86_64.AppImage", fresh, sum)});

        Updater u(*b.lib, b.host);
        QVERIFY(u.installsItself());
        QSignalSpy restarts(&u, &Updater::restartRequested);
        QSignalSpy urls(&u, &Updater::openUrl);
        u.restart();                                          // nothing installed yet
        QCOMPARE(restarts.count(), 0);
        u.check();
        QTRY_VERIFY(u.available());
        u.install();
        QCOMPARE(u.stage(), QStringLiteral("downloading"));
        QTRY_VERIFY(settled(u));
        QCOMPARE(u.stage(), QStringLiteral("ready"));
        QCOMPARE(u.status(), QStringLiteral("Lumen 0.3.0 is in place. Restart to use it."));
        QCOMPARE(readFile(image), fresh);
        QCOMPARE(update::sha256Of(image), sum);
#ifdef Q_OS_UNIX
        QVERIFY(QFileInfo(image).isExecutable());             // chmod +x
        QVERIFY(!(QFileInfo(image).permissions() & QFile::ExeOther));    // and no wider than it was
#endif
        QCOMPARE(QDir(b.dir.path() + "/apps").entryList(QDir::Files | QDir::Hidden), QStringList{"Lumen-x86_64.AppImage"});
        QCOMPARE(urls.count(), 0);

        u.check();                                            // a check now must not forget the restart
        QCOMPARE(u.stage(), QStringLiteral("ready"));
        u.later();                                            // Later on "Restart now" hides the only way to restart
        QVERIFY(!u.offered());
        b.lib->setSetting("update.lastCheck", "0");
        u.checkOnLaunch();
        QVERIFY(!u.offered());
        u.check();                                            // asking by hand brings it back
        QVERIFY(u.offered());
        QCOMPARE(u.stage(), QStringLiteral("ready"));
        QCOMPARE(u.status(), QStringLiteral("Lumen 0.3.0 is in place. Restart to use it."));
        u.restart();
        QCOMPARE(restarts.count(), 1);
        QCOMPARE(restarts.first().at(0).toString(), image);
        QCOMPARE(restarts.first().at(1).toStringList(), QStringList());
    }

    // The stop button on the banner's progress: back to the offer, with nothing changed or left behind.
    void aStoppedDownloadChangesNothing()
    {
        Bench b;
        const QString image = b.dir.path() + "/apps/Lumen-x86_64.AppImage";
        QDir().mkpath(b.dir.path() + "/apps");
        writeFile(image, "the old lumen");
        b.host.os = "linux";
        b.host.appImage = image;
        const QByteArray fresh = QByteArray("the new lumen\n").repeated(40000);
        const QByteArray sum = QCryptographicHash::hash(fresh, QCryptographicHash::Sha256).toHex();
        b.publish("v0.3.0", {b.offer("Lumen-x86_64.AppImage", fresh, sum)});

        Updater u(*b.lib, b.host);
        QSignalSpy restarts(&u, &Updater::restartRequested);
        u.check();
        QTRY_VERIFY(u.available());
        u.later();
        u.cancel();                                           // not downloading: nothing to stop
        QVERIFY(!u.offered());
        u.install();
        QCOMPARE(u.stage(), QStringLiteral("downloading"));
        u.cancel();
        QCOMPARE(u.stage(), QString());
        QCOMPARE(u.status(), QStringLiteral("Lumen 0.3.0 is available."));
        QVERIFY(u.available());
        QTest::qWait(200);                                    // nothing of the stopped download arrives late
        QCOMPARE(u.stage(), QString());
        QCOMPARE(readFile(image), QByteArray("the old lumen"));
        QCOMPARE(QDir(b.dir.path() + "/apps").entryList(QDir::Files | QDir::Hidden), QStringList{"Lumen-x86_64.AppImage"});
        u.restart();
        QCOMPARE(restarts.count(), 0);

        u.install();                                          // and Update works again afterwards
        QTRY_VERIFY(settled(u));
        QCOMPARE(u.stage(), QStringLiteral("ready"));
        QCOMPARE(readFile(image), fresh);
    }

    void aDownloadThatIsNotTheReleaseIsRefused_data()
    {
        QTest::addColumn<QString>("what");
        QTest::addColumn<QString>("says");
        QTest::newRow("wrong digest") << "wrong" << "The download did not match its checksum and was thrown away. Nothing was changed.";
        QTest::newRow("no digest") << "none" << "This release has no checksum to check the download against, so Lumen will not install it by itself.";
        QTest::newRow("no file") << "gone" << "The download did not finish. Nothing was changed.";
    }
    void aDownloadThatIsNotTheReleaseIsRefused()
    {
        QFETCH(QString, what);
        QFETCH(QString, says);
        Bench b;
        const QString image = b.dir.path() + "/apps/Lumen-x86_64.AppImage";
        QDir().mkpath(b.dir.path() + "/apps");
        writeFile(image, "the old lumen");
        b.host.os = "linux";
        b.host.appImage = image;
        const QByteArray tampered = "not what was published";
        const QByteArray published = QCryptographicHash::hash("what was published", QCryptographicHash::Sha256).toHex();
        QJsonObject entry = b.offer("Lumen-x86_64.AppImage", tampered, what == "none" ? QByteArray() : published);
        if (what == "gone") QVERIFY(QFile::remove(b.dir.path() + "/Lumen-x86_64.AppImage.published"));
        b.publish("v0.3.0", {entry});

        Updater u(*b.lib, b.host);
        QSignalSpy restarts(&u, &Updater::restartRequested);
        u.check();
        QTRY_VERIFY(u.available());
        u.install();
        QTRY_VERIFY(settled(u));
        QCOMPARE(u.stage(), QStringLiteral("failed"));
        QCOMPARE(u.status(), says);
        QCOMPARE(readFile(image), QByteArray("the old lumen"));
        QCOMPARE(QDir(b.dir.path() + "/apps").entryList(QDir::Files | QDir::Hidden), QStringList{"Lumen-x86_64.AppImage"});
        u.restart();
        QCOMPARE(restarts.count(), 0);
    }

    void windowsRunsTheInstaller()
    {
        Bench b;
        b.host.os = "windows";
        const QByteArray setup = "MZ the installer";
        const QByteArray sum = QCryptographicHash::hash(setup, QCryptographicHash::Sha256).toHex();
        b.publish("v0.3.0", {b.offer("Lumen-Setup-0.3.0.exe", setup, sum)});
        // The installer an earlier update ran is cleared away; nothing else in the folder is.
        writeFile(b.host.downloadDir + "/Lumen-Setup-0.2.0.exe", "MZ last time");
        writeFile(b.host.downloadDir + "/Other-Setup-1.0.exe", "MZ not ours");
        Updater u(*b.lib, b.host);
        QCOMPARE(QDir(b.host.downloadDir).entryList(QDir::Files), QStringList{"Other-Setup-1.0.exe"});
        QVERIFY(u.installsItself());
        QSignalSpy restarts(&u, &Updater::restartRequested);
        u.launchFailed();                                     // nothing was started: nothing failed
        QCOMPARE(u.stage(), QString());
        u.check();
        QTRY_VERIFY(u.available());
        u.install();
        QTRY_COMPARE(restarts.count(), 1);
        const QString program = restarts.first().at(0).toString();
        QCOMPARE(program, b.host.downloadDir + "/Lumen-Setup-0.3.0.exe");
        QCOMPARE(readFile(program), setup);
        const QStringList flags = restarts.first().at(1).toStringList();
        QVERIFY(flags.contains("/SILENT"));
        QVERIFY(flags.contains("/RELAUNCH=1"));

        // A refused UAC prompt: the installer never started. Say so, and let Update be pressed again.
        QCOMPARE(u.stage(), QStringLiteral("ready"));
        u.launchFailed();
        QCOMPARE(u.stage(), QStringLiteral("failed"));
        QCOMPARE(u.status(), QStringLiteral("The installer could not be started. Nothing was changed."));
        QVERIFY(u.offered());
        u.install();                                          // Try again
        QTRY_COMPARE(restarts.count(), 2);
        QCOMPARE(u.stage(), QStringLiteral("ready"));
        QCOMPARE(readFile(program), setup);

        // The wrong bytes are never run.
        b.publish("v0.3.1", {b.offer("Lumen-Setup-0.3.1.exe", "MZ something else", sum)});
        Updater again(*b.lib, b.host);
        QSignalSpy none(&again, &Updater::restartRequested);
        again.check();
        QTRY_VERIFY(again.available());
        again.install();
        QTRY_COMPARE(again.stage(), QStringLiteral("failed"));
        QCOMPARE(none.count(), 0);
        QVERIFY(!QFileInfo::exists(b.host.downloadDir + "/Lumen-Setup-0.3.1.exe"));
    }

    void tabletsAndSourceBuildsAreSentToThePage()
    {
        Bench b;
        const QUrl apk("https://example.invalid/Lumen-0.3.0-android.apk");
        b.publish("v0.3.0", {asset("Lumen-0.3.0-android.apk", apk, {}), asset("Lumen-x86_64.AppImage", QUrl("https://example.invalid/a"), {})});
        const QUrl page("https://example.invalid/release");
        struct Case { const char *os; QUrl opens; };
        for (const Case &c : {Case{"android", apk}, Case{"ios", page}, Case{"linux", page}, Case{"other", page}}) {
            b.host.os = c.os;
            Updater u(*b.lib, b.host);
            QVERIFY(!u.installsItself());
            QSignalSpy urls(&u, &Updater::openUrl);
            u.check();
            QTRY_VERIFY(u.available());
            u.install();
            QCOMPARE(urls.count(), 1);
            QCOMPARE(urls.first().first().toUrl(), c.opens);
            QCOMPARE(u.stage(), QString());
            u.openReleasePage();
            QCOMPARE(urls.last().first().toUrl(), page);
        }
        // An Android release with no signed .apk: the page, where the notes say why.
        b.publish("v0.3.0", {asset("Lumen-0.3.0-android-unsigned.apk", apk, {})});
        b.host.os = "android";
        Updater u(*b.lib, b.host);
        QSignalSpy urls(&u, &Updater::openUrl);
        u.check();
        QTRY_VERIFY(u.available());
        u.install();
        QCOMPARE(urls.first().first().toUrl(), page);
    }
};

QTEST_GUILESS_MAIN(TstUpdate)
#include "tst_update.moc"
