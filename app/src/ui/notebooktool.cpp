#include "notebooktool.h"
#include "storage/library.h"
#include "storage/notebookfile.h"
#include "storage/paths.h"
#include <QFile>
#include <QRegularExpression>

NotebookTool::NotebookTool(Database &db, Library &lib, QObject *parent) : QObject(parent), m_db(db), m_lib(lib) {}

static QVariantMap asMap(const notebookfile::Result &r)
{
    return QVariantMap{{"ok", r.ok}, {"error", r.error}, {"name", r.name}, {"notebookId", r.notebookId},
                       {"pages", r.pages}, {"attachments", r.attachments}};
}

// A .lumen file is an SQLite database, and SQLite needs a path of its own. A document picked on
// Android is a content:// URI, so it is worked on as a copy in the cache.
static bool copyFile(const QString &from, const QString &to)
{
    QFile in(from), out(to);
    if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    while (!in.atEnd()) {
        const QByteArray chunk = in.read(1 << 20);
        if (chunk.isEmpty() || out.write(chunk) != chunk.size()) return false;
    }
    return true;
}

static QString scratchCopy() { return paths::cacheDir() + QStringLiteral("/picked.lumen"); }

QVariantMap NotebookTool::exportNotebook(qint64 notebookId, const QUrl &file)
{
    const QString target = file.isLocalFile() ? file.toLocalFile() : scratchCopy();
    QFile::remove(scratchCopy());
    notebookfile::Result r = notebookfile::exportNotebook(m_db, notebookId, target);
    if (r.ok && !file.isLocalFile() && !copyFile(target, file.toString())) { r.ok = false; r.error = QStringLiteral("cannot write the chosen file"); }
    QFile::remove(scratchCopy());
    if (r.ok) emit m_lib.changed();
    return asMap(r);
}

QVariantMap NotebookTool::importNotebook(const QUrl &file)
{
    notebookfile::Result r;
    if (file.isLocalFile()) r = notebookfile::importNotebook(m_db, file.toLocalFile());
    else if (copyFile(file.toString(), scratchCopy())) r = notebookfile::importNotebook(m_db, scratchCopy());
    else r.error = QStringLiteral("cannot read the chosen file");
    QFile::remove(scratchCopy());
    if (r.ok) emit m_lib.changed();
    return asMap(r);
}

QString NotebookTool::suggestedName(qint64 notebookId) const
{
    QString name;
    for (const QVariant &v : m_lib.notebooks())
        if (v.toMap().value(QStringLiteral("id")).toLongLong() == notebookId) name = v.toMap().value(QStringLiteral("name")).toString();
    name.replace(QRegularExpression(QStringLiteral("[/\\\\:*?\"<>|]")), QStringLiteral("-"));
    return (name.trimmed().isEmpty() ? QStringLiteral("notebook") : name.trimmed()) + QStringLiteral(".lumen");
}

QString NotebookTool::describe(const QUrl &file) const
{
    if (file.isLocalFile()) return notebookfile::describe(file.toLocalFile());
    if (!copyFile(file.toString(), scratchCopy())) return {};
    const QString text = notebookfile::describe(scratchCopy());
    QFile::remove(scratchCopy());
    return text;
}
