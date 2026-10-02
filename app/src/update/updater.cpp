#include "updater.h"
#include "storage/library.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSslSocket>
#include <QSysInfo>
#include <QVersionNumber>

namespace update {

Host Host::detect()
{
    Host host;
    host.version = QStringLiteral(LUMEN_VERSION);
#if defined(Q_OS_ANDROID)
    host.os = QStringLiteral("android");
#elif defined(Q_OS_IOS)
    host.os = QStringLiteral("ios");
#elif defined(Q_OS_WIN)
    host.os = QStringLiteral("windows");
#elif defined(Q_OS_LINUX)
    host.os = QStringLiteral("linux");
#else
    host.os = QStringLiteral("other");
#endif
    // A release carries one AppImage, for x86-64.
    if (QSysInfo::buildCpuArchitecture() == QLatin1String("x86_64")) host.appImage = qEnvironmentVariable("APPIMAGE");
    host.feed = QUrl(QStringLiteral("https://api.github.com/repos/Matinthereal/lumen-notes/releases/latest"));
    const QString feed = qEnvironmentVariable("LUMEN_UPDATE_URL");
    if (!feed.isEmpty()) host.feed = QUrl::fromUserInput(feed, QDir::currentPath());
    host.downloadDir = QDir::tempPath();
    return host;
}

Release parseRelease(const QByteArray &json)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    Release r;
    if (o.value("draft").toBool() || o.value("prerelease").toBool()) return r;
    QString tag = o.value("tag_name").toString().trimmed();
    if (tag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) tag.remove(0, 1);
    if (QVersionNumber::fromString(tag).isNull()) return r;
    r.version = tag;
    r.notes = o.value("body").toString();
    r.page = QUrl(o.value("html_url").toString());
    for (const QJsonValue &v : o.value("assets").toArray()) {
        const QJsonObject a = v.toObject();
        Asset asset;
        asset.name = a.value("name").toString();
        asset.url = QUrl(a.value("browser_download_url").toString());
        asset.size = qint64(a.value("size").toDouble());
        // "sha256:<64 hex digits>"; anything else counts as no digest.
        const QString digest = a.value("digest").toString().toLower();
        const QByteArray hex = digest.mid(7).toLatin1();
        if (digest.startsWith(QLatin1String("sha256:")) && hex.size() == 64 && QByteArray::fromHex(hex).size() == 32)
            asset.sha256 = hex;
        if (!asset.name.isEmpty() && asset.url.isValid()) r.assets.append(asset);
    }
    return r;
}

bool isNewer(const QString &tag, const QString &running)
{
    QString t = tag.trimmed();
    if (t.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) t.remove(0, 1);
    qsizetype suffix = 0;
    const QVersionNumber found = QVersionNumber::fromString(t, &suffix);
    if (found.isNull() || suffix != t.size()) return false;
    return found.normalized() > QVersionNumber::fromString(running).normalized();
}

Install installFor(const Host &host)
{
    if (host.os == QLatin1String("android")) return Install::AndroidApk;
    if (host.os == QLatin1String("windows")) return Install::WindowsSetup;
    if (host.os == QLatin1String("linux") && !host.appImage.isEmpty()) {
        // The new file is written beside the old one and renamed over it, so both must be ours.
        const QFileInfo file(host.appImage);
        if (file.isFile() && file.isWritable() && QFileInfo(file.absolutePath()).isWritable()) return Install::AppImage;
    }
    return Install::ReleasePage;
}

std::optional<Asset> assetFor(const Release &release, Install install)
{
    for (const Asset &a : release.assets) {
        const bool fits = install == Install::AppImage ? a.name == QLatin1String("Lumen-x86_64.AppImage")
                        : install == Install::WindowsSetup ? a.name.startsWith(QLatin1String("Lumen-Setup-")) && a.name.endsWith(QLatin1String(".exe"))
                        // Only the signed one: "…-android-unsigned.apk" cannot be installed.
                        : install == Install::AndroidApk ? a.name.startsWith(QLatin1String("Lumen-")) && a.name.endsWith(QLatin1String("-android.apk"))
                        : false;
        if (fits) return a;
    }
    return std::nullopt;
}

bool checkDue(qint64 lastCheckSecs, qint64 nowSecs)
{
    // A last check in the future is a clock that has been set back: ask again.
    return lastCheckSecs <= 0 || lastCheckSecs > nowSecs || nowSecs - lastCheckSecs >= 24 * 3600;
}

