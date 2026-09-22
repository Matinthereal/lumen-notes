#include "tabletmode.h"
#include <QLoggingCategory>
#include <QVariant>
#if defined(Q_OS_LINUX) && defined(LUMEN_HAVE_DBUS)
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#endif

Q_LOGGING_CATEGORY(lcTablet, "lumen.tablet")

TabletMode::TabletMode(QObject *parent) : QObject(parent)
{
#if defined(Q_OS_LINUX) && defined(LUMEN_HAVE_DBUS)
    queryKwin();
    QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin"), QStringLiteral("org.kde.KWin.TabletModeManager"),
                                          QStringLiteral("tabletModeChanged"), this, SLOT(onKwinTabletChanged(bool)));
    // Property-change notifications are what KWin actually emits.
    QDBusConnection::sessionBus().connect(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin"), QStringLiteral("org.freedesktop.DBus.Properties"),
                                          QStringLiteral("PropertiesChanged"), this, SLOT(onPropertiesChanged(QString,QVariantMap,QStringList)));
    refreshRotation();
#endif
}

TabletMode::~TabletMode()
{
#if defined(Q_OS_LINUX) && defined(LUMEN_HAVE_DBUS)
    for (QProcess *p : findChildren<QProcess *>()) if (p->state() != QProcess::NotRunning) { p->kill(); p->waitForFinished(200); }
#endif
}

void TabletMode::setTablet(bool v)
{
    m_userForced = true;
    if (m_tablet == v) return;
    m_tablet = v;
    emit tabletChanged();
}

#if defined(Q_OS_LINUX) && defined(LUMEN_HAVE_DBUS)
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
#else
// No KWin D-Bus here (not Linux, or Qt6::DBus wasn't found): tablet mode is manual-only — the
// toggle above already handles that — and display rotation has no equivalent outside Plasma.
void TabletMode::queryKwin() { }
void TabletMode::rotateDisplay(const QString &to) { m_rotation = to; emit rotationChanged(); }
void TabletMode::refreshRotation() { }
void TabletMode::onKwinTabletChanged(bool) { }
void TabletMode::onPropertiesChanged(const QString &, const QVariantMap &, const QStringList &) { }
#endif
