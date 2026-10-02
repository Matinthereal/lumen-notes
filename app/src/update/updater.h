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
// newest release; nothing about the library or the machine goes with it.
namespace update {

struct Asset {
    QString name;
    QUrl url;
    qint64 size = 0;
    QByteArray sha256;       // lower-case hex; empty when GitHub gave no digest
};

struct Release {
    QString version;         // "0.3.0", without the tag's "v"; empty when the answer was not a release
    QString notes;           // the release's text, markdown
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
    QString appImage;        // $APPIMAGE, when Lumen runs from one
    QUrl feed;               // empty: never ask (test runs)
    QString downloadDir;     // where the Windows installer is put
    static Host detect();    // LUMEN_UPDATE_URL replaces the feed, for tests and screenshots
};

Release parseRelease(const QByteArray &json);
// "v0.3.0" against "0.2.0". A tag that is no version, or carries a suffix ("-rc1"), is never newer.
bool isNewer(const QString &tag, const QString &running);
Install installFor(const Host &host);
// The file that install needs, or nothing when the release does not carry it.
std::optional<Asset> assetFor(const Release &release, Install install);
bool checkDue(qint64 lastCheckSecs, qint64 nowSecs);        // the automatic check: once a day
QByteArray sha256Of(const QString &path);                   // hex; empty when the file cannot be read

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
    Q_INVOKABLE void later();          // put the banner away until the next automatic check
    Q_INVOKABLE void skip();           // and never mention this version again
    Q_INVOKABLE void unskip();
    Q_INVOKABLE void install();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void restart();
    Q_INVOKABLE void openReleasePage();

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