QByteArray sha256Of(const QString &path)
{
    QFile f(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!f.open(QIODevice::ReadOnly) || !hash.addData(&f)) return {};
    return hash.result().toHex();
}

}

Updater::Updater(Library &lib, update::Host host, QObject *parent)
    : QObject(parent), m_lib(lib), m_host(std::move(host)), m_install(update::installFor(m_host)) {}

Updater::~Updater()
{
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); }
}

bool Updater::installsItself() const
{
    return m_install == update::Install::AppImage || m_install == update::Install::WindowsSetup;
}

bool Updater::automatic() const { return m_lib.setting("update.auto", "1") == QLatin1String("1"); }

void Updater::setAutomatic(bool on)
{
    if (on == automatic()) return;
    m_lib.setSetting("update.auto", on ? "1" : "0");
    emit automaticChanged();
}

void Updater::set(const QString &stage, const QString &status)
{
    m_stage = stage;
    m_status = status;
    emit changed();
}

void Updater::fail(const QString &why) { set(QStringLiteral("failed"), why); }

void Updater::checkOnLaunch()
{
    if (!automatic() || m_host.feed.isEmpty()) return;
    if (!update::checkDue(m_lib.setting("update.lastCheck", "0").toLongLong(), QDateTime::currentSecsSinceEpoch())) return;
    fetch(false);
}

void Updater::check() { fetch(true); }

void Updater::fetch(bool manual)
{
    // An install under way, or one waiting for its restart, is not to be forgotten for a check.
    if (m_reply || m_stage == QLatin1String("downloading") || m_stage == QLatin1String("ready")) return;
    if (m_host.feed.isEmpty()) {
        if (manual) set({}, QStringLiteral("Updates are not checked during a test run."));
        return;
    }
    QNetworkRequest request(m_host.feed);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Lumen/%1").arg(m_host.version));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(15000);
    m_reply = m_net.get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = m_reply, manual] { checked(reply, manual); });
    set(QStringLiteral("checking"), manual ? QStringLiteral("Checking…") : m_status);
}

void Updater::checked(QNetworkReply *reply, bool manual)
{
    m_reply = nullptr;
    reply->deleteLater();
    // The automatic check keeps quiet when it fails: offline is an ordinary place to take notes.
    const QString before = m_status;
    if (reply->error() != QNetworkReply::NoError) {
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // Qt for Android can come without a TLS backend; then no https request can be made at all.
        const QString why = m_host.feed.scheme() == QLatin1String("https") && !QSslSocket::supportsSsl()
                                ? QStringLiteral("This build of Lumen cannot make a secure connection. New releases are on its GitHub page.")
                          : http == 403 || http == 429 ? QStringLiteral("GitHub is turning requests away just now. Try again in an hour.")
                          : http >= 400 ? QStringLiteral("GitHub answered with an error (%1). Try again later.").arg(http)
                          : QStringLiteral("Could not reach GitHub. Are you online?");
        set({}, manual ? why : before);
        return;
    }
    const update::Release found = update::parseRelease(reply->readAll());
    if (!found.valid()) {
        set({}, manual ? QStringLiteral("GitHub's answer could not be read. Try again later.") : before);
        return;
    }
    m_lib.setSetting("update.lastCheck", QString::number(QDateTime::currentSecsSinceEpoch()));
    if (!update::isNewer(found.version, m_host.version)) {
        m_release = {};
        m_offered = false;
        set({}, QStringLiteral("Lumen %1 is the newest version.").arg(m_host.version));
        return;
    }
    m_release = found;
    m_offered = manual || found.version != m_lib.setting("update.skipped", "");
    set({}, QStringLiteral("Lumen %1 is available.").arg(found.version));
}

void Updater::later()
{
    m_offered = false;
    emit changed();
}

void Updater::skip()
{
    if (!available()) return;
    m_lib.setSetting("update.skipped", m_release.version);
    later();
}

void Updater::unskip()
{
    m_lib.setSetting("update.skipped", "");
    m_offered = available();
    emit changed();
}

void Updater::openReleasePage()
{
    if (available()) emit openUrl(m_release.page);
}

