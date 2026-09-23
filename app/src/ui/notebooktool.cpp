#include "notebooktool.h"
#include "storage/library.h"
#include "storage/notebookfile.h"
#include <QRegularExpression>

NotebookTool::NotebookTool(Database &db, Library &lib, QObject *parent) : QObject(parent), m_db(db), m_lib(lib) {}

static QVariantMap asMap(const notebookfile::Result &r)
{
    return QVariantMap{{"ok", r.ok}, {"error", r.error}, {"name", r.name}, {"notebookId", r.notebookId},
                       {"pages", r.pages}, {"attachments", r.attachments}};
}

QVariantMap NotebookTool::exportNotebook(qint64 notebookId, const QUrl &file)
{
    const notebookfile::Result r = notebookfile::exportNotebook(m_db, notebookId, file.toLocalFile());
    if (r.ok) emit m_lib.changed();
    return asMap(r);
}

QVariantMap NotebookTool::importNotebook(const QUrl &file)
{
    const notebookfile::Result r = notebookfile::importNotebook(m_db, file.toLocalFile());
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

QString NotebookTool::describe(const QUrl &file) const { return notebookfile::describe(file.toLocalFile()); }
