#include "jsonlines.h"
#include <QJsonDocument>

void JsonLineBuffer::append(const QByteArray &bytes) { m_buf += bytes; }

bool JsonLineBuffer::next(QJsonObject &out)
{
    for (;;) {
        const int nl = m_buf.indexOf('\n');
        if (nl < 0)
            return false;
        const QByteArray line = m_buf.left(nl).trimmed();
        m_buf.remove(0, nl + 1);
        if (line.isEmpty())
            continue;
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            ++m_dropped;
            continue;
        }
        out = doc.object();
        return true;
    }
}

QByteArray JsonLineBuffer::encode(const QJsonObject &obj)
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
}
