/**********************************************************************************

Copyright (c) 2012, Tan Yu Sheng
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

**********************************************************************************/

#include <QApplication>
#include "mainwindow.h"
#include <QPalette>
#include <QFile>
#include <QDomDocument>
#include <QStandardPaths>
#include <QSettings>
#include <QRegularExpression>
#include <QScreen>
#include <QWindow>
#include <QTimer>

int main(int argc, char *argv[])
{
    //If on Windows set Hi DPI support
    #ifdef Q_OS_WIN
        qputenv("QT_AUTO_SCREEN_SCALE_FACTOR","1");
    #endif
    QApplication a(argc, argv);
    a.setStyle("fusion");

    bool darkModeEnabled = false;

    #ifdef Q_OS_MACOS
        QFile file(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/settings.xml");
    #else
        QFile file("settings.xml");
    #endif
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QDomDocument doc;
        doc.setContent(&file);
        file.close();

        QDomNode settings = doc.namedItem("settings");
        QDomNode darkMode = settings.namedItem("darkMode");
        if (!darkMode.isNull()) {
            if (darkMode.firstChild().nodeValue() == "1") {
                darkModeEnabled = true;

                QPalette darkPalette;
                darkPalette.setColor(QPalette::Window, QColor(53,53,53));
                darkPalette.setColor(QPalette::WindowText, Qt::white);
                darkPalette.setColor(QPalette::Base, QColor(25,25,25));
                darkPalette.setColor(QPalette::AlternateBase, QColor(53,53,53));
                darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
                darkPalette.setColor(QPalette::ToolTipText, Qt::white);
                darkPalette.setColor(QPalette::Text, Qt::white);
                darkPalette.setColor(QPalette::Button, QColor(53,53,53));
                darkPalette.setColor(QPalette::ButtonText, Qt::white);
                darkPalette.setColor(QPalette::BrightText, Qt::red);
                darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));

                darkPalette.setColor(QPalette::Disabled, QPalette::Text, Qt::darkGray);
                darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, Qt::darkGray);

                darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
                darkPalette.setColor(QPalette::HighlightedText, Qt::black);

                a.setPalette(darkPalette);

                a.setStyleSheet("QToolTip { color: #ffffff; background-color: #2a82da; border: 1px solid white; }");
            }

        }

    }

    if (!darkModeEnabled) {
        //Default Fusion light palette, so the OS colour scheme doesn't leak in
        const QColor windowText = Qt::black;
        const QColor backGround = QColor(239, 239, 239);
        const QColor light = backGround.lighter(150);
        const QColor mid = backGround.darker(130);
        const QColor midLight = mid.lighter(110);
        const QColor base = Qt::white;
        const QColor disabledBase(backGround);
        const QColor dark = backGround.darker(150);
        const QColor darkDisabled = QColor(209, 209, 209).darker(110);
        const QColor text = Qt::black;
        const QColor highlight = QColor(48, 140, 198);
        const QColor highlightedText = Qt::white;
        const QColor disabledText = QColor(190, 190, 190);
        const QColor button = backGround;
        const QColor shadow = dark.darker(135);
        const QColor disabledShadow = shadow.lighter(150);
        const QColor disabledHighlight(145, 145, 145);
        QColor placeholder = text;
        placeholder.setAlpha(128);

        QPalette lightPalette(windowText, backGround, light, dark, mid, text, base);
        lightPalette.setBrush(QPalette::Midlight, midLight);
        lightPalette.setBrush(QPalette::Button, button);
        lightPalette.setBrush(QPalette::Shadow, shadow);
        lightPalette.setBrush(QPalette::HighlightedText, highlightedText);

        lightPalette.setBrush(QPalette::Disabled, QPalette::Text, disabledText);
        lightPalette.setBrush(QPalette::Disabled, QPalette::WindowText, disabledText);
        lightPalette.setBrush(QPalette::Disabled, QPalette::ButtonText, disabledText);
        lightPalette.setBrush(QPalette::Disabled, QPalette::Base, disabledBase);
        lightPalette.setBrush(QPalette::Disabled, QPalette::Dark, darkDisabled);
        lightPalette.setBrush(QPalette::Disabled, QPalette::Shadow, disabledShadow);

        lightPalette.setBrush(QPalette::Active, QPalette::Highlight, highlight);
        lightPalette.setBrush(QPalette::Inactive, QPalette::Highlight, highlight);
        lightPalette.setBrush(QPalette::Disabled, QPalette::Highlight, disabledHighlight);
        lightPalette.setBrush(QPalette::PlaceholderText, placeholder);

        a.setPalette(lightPalette);
    }

    // Read optional colors next to settings.xml; missing or invalid values use defaults.
