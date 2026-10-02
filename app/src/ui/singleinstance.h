#pragma once
#include <QList>
#include <QLocalServer>
#include <QObject>
#include <QStringList>
#include <QUrl>

// One Lumen per library on a desktop. Two windows on one SQLite library would each save over the
// other's pages, so a second launch ("Open with Lumen" on a PDF, or the launcher clicked again)
// hands its files to the one already running and exits; that one comes forward and opens them.
// The key is the library's folder, so a run on its own LUMEN_DATA_DIR never meets another.
// Desktop builds only: phones and tablets hand files over through the system (Platform).
class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(const QString &dataDir, QObject *parent = nullptr);

    // True when a Lumen is already running on this library: it has the files (and the launcher's
    // activation token, so the compositor lets it come forward), and this process should exit.
    bool handOver(const QList<QUrl> &files, const QString &activationToken = {}, int timeoutMs = 3000);
    // Become the running Lumen for this library. False only when another one answers on the
    // socket (two launches at once): hand over to it instead. Launches take turns through a lock
    // file beside the socket, so a socket left by a crash is replaced by exactly one of them.
    bool listen();
    QString serverName() const { return m_name; }
    // Stop being it, ahead of a restart: the Lumen started next must not hand over to this one.
    void close() { m_server.close(); }

    // The files a command line hands over (Exec=lumen %U): local files that exist, as URLs.
    // Flags, and the values of the flags that take one, are not files.
    static QList<QUrl> filesIn(const QStringList &arguments);

signals:
    void handedOver(const QList<QUrl> &files, const QString &activationToken);   // files may be empty: just come forward

private:
    void read(class QLocalSocket *socket);
    QString m_name;
    QLocalServer m_server;
};
