#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Tablet mode (D-012): follows KWin's TabletModeManager over D-Bus when it changes, and can be
// toggled by hand (toolbar button, Ctrl+Shift+T). The hinge itself is not detectable on this
// machine (HARDWARE.md), so the manual toggle is first-class, not a fallback. Also exposes a
// rotate-display action through kscreen-doctor and the state of the on-screen keyboard.
// The KWin/kscreen-doctor pieces are Linux-only and compiled out elsewhere (see tabletmode.cpp);
// the manual toggle works everywhere.
//
// Headless (the offscreen platform, --uitest, --smoke) it never talks to the desktop: no KWin D-Bus,
// no kscreen-doctor. Tablet mode and rotation are then just the app's own state, so a test run
// cannot read or change the session the maker is working in.
class TabletMode : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool tablet READ tablet WRITE setTablet NOTIFY tabletChanged)
    Q_PROPERTY(bool kwinAvailable READ kwinAvailable NOTIFY tabletChanged)
    Q_PROPERTY(bool kwinTablet READ kwinTablet NOTIFY tabletChanged)
    Q_PROPERTY(QString rotation READ rotation NOTIFY rotationChanged)
    // What this machine has to write with, for the first run's defaults.
    Q_PROPERTY(bool penAvailable READ penAvailable CONSTANT)
    Q_PROPERTY(bool touchAvailable READ touchAvailable CONSTANT)
public:
    explicit TabletMode(bool liveSession, QObject *parent = nullptr);
    ~TabletMode() override;
    bool tablet() const { return m_tablet; }
    void setTablet(bool v);
    bool kwinAvailable() const { return m_kwinAvailable; }
    bool kwinTablet() const { return m_kwinTablet; }
    QString rotation() const { return m_rotation; }
    Q_INVOKABLE void rotateDisplay(const QString &to);   // "none" | "left" | "right" | "inverted"
    Q_INVOKABLE void refreshRotation();
    static bool penAvailable();
    static bool touchAvailable();
signals:
    void tabletChanged();
    void rotationChanged();
private slots:
    void onKwinTabletChanged(bool on);
    void onPropertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &invalidated);
private:
    void queryKwin();
    bool m_live = false;
    bool m_tablet = false, m_kwinAvailable = false, m_kwinTablet = false, m_userForced = false;
    QString m_rotation = QStringLiteral("none");
};
