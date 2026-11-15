#include "theme.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>
#include <QString>

namespace presspng::gui {

namespace {

struct Colors {
    QColor window;
    QColor base;
    QColor alternate;
    QColor text;
    QColor dim;
    QColor line;
    QColor hover;
    QColor accent;
    QColor accentHover;
    QColor accentSoft;
};

Colors lightColors() {
    return {QColor("#f4f4f6"), QColor("#ffffff"), QColor("#eceef1"),
            QColor("#1c1d1f"), QColor("#8b8f96"), QColor("#d7d9de"),
            QColor("#e9ebef"), QColor("#2f6fed"), QColor("#1f5ad6"),
            QColor("#dce7ff")};
}

Colors darkColors() {
    return {QColor("#1b1c1f"), QColor("#232529"), QColor("#2a2d32"),
            QColor("#e7e8ea"), QColor("#8b9098"), QColor("#3a3d43"),
            QColor("#2c2f35"), QColor("#4c8dff"), QColor("#6ba0ff"),
            QColor("#233451")};
}

QPalette buildPalette(const Colors& c) {
    QPalette p;
    p.setColor(QPalette::Window, c.window);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.base);
    p.setColor(QPalette::AlternateBase, c.alternate);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.base);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::ToolTipBase, c.base);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::Highlight, c.accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, c.dim);
    p.setColor(QPalette::Disabled, QPalette::Text, c.dim);
    p.setColor(QPalette::Disabled, QPalette::WindowText, c.dim);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, c.dim);
    return p;
}

QString buildStyleSheet(const Colors& c) {
    const QString line = c.line.name();
    const QString base = c.base.name();
    const QString hover = c.hover.name();
    return QStringLiteral(R"(
QToolBar { border: 0; padding: 6px 8px; spacing: 4px; }
QToolBar::separator { background: %1; width: 1px; margin: 8px 6px; }
QToolButton, QPushButton {
    padding: 5px 10px; border: 1px solid %1; border-radius: 6px; background: %2;
}
QToolButton:hover, QPushButton:hover { background: %3; }
QToolButton:checked, QPushButton:checked { background: %6; border-color: %4; }
QPushButton#primary {
    background: %4; color: #ffffff; border: 0; font-weight: 600; padding: 6px 18px;
}
QPushButton#primary:hover { background: %5; }
QPushButton#primary:disabled { background: %1; color: #ffffff; }
QComboBox, QSpinBox {
    padding: 4px 6px; border: 1px solid %1; border-radius: 6px; background: %2;
}
QComboBox:focus, QSpinBox:focus { border-color: %4; }
QSlider::groove:horizontal { height: 4px; border-radius: 2px; background: %1; }
QSlider::handle:horizontal {
    width: 14px; margin: -6px 0; border-radius: 7px; background: %4;
}
QSlider::handle:horizontal:hover { background: %5; }
QStatusBar { border-top: 1px solid %1; }
QStatusBar::item { border: 0; }
QWidget#settingsPanel { background: %7; border-right: 1px solid %1; }
QLabel#caption { color: %8; font-size: 11px; font-weight: 600; letter-spacing: 1px; }
QWidget#previewStrip { border-top: 1px solid %1; }
)")
        .arg(line, base, hover, c.accent.name(), c.accentHover.name(),
             c.accentSoft.name(), c.alternate.name(), c.dim.name());
}

} // namespace

void applyTheme(QApplication& app) {
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // OS detection is wrong often enough (remote sessions, minimal WMs) that
    // an explicit override is worth having.
    const QString forced = qEnvironmentVariable("PRESSPNG_THEME").toLower();
    bool dark = app.styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    if (forced == QLatin1String("dark")) {
        dark = true;
    } else if (forced == QLatin1String("light")) {
        dark = false;
    }
    const Colors colors = dark ? darkColors() : lightColors();

    app.setPalette(buildPalette(colors));
    app.setStyleSheet(buildStyleSheet(colors));
}

} // namespace presspng::gui
