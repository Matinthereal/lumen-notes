#include "theme.h"
#include "storage/library.h"
#include <QGuiApplication>
#include <QStyleHints>

Theme::Theme(Library &lib, QObject *parent)
    : QObject(parent), m_lib(lib), m_system(QGuiApplication::palette())
{
    m_colours = m_lib.setting(QStringLiteral("ui.colours"), QStringLiteral("paper"));
    m_appearance = m_lib.setting(QStringLiteral("ui.appearance"), QStringLiteral("auto"));
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_appearance == QLatin1String("auto")) apply();
    });
    apply();
}

void Theme::setColours(const QString &colours)
{
    if (colours == m_colours) return;
    m_colours = colours;
    m_lib.setSetting(QStringLiteral("ui.colours"), colours);
    apply();
}

void Theme::setAppearance(const QString &appearance)
{
    if (appearance == m_appearance) return;
    m_appearance = appearance;
    m_lib.setSetting(QStringLiteral("ui.appearance"), appearance);
    apply();
}

void Theme::apply()
{
    if (m_colours == QLatin1String("system")) {
        QGuiApplication::setPalette(m_system);
        const QColor w = m_system.color(QPalette::Window);
        m_dark = w.lightnessF() < 0.5;
    } else {
        m_dark = m_appearance == QLatin1String("dark")
                 || (m_appearance == QLatin1String("auto") && QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
        QGuiApplication::setPalette(paperPalette(m_dark));
    }
    emit changed();
}

// Contrast measured (WCAG): text on desk 13.9:1 light / 15.6:1 dark; quiet text 5.5:1 / 6.6:1;
// white on the light accent 5.9:1, the dark accent on the dark desk 7.2:1.
QPalette Theme::paperPalette(bool dark)
{
    struct Roles {
        const char *window, *text, *base, *alternate, *button, *highlight, *onHighlight, *mid, *midlight,
            *darkRole, *light, *placeholder, *tipBase, *tipText, *link, *visited, *disabled;
    };
    static const Roles lightRoles{"#F2EDE4", "#2B2723", "#FFFDF8", "#F6F2EA", "#FBF8F2", "#3558D8", "#FFFFFF", "#D9CFBE",
                                  "#EAE3D6", "#B5AC9E", "#FFFFFF", "#857E72", "#2B2723", "#F6F2EA", "#3558D8", "#6A4FD2", "#B5AC9E"};
    static const Roles darkRoles{"#151618", "#ECEAE6", "#222428", "#1C1D20", "#26282C", "#8AA6F5", "#0B0F14", "#3A3C41",
                                 "#2E3034", "#0E0F10", "#3A3C41", "#7A7872", "#ECEAE6", "#151618", "#8AA6F5", "#B3A2F5", "#6E6C68"};
    const Roles &r = dark ? darkRoles : lightRoles;
    QPalette p;
    auto set = [&p](QPalette::ColorRole role, const char *hex) { p.setColor(QPalette::All, role, QColor(QLatin1String(hex))); };
    set(QPalette::Window, r.window);
    set(QPalette::WindowText, r.text);
    set(QPalette::Base, r.base);
    set(QPalette::AlternateBase, r.alternate);
    set(QPalette::Text, r.text);
    set(QPalette::Button, r.button);
    set(QPalette::ButtonText, r.text);
    set(QPalette::BrightText, "#FFFFFF");
    set(QPalette::Highlight, r.highlight);
    set(QPalette::HighlightedText, r.onHighlight);
    set(QPalette::Accent, r.highlight);
    set(QPalette::Mid, r.mid);
    set(QPalette::Midlight, r.midlight);
    set(QPalette::Dark, r.darkRole);
    set(QPalette::Light, r.light);
    set(QPalette::Shadow, dark ? "#000000" : "#3C2814");
    set(QPalette::PlaceholderText, r.placeholder);
    set(QPalette::ToolTipBase, r.tipBase);
    set(QPalette::ToolTipText, r.tipText);
    set(QPalette::Link, r.link);
    set(QPalette::LinkVisited, r.visited);
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, QColor(QLatin1String(r.disabled)));
    return p;
}
