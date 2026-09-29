#pragma once
#include <QObject>
#include <QPalette>

class Library;

// Lumen's colours (ADR mynotes-003, "Paper & Lamp"): a warm paper desk, a lamp-blue accent, in light
// and dark. They go into the application palette, so every SystemPalette in the QML and every Qt
// Quick control follows without a line changing. "system" keeps the platform's own colours (a
// Plasma user's scheme, say). Appearance: auto follows the system's dark mode.
class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString colours READ colours WRITE setColours NOTIFY changed)       // paper | system
    Q_PROPERTY(QString appearance READ appearance WRITE setAppearance NOTIFY changed)  // auto | light | dark
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
public:
    explicit Theme(Library &lib, QObject *parent = nullptr);
    QString colours() const { return m_colours; }
    QString appearance() const { return m_appearance; }
    bool dark() const { return m_dark; }
    void setColours(const QString &colours);
    void setAppearance(const QString &appearance);

    static QPalette paperPalette(bool dark);

signals:
    void changed();

private:
    void apply();
    Library &m_lib;
    QPalette m_system;          // the platform's palette, from before the first apply()
    QString m_colours, m_appearance;
    bool m_dark = false;
};
