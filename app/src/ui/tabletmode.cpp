#include "tabletmode.h"
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QLoggingCategory>
#include <QProcess>
#include <QRegularExpression>
#include <QVariant>
#include <QTimer>

Q_LOGGING_CATEGORY(lcTablet, "lumen.tablet")

TabletMode::TabletMode(QObject *parent) : QObject(parent)
{
    queryKwin();
    QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin"), QStringLiteral("org.kde.KWin.TabletModeManager"),
                                          QStringLiteral("tabletModeChanged"), this, SLOT(onKwinTabletChanged(bool)));
    // Property-change notifications are what KWin actually emits.
    QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin"), QStringLiteral("org.freedesktop.DBus.Properties"),
                                          QStringLiteral("PropertiesChanged"), this, SLOT(onPropertiesChanged(QString,QVariantMap,QStringList)));
    refreshRotation();
}

TabletMode::~TabletMode()
{
    for (QProcess *p : findChildren<QProcess *>()) if (p->state() != QProcess::NotRunning) { p->kill(); p->waitForFinished(200); }
}

void TabletMode::queryKwin()
{
    QDBusInterface kwin(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin"), QStringLiteral("org.kde.KWin.TabletModeManager"), QDBusConnection::sessionBus());
    if (!kwin.isValid()) { m_kwinAvailable = false; return; }
    const QVariant avail = kwin.property("tabletModeAvailable"), mode = kwin.property("tabletMode");
    m_kwinAvailable = avail.isValid() && avail.toBool();
    m_kwinTablet = mode.isValid() && mode.toBool();
    if (!m_userForced && m_kwinTablet != m_tablet) { m_tablet = m_kwinTablet; }
    emit tabletChanged();
}

void TabletMode::setTablet(bool v)
{
    m_userForced = true;
    if (m_tablet == v) return;
    m_tablet = v;
    emit tabletChanged();
}

void TabletMode::rotateDisplay(const QString &to)
{
    QProcess::startDetached(QStringLiteral("kscreen-doctor"), {QStringLiteral("output.eDP-1.rotation.%1").arg(to == "none" ? "normal" : to)});
    m_rotation = to;
    emit rotationChanged();
}

void TabletMode::refreshRotation()
{
    // Asynchronous: kscreen-doctor can hang without a running KWin (tests, offscreen).
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p](int, QProcess::ExitStatus) {
        const QString out = QString::fromUtf8(p->readAllStandardOutput());
        p->deleteLater();
        const QRegularExpressionMatch m = QRegularExpression("Rotation:\\s*\\S*?(\\d)").match(out);
        if (!m.hasMatch()) return;
        static const char *names[] = {"none", "none", "left", "none", "inverted", "none", "none", "none", "right"};
        m_rotation = QString::fromLatin1(names[std::clamp(m.captured(1).toInt(), 0, 8)]);
        emit rotationChanged();
    });
    connect(p, &QProcess::errorOccurred, p, &QObject::deleteLater);
    p->start(QStringLiteral("kscreen-doctor"), {QStringLiteral("-o")});
    QTimer::singleShot(4000, p, [p] { if (p->state() != QProcess::NotRunning) p->kill(); });
}

#include <QMetaObject>
// slots for the D-Bus connections above
void TabletMode::onKwinTabletChanged(bool on) { m_kwinTablet = on; if (!m_userForced) { m_tablet = on; } emit tabletChanged(); }
void TabletMode::onPropertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &)
{
    if (iface != QLatin1String("org.kde.KWin.TabletModeManager")) return;
    if (changed.contains(QStringLiteral("tabletMode"))) onKwinTabletChanged(changed.value(QStringLiteral("tabletMode")).toBool());
}