void Updater::install()
{
    if (!available() || m_reply || m_stage == QLatin1String("ready")) return;
    const std::optional<update::Asset> asset = update::assetFor(m_release, m_install);
    if (!asset) { emit openUrl(m_release.page); return; }
    if (m_install == update::Install::AndroidApk) { emit openUrl(asset->url); return; }
    // What Lumen installs by itself must be the file GitHub published, bit for bit.
    if (asset->sha256.isEmpty()) {
        fail(QStringLiteral("This release has no checksum to check the download against, so Lumen will not install it by itself."));
        return;
    }
    download(*asset, m_install == update::Install::AppImage ? m_host.appImage
                                                            : QDir(m_host.downloadDir).filePath(QFileInfo(asset->name).fileName()));
}

void Updater::download(const update::Asset &asset, const QString &target)
{
    // Written beside the target and renamed over it at the end, so a download that stops half way
    // leaves the Lumen that is there untouched.
    m_file = std::make_unique<QSaveFile>(target);
    m_file->setDirectWriteFallback(false);
    if (!m_file->open(QIODevice::WriteOnly)) {
        const QString error = m_file->errorString();
        m_file.reset();
        fail(QStringLiteral("Could not write %1: %2").arg(QDir::toNativeSeparators(target), error));
        return;
    }
    m_hash.reset();
    m_progress = asset.size > 0 ? 0 : -1;
    emit progressChanged();
    QNetworkRequest request(asset.url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Lumen/%1").arg(m_host.version));
    request.setTransferTimeout(30000);          // 30 s with nothing arriving, not 30 s in all
    m_reply = m_net.get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, [this, reply = m_reply] {
        const QByteArray chunk = reply->readAll();
        m_hash.addData(chunk);
        m_file->write(chunk);
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this, size = asset.size](qint64 received, qint64 total) {
        if (total <= 0) total = size;
        m_progress = total > 0 ? qBound(0.0, double(received) / double(total), 1.0) : -1;
        emit progressChanged();
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = m_reply, asset] { downloaded(reply, asset); });
    set(QStringLiteral("downloading"), QStringLiteral("Downloading Lumen %1").arg(m_release.version));
}

void Updater::downloaded(QNetworkReply *reply, const update::Asset &asset)
{
    m_reply = nullptr;
    reply->deleteLater();
    const QByteArray rest = reply->readAll();
    m_hash.addData(rest);
    m_file->write(rest);
    const std::unique_ptr<QSaveFile> file = std::move(m_file);
    const QString target = file->fileName();
    if (reply->error() != QNetworkReply::NoError) {
        file->cancelWriting();
        fail(QStringLiteral("The download did not finish. Nothing was changed."));
        return;
    }
    if (m_hash.result().toHex() != asset.sha256) {
        file->cancelWriting();
        fail(QStringLiteral("The download did not match its checksum and was thrown away. Nothing was changed."));
        return;
    }
    if (m_install == update::Install::AppImage) {
        // chmod +x, keeping whatever else the old file allowed.
        QFile::Permissions mode = QFileInfo(target).permissions() | QFile::ExeOwner | QFile::ExeUser;
        if (mode & QFile::ReadGroup) mode |= QFile::ExeGroup;
        if (mode & QFile::ReadOther) mode |= QFile::ExeOther;
        file->setPermissions(mode);
    }
    if (!file->commit()) {
        fail(QStringLiteral("Could not write %1: %2").arg(QDir::toNativeSeparators(target), file->errorString()));
        return;
    }
    m_offered = true;       // the banner is where Restart is
    if (m_install == update::Install::WindowsSetup) {
        // packaging/windows/lumen.iss: no wizard, only its progress window; it closes a Lumen still
        // holding its files, and starts the new one when it is done because we ask it to.
        set(QStringLiteral("ready"), QStringLiteral("Starting the installer…"));
        emit restartRequested(target, {QStringLiteral("/SILENT"), QStringLiteral("/NORESTART"), QStringLiteral("/CLOSEAPPLICATIONS"), QStringLiteral("/RELAUNCH=1")});
        return;
    }
    set(QStringLiteral("ready"), QStringLiteral("Lumen %1 is in place. Restart to use it.").arg(m_release.version));
}

void Updater::cancel()
{
    if (m_stage != QLatin1String("downloading") || !m_reply) return;
    m_reply->disconnect(this);
    m_reply->abort();
    m_reply->deleteLater();
    m_reply = nullptr;
    m_file->cancelWriting();
    m_file.reset();
    set({}, QStringLiteral("Lumen %1 is available.").arg(m_release.version));
}

void Updater::restart()
{
    if (m_stage == QLatin1String("ready") && m_install == update::Install::AppImage)
        emit restartRequested(m_host.appImage, {});
}
