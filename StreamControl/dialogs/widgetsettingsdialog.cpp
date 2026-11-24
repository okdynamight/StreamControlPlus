/**********************************************************************************

Copyright (c) 2015, Antony Clarke
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

#include <QVBoxLayout>
#include <QLineEdit>
#include <QFormLayout>
#include <QLabel>
#include <QList>
#include <QPair>
#include <QPushButton>
#include "../portablesettings.h"
#include "twitchpointsdialog.h"

#include "widgetsettingsdialog.h"


WidgetSettingsDialog::WidgetSettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    tabWidget = new QTabWidget;

    // These are non-secret identifiers only. They do not connect the services.
    const auto addServiceTab = [this](const QString &title,
                                      const QList<QPair<QString, QString>> &fields) {
        auto *page = new QWidget(tabWidget);
        auto *layout = new QVBoxLayout(page);
        auto *note = new QLabel(tr("Settings below are reserved for future connections. Do not enter passwords or tokens here."), page);
        note->setWordWrap(true);
        layout->addWidget(note);
        auto *form = new QFormLayout;
        for (const auto &field : fields) {
            auto *edit = new QLineEdit(page);
            futureServiceFields.insert(field.first, edit);
            form->addRow(field.second, edit);
            connect(edit, &QLineEdit::textEdited, this, &WidgetSettingsDialog::saveDetails);
        }
        layout->addLayout(form);
        layout->addStretch();
        tabWidget->addTab(page, title);
        return page;
    };
    auto *tiltifyPage = new QWidget(tabWidget);
    auto *tiltifyForm = new QFormLayout(tiltifyPage);
    tiltifyClientId = new QLineEdit(tiltifyPage);
    tiltifyClientSecret = new QLineEdit(tiltifyPage);
    tiltifyClientSecret->setEchoMode(QLineEdit::Password);
    tiltifyForm->addRow(tr("Client ID:"), tiltifyClientId);
    tiltifyForm->addRow(tr("Client secret:"), tiltifyClientSecret);
    tabWidget->addTab(tiltifyPage, tr("Tiltify"));
    auto *gdqPage = new QWidget(tabWidget);
    auto *gdqForm = new QFormLayout(gdqPage);
    gdqUrl = new QLineEdit(gdqPage); gdqEvent = new QLineEdit(gdqPage);
    gdqUsername = new QLineEdit(gdqPage); gdqPassword = new QLineEdit(gdqPage);
    gdqPassword->setEchoMode(QLineEdit::Password);
    gdqForm->addRow(tr("Tracker API URL:"), gdqUrl);
    gdqForm->addRow(tr("Event ID (optional):"), gdqEvent);
    gdqForm->addRow(tr("Username (optional):"), gdqUsername);
    gdqForm->addRow(tr("Password (optional):"), gdqPassword);
    tabWidget->addTab(gdqPage, tr("GDQ Tracker"));

    auto *bracketPage = new QWidget(tabWidget);
    auto *bracketForm = new QFormLayout(bracketPage);
    bracketStartKey = new QLineEdit(bracketPage);
    bracketParryKey = new QLineEdit(bracketPage);
    bracketStartKey->setEchoMode(QLineEdit::Password);
    bracketParryKey->setEchoMode(QLineEdit::Password);
    bracketForm->addRow(tr("Start.gg API key:"), bracketStartKey);
    bracketForm->addRow(tr("Parry.gg API key:"), bracketParryKey);
    tabWidget->addTab(bracketPage, tr("Bracket"));
    auto *lockoutPage = new QWidget(tabWidget);
    auto *lockoutForm = new QFormLayout(lockoutPage);
    lockoutToken = new QLineEdit(lockoutPage);
    lockoutToken->setEchoMode(QLineEdit::Password);
    lockoutForm->addRow(tr("Socket token:"), lockoutToken);
    tabWidget->addTab(lockoutPage, tr("Lockout.live"));

    buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok
                                     | QDialogButtonBox::Cancel);

    QVBoxLayout *mainLayout = new QVBoxLayout;

    mainLayout->addWidget(tabWidget);
    mainLayout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, [this] {
        PortableSettings bracket("StreamControlPlus", "BracketImport");
        bracket.setValue("startgg/token", bracketStartKey->text());
        bracket.setValue("parry/token", bracketParryKey->text());
        PortableSettings lockout("StreamControlPlus", "LockoutViewer");
        lockout.setValue("token", lockoutToken->text());
        PortableSettings tiltify("StreamControlPlus", "Tiltify");
        tiltify.setValue("clientId", tiltifyClientId->text().trimmed());
        tiltify.setValue("clientSecret", tiltifyClientSecret->text());
        PortableSettings gdq("StreamControlPlus", "GDQTracker");
        gdq.setValue("baseUrl", gdqUrl->text().trimmed()); gdq.setValue("event", gdqEvent->text().trimmed());
        gdq.setValue("username", gdqUsername->text().trimmed()); gdq.setValue("password", gdqPassword->text());
        accept();
    });
    connect(buttonBox, SIGNAL(rejected()), this, SLOT(reject()));


    setLayout(mainLayout);

    setWindowTitle(tr("Widget Settings"));
}

void WidgetSettingsDialog::saveDetails()
{
    for (auto it = futureServiceFields.constBegin(); it != futureServiceFields.constEnd(); ++it)
        settings[it.key()] = it.value()->text();
}

void WidgetSettingsDialog::setConfig(QMap<QString, QString> newSettings) {
    settings = newSettings;
    PortableSettings bracket("StreamControlPlus", "BracketImport");
    bracketStartKey->setText(bracket.value("startgg/token").toString());
    bracketParryKey->setText(bracket.value("parry/token").toString());
    PortableSettings lockout("StreamControlPlus", "LockoutViewer");
    lockoutToken->setText(lockout.value("token").toString());
    PortableSettings tiltify("StreamControlPlus", "Tiltify");
    tiltifyClientId->setText(tiltify.value("clientId").toString());
    tiltifyClientSecret->setText(tiltify.value("clientSecret").toString());
    PortableSettings gdq("StreamControlPlus", "GDQTracker");
    gdqUrl->setText(gdq.value("baseUrl", settings.value("integrations>gdqTracker>baseUrl")).toString());
    gdqEvent->setText(gdq.value("event", settings.value("integrations>gdqTracker>event")).toString());
    gdqUsername->setText(gdq.value("username").toString()); gdqPassword->setText(gdq.value("password").toString());
    for (auto it = futureServiceFields.constBegin(); it != futureServiceFields.constEnd(); ++it)
        it.value()->setText(settings.value(it.key()));
}

QMap<QString, QString> WidgetSettingsDialog::getConfig() const
{
    return settings;
}
