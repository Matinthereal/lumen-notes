#include "singleinstance.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QStandardPaths>

// The request is one line of JSON: {"files": [urls], "token": "…"}; the answer is "ok\n".
SingleInstance::SingleInstance(const QString &dataDir, QObject *parent) : QObject(parent)
{
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    QDir(runtime).mkpath(QStringLiteral("lumen"));
    const QByteArray library = QDir(dataDir).canonicalPath().toUtf8();
    const QString key = QString::fromLatin1(QCryptographicHash::hash(library.isEmpty() ? dataDir.toUtf8() : library, QCryptographicHash::Sha1).toHex().left(16));
    m_name = QDir(runtime).filePath(QStringLiteral("lumen/instance-%1.sock").arg(key));
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *s = m_server.nextPendingConnection()) {
            connect(s, &QLocalSocket::readyRead, this, [this, s] { read(s); });
            connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
            read(s);
        }
    });
}

bool SingleInstance::handOver(const QList<QUrl> &files, const QString &activationToken, int timeoutMs)
{
    QLocalSocket socket;
    socket.connectToServer(m_name);
    if (!socket.waitForConnected(timeoutMs)) return false;     // nobody there, or a socket left by a crash
    QJsonArray urls;
    for (const QUrl &f : files) urls.append(QString::fromUtf8(f.toEncoded()));
    socket.write(QJsonDocument(QJsonObject{{"files", urls}, {"token", activationToken}}).toJson(QJsonDocument::Compact) + '\n');
    socket.waitForBytesWritten(timeoutMs);
    // A Lumen that accepted the connection is running even if it is too busy to answer in time:
    // starting a second one on the same library is the thing to avoid.
    if (!socket.waitForReadyRead(timeoutMs) || !socket.readLine().startsWith("ok"))
        qWarning("lumen: the running window did not answer; it will open the files when it can");
    socket.disconnectFromServer();
    return true;
}

bool SingleInstance::listen()
{
    if (m_server.listen(m_name)) return true;
    if (m_server.serverError() != QAbstractSocket::AddressInUseError) {
        qWarning("lumen: cannot listen on %s: %s", qPrintable(m_name), qPrintable(m_server.errorString()));
        return true;          // no guard is better than no Lumen
    }
    // The socket file is there. Only remove it when nothing answers on it: removing a live one
    // (two launches at once) would leave that Lumen unreachable.
    QLocalSocket probe;
    probe.connectToServer(m_name);
    if (probe.waitForConnected(500)) return false;
    QLocalServer::removeServer(m_name);
    if (!m_server.listen(m_name)) qWarning("lumen: cannot listen on %s: %s", qPrintable(m_name), qPrintable(m_server.errorString()));
    return true;
}

void SingleInstance::read(QLocalSocket *socket)
{
    if (!socket->canReadLine()) return;
    const QJsonObject request = QJsonDocument::fromJson(socket->readLine()).object();
    socket->write("ok\n");
    socket->flush();
    QList<QUrl> files;
    for (const QJsonValue &v : request.value("files").toArray()) files << QUrl::fromEncoded(v.toString().toUtf8());
    emit handedOver(files, request.value("token").toString());
}

QList<QUrl> SingleInstance::filesIn(const QStringList &arguments)
{
    static const QStringList takesValue{QStringLiteral("--screenshot"), QStringLiteral("--shot-js"), QStringLiteral("--shot-delay")};
    QList<QUrl> out;
    for (int i = 1; i < arguments.size(); ++i) {
        const QString &a = arguments.at(i);
        if (takesValue.contains(a)) { ++i; continue; }
        if (a.startsWith(u'-')) continue;
        const QUrl url = a.contains(QLatin1String("://")) ? QUrl(a) : QUrl::fromLocalFile(QFileInfo(a).absoluteFilePath());
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile()) out << url;
    }
    return out;
}