#ifdef Q_OS_MACOS
    const QString buttonColorsPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                     + "/button-colors.ini";
#else
    const QString buttonColorsPath = "button-colors.ini";
#endif
    QSettings buttonColors(buttonColorsPath, QSettings::IniFormat);
    const QRegularExpression hexColor(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    const auto color = [&buttonColors, &hexColor](const char *key, const char *fallback) {
        const QString value = buttonColors.value(QLatin1String(key), QLatin1String(fallback)).toString().trimmed();
        return hexColor.match(value).hasMatch() ? value : QString::fromLatin1(fallback);
    };
    const auto buttonRule = [&color](const QString &selector, const char *group,
                                     const char *background, const char *border,
                                     const char *hover, const char *pressed,
                                     const char *text) {
        const QByteArray prefix = QByteArray(group) + '/';
        return QStringLiteral("%1 { background-color: %2; color: %3; border: 1px solid %4; }\n"
                              "%1:hover { background-color: %5; }\n"
                              "%1:pressed { background-color: %6; }\n")
            .arg(selector, color((prefix + "background").constData(), background),
                 color((prefix + "text").constData(), text),
                 color((prefix + "border").constData(), border),
                 color((prefix + "hover").constData(), hover),
                 color((prefix + "pressed").constData(), pressed));
    };
    QString buttonStyle = QStringLiteral("QPushButton { border-radius: 4px; padding: 2px 5px; }\n");
    buttonStyle += buttonRule(QStringLiteral("QPushButton"), "default", "#345f91", "#4977a7", "#4275ad", "#284b73", "#ffffff");
    buttonStyle += buttonRule(QStringLiteral("QPushButton[scButtonRole=\"reset\"]"), "reset", "#963d48", "#b65b65", "#b34c59", "#772c37", "#ffffff");
    buttonStyle += buttonRule(QStringLiteral("QPushButton[scButtonRole=\"swap\"]"), "swap", "#5c518d", "#7e72aa", "#7164a5", "#49406f", "#ffffff");
    buttonStyle += buttonRule(QStringLiteral("QPushButton[scButtonRole=\"apply\"]"), "apply", "#286d5c", "#438d7a", "#32866f", "#205648", "#ffffff");
    buttonStyle += QStringLiteral("QPushButton:focus { border: 2px solid %1; }\n")
        .arg(color("states/focusBorder", "#84b9ff"));
    buttonStyle += QStringLiteral("QPushButton:disabled, QPushButton[scButtonRole=\"reset\"]:disabled, "
                                  "QPushButton[scButtonRole=\"swap\"]:disabled, QPushButton[scButtonRole=\"apply\"]:disabled "
                                  "{ background-color: %1; color: %2; border-color: %3; }\n")
        .arg(color("states/disabledBackground", "#666b72"),
             color("states/disabledText", "#dddddd"), color("states/disabledBorder", "#73777c"));
    buttonStyle += QStringLiteral(R"(
        QToolBar#toolBar QToolButton { background-color: #345f91; color: white;
            border: 1px solid #4977a7; border-radius: 4px; padding: 2px 5px; }
        QToolBar#toolBar QToolButton:hover { background-color: #4275ad; }
        QToolBar#toolBar QToolButton:pressed { background-color: #284b73; }
        QToolBar#toolBar QToolButton:disabled { background-color: #666b72; color: #dddddd; }
    )");
    a.setStyleSheet(a.styleSheet() + buttonStyle);

    MainWindow mainWindow;
    mainWindow.show();
    // Center the native frame after the window manager has created it.
    QTimer::singleShot(0, &mainWindow, [&mainWindow] {
        QScreen *screen = mainWindow.windowHandle() ? mainWindow.windowHandle()->screen() : nullptr;
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (!screen) return;
        const QRect available = screen->availableGeometry();
        const QRect frame = mainWindow.frameGeometry();
        const int x = available.left() + qMax(0, (available.width() - frame.width()) / 2);
        const int y = available.top() + qMax(0, (available.height() - frame.height()) / 2);
        mainWindow.move(x, y);
    });
    return a.exec();
}
