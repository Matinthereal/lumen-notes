#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Tablet mode (D-012): follows KWin's TabletModeManager over D-Bus when it changes, and can be
// toggled by hand (toolbar button, Ctrl+Shift+T). The hinge itself is not detectable on this
// machine (HARDWARE.md), so the manual toggle is first-class, not a fallback. Also exposes a
// rotate-display action through kscreen-doctor and the state of the on-screen keyboard.
class TabletMode : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool tablet READ tablet WRITE setTablet NOTIFY tabletChanged)
    Q_PROPERTY(bool kwinAvailable READ kwinAvailable NOTIFY tabletChanged)
    Q_PROPERTY(bool kwinTablet READ kwinTablet NOTIFY tabletChanged)
    Q_PROPERTY(QString rotation READ rotation NOTIFY rotationChanged)
public:
    explicit TabletMode(QObject *parent = nullptr);
    ~TabletMode() override;
    bool tablet() const { return m_tablet; }
    void setTablet(bool v);
    bool kwinAvailable() const { return m_kwinAvailable; }
    bool kwinTablet() const { return m_kwinTablet; }
    QString rotation() const { return m_rotation; }
    Q_INVOKABLE void rotateDisplay(const QString &to);   // "none" | "left" | "right" | "inverted"
    Q_INVOKABLE void refreshRotation();
signals:
    void tabletChanged();
    void rotationChanged();
private slots:
    void onKwinTabletChanged(bool on);
    void onPropertiesChanged(const QString &iface, const QVariantMap &changed, const QStringList &invalidated);
private:
    void queryKwin();
    bool m_tablet = false, m_kwinAvailable = false, m_kwinTablet = false, m_userForced = false;
    QString m_rotation = QStringLiteral("none");
};
