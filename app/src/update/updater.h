#pragma once
#include <QByteArray>
#include <QCryptographicHash>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <memory>
#include <optional>

class Library;
class QNetworkReply;
class QSaveFile;

// Finding out about a newer Lumen and getting it. The only thing sent is one GET to GitHub for the
// newest release; nothing about the library or the machine goes with it. Pictures in the release
// notes are left out, because showing them would fetch them from wherever they are kept.
namespace update {

struct Asset {
    QString name;
    QUrl url;
    qint64 size = 0;
    QByteArray sha256;       // lower-case hex; empty when GitHub gave no digest
};

struct Release {
    QString version;         // "0.3.0", without the tag's "v"; empty when the answer was not a release
    QString notes;           // the release's text, markdown, without its pictures
    QUrl page;
    QList<Asset> assets;
    bool valid() const { return !version.isEmpty(); }
};

// How this copy of Lumen was installed decides what Update can do.
enum class Install {
    AppImage,        // download the new file over $APPIMAGE and restart
    WindowsSetup,    // download the installer and run it
    AndroidApk,      // hand the signed .apk to the system's installer
    ReleasePage      // an iPad, or a build from source: show the release page
};

struct Host {
    QString version;         // the running one
    QString os;              // android | ios | windows | linux | other
    QString cpu;             // what this build was made for: arm64, x86_64, …
    QString appImage;        // $APPIMAGE, when Lumen runs from it (ownAppImage)
    QUrl feed;               // empty: never ask (test runs)
    QString downloadDir;     // where the Windows installer is put
    // LUMEN_UPDATE_URL replaces the feed, for tests and screenshots; with it, $APPIMAGE is taken at
    // its word, so the swap can be run on a stand-in file.
    static Host detect();
};

Release parseRelease(const QByteArray &json);
// Markdown with its pictures taken out: "![alt](url)" leaves its alt text, an <img> tag nothing.
QString withoutPictures(QString markdown);
// $APPIMAGE is inherited by everything an AppImage starts, a terminal's shell included, so it is
// only ours when this program runs from that image's mount ($APPDIR). Empty when it is not.
QString ownAppImage(const QString &appImage, const QString &appDir, const QString &program);
// "v0.3.0" against "0.2.0". A tag that is no version, or carries a suffix ("-rc1"), is never newer.
bool isNewer(const QString &tag, const QString &running);
Install installFor(const Host &host);
// The file that install needs, or nothing when the release does not carry it.
// An Android release carries one .apk for each processor: an ARM one run through an x86 machine's
// translator (Waydroid, a Chromebook) stops answering, so that machine is never handed it.
std::optional<Asset> assetFor(const Release &release, Install install, const QString &cpu = {});
bool checkDue(qint64 lastCheckSecs, qint64 nowSecs);        // the automatic check: once a day
QByteArray sha256Of(const QString &path);                   // hex; empty when the file cannot be read
// What a check asked for by hand says when it fails. httpStatus is 0 when GitHub was never reached.
QString checkFailure(const QUrl &feed, bool tlsAvailable, int httpStatus);

}

// Exposed to QML as `updater`: the banner, the What's new sheet and Settings › Updates read it.
class Updater : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)      // a newer release is known
    Q_PROPERTY(bool offered READ offered NOTIFY changed)          // and the banner should say so
    Q_PROPERTY(bool installsItself READ installsItself CONSTANT)  // Update downloads, rather than opening a page
    // "" · checking · downloading · ready (restart to use it) · failed
    Q_PROPERTY(QString stage READ stage NOTIFY changed)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)   // 0..1, below 0 when the size is unknown
    Q_PROPERTY(QString status READ status NOTIFY changed)         // what the last check or install came to
    Q_PROPERTY(bool automatic READ automatic WRITE setAutomatic NOTIFY automaticChanged)
public:
    explicit Updater(Library &lib, update::Host host = update::Host::detect(), QObject *parent = nullptr);
    ~Updater() override;

    QString currentVersion() const { return m_host.version; }
    QString latestVersion() const { return m_release.version; }
    QString notes() const { return m_release.notes; }
    bool available() const { return m_release.valid(); }
    bool offered() const { return m_offered; }
    bool installsItself() const;
    QString stage() const { return m_stage; }
    qreal progress() const { return m_progress; }
    QString status() const { return m_status; }
    bool automatic() const;
    void setAutomatic(bool on);
    void setFeed(const QUrl &feed) { m_host.feed = feed; }

    // At launch: asks only when the setting is on and the last answer is a day old. Says nothing
    // when it fails, and nothing about a version that was skipped.
    void checkOnLaunch();
    Q_INVOKABLE void check();          // on demand: always asks, always says what came of it
    Q_INVOKABLE void later();          // put the banner away until the next check
    Q_INVOKABLE void skip();           // and never mention this version again, unless asked
    Q_INVOKABLE void unskip();
    Q_INVOKABLE void install();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void restart();
    Q_INVOKABLE void openReleasePage();
    // What restartRequested named could not be started (a refused UAC prompt, say): the banner
    // says so and offers the download again, instead of waiting for an installer that never came.
    void launchFailed();

signals:
    void changed();
    void progressChanged();
    void automaticChanged();
    void openUrl(const QUrl &url);                 // QML hands it to the system
    // Start this and quit: the new AppImage, or the Windows installer.
    void restartRequested(const QString &program, const QStringList &arguments);

private:
    void fetch(bool manual);
    void checked(QNetworkReply *reply, bool manual);
    void download(const update::Asset &asset, const QString &target);
    void downloaded(QNetworkReply *reply, const update::Asset &asset);
    void fail(const QString &why);
    void set(const QString &stage, const QString &status);

    Library &m_lib;
    update::Host m_host;
    update::Install m_install;
    QNetworkAccessManager m_net;
    QNetworkReply *m_reply = nullptr;
    std::unique_ptr<QSaveFile> m_file;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    update::Release m_release;
    bool m_offered = false;
    QString m_stage;
    QString m_status;
    qreal m_progress = -1;
};
