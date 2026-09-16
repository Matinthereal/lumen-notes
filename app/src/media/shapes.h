#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class Database;

// Drawn objects — line, arrow, rectangle, ellipse, triangle — that stay editable after you place
// them: outline colour, fill, thickness and size can all be changed later, which is the whole
// difference between a shape and a frozen stroke.
class Shapes : public QObject {
    Q_OBJECT
public:
    explicit Shapes(Database &db, QObject *parent = nullptr);

    Q_INVOKABLE QVariantList list(qint64 pageId) const;
    Q_INVOKABLE qint64 create(qint64 pageId, const QString &kind, double x, double y, double w, double h,
                              const QString &stroke, const QString &fill, double width);
    Q_INVOKABLE void setGeometry(qint64 id, double x, double y, double w, double h);
    Q_INVOKABLE void setStyle(qint64 id, const QString &stroke, const QString &fill, double width);
    Q_INVOKABLE void remove(qint64 id);
    Q_INVOKABLE qint64 duplicate(qint64 id);
    Q_INVOKABLE QVariantMap shape(qint64 id) const;
    Q_INVOKABLE int count(qint64 pageId) const;

signals:
    void changed(qint64 pageId);

private:
    Database &m_db;
};
