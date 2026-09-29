#pragma once
#include <QJsonObject>
#include <QString>

// A helper that runs inside the app, for builds that cannot start the Python helpers (iOS has no
// processes at all). It answers the same methods with the same JSON as the Python module of the
// same name. Calls arrive one at a time on a thread of the supervisor's own.
class InProcessWorker {
public:
    virtual ~InProcessWorker() = default;
    // A failure sets *error; the returned object is ignored then.
    virtual QJsonObject call(const QString &method, const QJsonObject &params, QString *error) = 0;
};
