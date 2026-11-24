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

#include <cstdlib>
#include <QSignalMapper>
#include <QToolBar>
#include <QAction>
#include <QMenu>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QSet>
#include <QToolButton>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QInputDialog>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStatusBar>
#include <QMessageBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "portablesettings.h"
#include <QJsonValue>
#include <QProcess>
#include <QScrollArea>
#include <QDir>
#include <QRadioButton>
#include <QStringListModel>
#include <QLabel>
#include <QNoDebug>
#include <QDateTime>
#include <QStandardPaths>


#include "dialogs/tiltifydialog.h"
#include "dialogs/gdqtrackerdialog.h"
#include "dialogs/broadcastcontrol.h"
#include "dialogs/bingodialog.h"
#include "dialogs/bracketimportdialog.h"
#include "dialogs/kevcygviewer.h"
#include "dialogs/lockoutviewer.h"
#include "dialogs/scheduledialog.h"
#include "configwindow.h"
#include "csv.h"
#include "teamstore.h"
#include <QCompleter>
#include <QRegularExpression>
#include "ScCompleter.h"
#include "ScLineEdit.h"
#include "ScComboBox.h"
#include "scradiogroup.h"
#include "sctsbutton.h"
#include "scsetbutton.h"
#include "widgets/providerwidget.h"
#include "widgets/providerwidgetbuilder.h"
#include "twitterwidget.h"
#include "mainwindow.h"
#include <QTimer>
#include <functional>
#include <memory>
#include <algorithm>
#include <QUrlQuery>
#include <QCryptographicHash>
#include <QSignalBlocker>
#include <QLineEdit>
#include <QScrollArea>

#ifdef Q_OS_WIN
    #include "windows.h"
#endif
#ifdef Q_OS_MACOS
    #include <Carbon/Carbon.h>
#endif
#include "win_keyhelper.h"

namespace {
// Publish a score against the last published snapshot, not the current unsaved form.
bool publishScoreOnly(const QString &outputPath, int format, const QString &id, int value,
                      WebSocketServer *server, QString *error) {
    const bool writeJson=format==SC_JSON || format==SC_Both;
    const bool writeXml=format==SC_XML || format==SC_Both;
    if(!writeJson && !writeXml){*error=QStringLiteral("Select JSON or XML output in Configuration first.");return false;}
    const QString jsonPath=outputPath+QStringLiteral("streamcontrol.json");
    const QString xmlPath=outputPath+QStringLiteral("streamcontrol.xml");
    QJsonObject snapshot; QDomDocument xml;
    if(writeJson){
        QFile input(jsonPath);
        if(!input.open(QIODevice::ReadOnly)){*error=QStringLiteral("Cannot read %1. Save the dashboard once first.").arg(jsonPath);return false;}
        QJsonParseError parse;const QJsonDocument doc=QJsonDocument::fromJson(input.readAll(),&parse);
        if(parse.error!=QJsonParseError::NoError || !doc.isObject()){*error=QStringLiteral("The existing JSON output is invalid; no score was published.");return false;}
        snapshot=doc.object();
    } else if(server) {
        QJsonParseError parse;const QJsonDocument doc=QJsonDocument::fromJson(server->lastMessage().toUtf8(),&parse);
        if(parse.error!=QJsonParseError::NoError || !doc.isObject()){*error=QStringLiteral("No published WebSocket snapshot is available. Save the dashboard once first.");return false;}
        snapshot=doc.object();
    }
    if(writeXml){
        QFile input(xmlPath);
        if(!input.open(QIODevice::ReadOnly)){*error=QStringLiteral("Cannot read %1. Save the dashboard once first.").arg(xmlPath);return false;}
        if(!xml.setContent(&input) || xml.documentElement().tagName()!=QStringLiteral("items")){*error=QStringLiteral("The existing XML output is invalid; no score was published.");return false;}
        QDomElement root=xml.documentElement();QDomElement score=root.firstChildElement(id);
        if(score.isNull()){score=xml.createElement(id);root.appendChild(score);}
        while(!score.firstChild().isNull())score.removeChild(score.firstChild());
        score.appendChild(xml.createTextNode(QString::number(value)));
    }
    snapshot.insert(id,QString::number(value));
    QSaveFile jsonFile(jsonPath),xmlFile(xmlPath);
    if(writeJson){
        const QByteArray bytes=QJsonDocument(snapshot).toJson(QJsonDocument::Indented);
        if(!jsonFile.open(QIODevice::WriteOnly) || jsonFile.write(bytes)!=bytes.size()){*error=QStringLiteral("Could not prepare score JSON: %1").arg(jsonFile.errorString());return false;}
    }
    if(writeXml){
        const QByteArray bytes=xml.toByteArray();
        if(!xmlFile.open(QIODevice::WriteOnly) || xmlFile.write(bytes)!=bytes.size()){*error=QStringLiteral("Could not prepare score XML: %1").arg(xmlFile.errorString());return false;}
    }
    if(writeJson && !jsonFile.commit()){*error=QStringLiteral("Could not publish score JSON: %1").arg(jsonFile.errorString());return false;}
    if(writeXml && !xmlFile.commit()){*error=QStringLiteral("Could not publish score XML: %1. If JSON output is enabled, it may already have been updated.").arg(xmlFile.errorString());return false;}
    if(server)server->broadcast(QString::fromUtf8(QJsonDocument(snapshot).toJson(QJsonDocument::Compact)));
    return true;
}
}

MainWindow::MainWindow()
{
    QString portableError;
    if (!PortableSettings::initialize(&portableError)) {
        QMessageBox::critical(nullptr, tr("Portable settings"), portableError);
        std::exit(EXIT_FAILURE);
    }
    if (objectName().isEmpty())
                setObjectName(QStringLiteral("MainWindow"));
    setWindowTitle(tr("StreamControl Plus"));

    //Set up signalmappers
    resetMapper = new QSignalMapper (this) ;
    swapMapper = new QSignalMapper (this) ;

    //some defaults
    resize(50, 50);
    setFixedSize(50, 50);

    QIcon icon;
    icon.addFile(QStringLiteral(":/StreamControl/icons/sclogo.png"), QSize(), QIcon::Normal, QIcon::Off);
    setWindowIcon(icon);
    setAnimated(true);
    setWindowFlags(windowFlags()^Qt::WindowMaximizeButtonHint);

    cWindow = new ConfigWindow(this);
    widgetSettingsDialog = new WidgetSettingsDialog(this);
    wsConnectionsDialog = new WebSocketConnectionsDialog(this);

    toolBar = new QToolBar(this);
    toolBar->setObjectName(QStringLiteral("toolBar"));
    toolBar->setLayoutDirection(Qt::LeftToRight);
    toolBar->setMovable(false);
    toolBar->setFloatable(false);
    addToolBar(Qt::TopToolBarArea, toolBar);

    QAction* actionSave = new QAction(this);
    actionSave->setObjectName(QStringLiteral("actionSave"));
    QIcon icon1;
    icon1.addFile(QStringLiteral(":/StreamControl/icons/fugue/bonus/icons-24/disk.png"), QSize(), QIcon::Normal, QIcon::Off);
    actionSave->setIcon(icon1);
    actionSave->setShortcuts(QKeySequence::Save);
    connect(actionSave,SIGNAL( triggered() ),this,SLOT( saveData() ));

    actionReloadLayout = new QAction(this);
    actionReloadLayout->setObjectName(QStringLiteral("actionReloadLayout"));
    QIcon reloadIcon;
    reloadIcon.addFile(QStringLiteral(":/StreamControl/icons/fugue/bonus/icons-24/arrow-circle-double.png"), QSize(), QIcon::Normal, QIcon::Off);
    actionReloadLayout->setIcon(reloadIcon);
    connect(actionReloadLayout,SIGNAL( triggered() ),this,SLOT( reloadLayout() ));


    QMenu *configMenu = new QMenu();
    auto *tiltifyDialog = new TiltifyDialog([this] { return settings.value(QStringLiteral("outputPath")); }, this);
    auto *tiltifyAction = new QAction(tr("Tiltify"), this);
    connect(tiltifyAction, &QAction::triggered, tiltifyDialog, [tiltifyDialog] { tiltifyDialog->show(); tiltifyDialog->raise(); tiltifyDialog->activateWindow(); });
    auto *broadcastControl = new BroadcastControl(this);
    broadcastControl->setObjectName(QStringLiteral("sc-remote-timers"));
    QAction *actionTimers = new QAction(tr("Timers"), this);
    connect(actionTimers, &QAction::triggered, this, [broadcastControl] {
        broadcastControl->show(); broadcastControl->raise(); broadcastControl->activateWindow();
    });
    QAction *actionAnimations = new QAction(tr("Broadcast Control"), this);
    connect(actionAnimations, &QAction::triggered, broadcastControl, &BroadcastControl::showAnimations);
    auto *bingoDialog = new BingoDialog(this);
    QAction *actionBingo = new QAction(tr("BingoSync Viewer"), this);
    connect(actionBingo, &QAction::triggered, this, [bingoDialog] { bingoDialog->show(); bingoDialog->raise(); bingoDialog->activateWindow(); });
    configMenu->addAction(actionBingo);
    auto *kevcygViewer = new KevcygViewer(this);
    auto *actionKevcyg = new QAction(tr("Kevcyg Bingo Viewer"), this);
    connect(actionKevcyg, &QAction::triggered, this, [kevcygViewer] { kevcygViewer->show(); kevcygViewer->raise(); kevcygViewer->activateWindow(); });
    configMenu->addAction(actionKevcyg);
    auto *lockoutViewer = new LockoutViewer(this);
    QAction *actionLockout = new QAction(tr("Lockout.live Viewer"), this);
    connect(actionLockout, &QAction::triggered, this, [lockoutViewer] { lockoutViewer->show(); lockoutViewer->raise(); lockoutViewer->activateWindow(); });
    configMenu->addAction(actionLockout);
    configMenu->addAction(tiltifyAction);
    auto *gdqDialog = new GdqTrackerDialog([this] { return settings.value(QStringLiteral("outputPath")); }, this);
    auto *gdqAction = new QAction(tr("GDQ Tracker"), this);
    connect(gdqAction, &QAction::triggered, gdqDialog, [gdqDialog] { gdqDialog->show(); gdqDialog->raise(); gdqDialog->activateWindow(); });
    configMenu->addAction(gdqAction);
    QAction *actionConfig = new QAction("Configuration", this);
    configMenu->addAction(actionConfig);
    QAction *actionSchedule = new QAction("Schedule", this);
    auto *scheduleDialog = new ScheduleDialog(this);
    scheduleDialog->setObjectName(QStringLiteral("sc-remote-schedule"));
    connect(actionSchedule, &QAction::triggered, scheduleDialog, &QDialog::show);
    scheduleDialog->setApplyToLayoutHandler([this, scheduleDialog](const QJsonObject &entry, bool configure) {
        QStringList fields = {"scheduled_start", "game", "category", "system", "release_year", "estimate"};
        for (int i = 1; i <= 4; ++i) fields << QString("runner_%1").arg(i) << QString("runner_%1_pronouns").arg(i)
            << QString("runner_%1_platform").arg(i) << QString("runner_%1_service").arg(i);
        QMap<QString, QString> defaults = {{"game","sr-game"},{"category","sr-category"},{"system","sr-platform"},
            {"release_year","sr-release"},{"estimate","sr-estimate"}};
        for (int i=1; i<=4; ++i) {
            defaults[QString("runner_%1").arg(i)] = QString("sr-runner-name-%1").arg(i);
            defaults[QString("runner_%1_pronouns").arg(i)] = QString("sr-runner-pronouns-%1").arg(i);
            defaults[QString("runner_%1_platform").arg(i)] = QString("sr-runner-%1-platform").arg(i);
            defaults[QString("runner_%1_service").arg(i)] = QString("sr-runner-service-%1").arg(i);
        }
        PortableSettings mappingSettings(QStringLiteral("StreamControlPlus"), QStringLiteral("ScheduleMappings"));
        const QByteArray hash=QCryptographicHash::hash(settings.value("layoutPath").toUtf8(), QCryptographicHash::Sha256).toHex();
        mappingSettings.beginGroup(QString::fromLatin1(hash));
        if (configure) {
            QDialog dialog(scheduleDialog); dialog.setWindowTitle(tr("Configure schedule mapping")); dialog.resize(650, 650);
            auto *outer=new QVBoxLayout(&dialog);
            auto *note=new QLabel(tr("Choose dashboard fields for schedule columns. Mappings are saved for this layout; missing schedule columns are left unchanged."), &dialog); note->setWordWrap(true); outer->addWidget(note);
            auto *scroll=new QScrollArea(&dialog); scroll->setWidgetResizable(true); auto *content=new QWidget(scroll); auto *form=new QFormLayout(content);
            QMap<QString,QComboBox*> boxes;
            for(const QString &field:fields) {
                auto *box=new QComboBox(content); box->addItem(tr("Do not change"),QString());
                for(auto it=widgetType.constBegin();it!=widgetType.constEnd();++it)
                    if((it.value()=="lineEdit"||it.value()=="comboBox") && widgetList.value(it.key())) box->addItem(it.key(),it.key());
                const QString value=mappingSettings.value(field,defaults.value(field)).toString(); const int idx=box->findData(value); if(idx>=0) box->setCurrentIndex(idx);
                form->addRow(field,box); boxes.insert(field,box);
            }
            scroll->setWidget(content); outer->addWidget(scroll);
            auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);outer->addWidget(buttons);
            connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
            if(dialog.exec()!=QDialog::Accepted)return;
            QSet<QString> used;
            for(const QString &field:fields){const QString id=boxes.value(field)->currentData().toString();if(!id.isEmpty() && used.contains(id)){QMessageBox::warning(scheduleDialog,tr("Schedule mapping"),tr("Each dashboard field can only be selected once; no mapping was saved."));return;}if(!id.isEmpty())used.insert(id);}
            for(const QString &field:fields)mappingSettings.setValue(field,boxes.value(field)->currentData().toString());
            return;
        }
        const auto report = [scheduleDialog](const QString &message) {
            if (scheduleDialog->property("scRemoteApply").toBool()) scheduleDialog->setProperty("scRemoteError", message);
            else QMessageBox::warning(scheduleDialog, QObject::tr("Schedule mapping"), message);
        };
        QSet<QString> assigned; QJsonArray actions;
        for(const QString &field:fields) {
            if(!entry.contains(field)) continue;
            const QString id=mappingSettings.value(field,defaults.value(field)).toString(); if(id.isEmpty())continue;
            if(assigned.contains(id)){report(tr("A dashboard field is mapped more than once. Check the mapping."));return;} assigned.insert(id);
            const QString type=widgetType.value(id); if((type!="lineEdit"&&type!="comboBox")||!widgetList.value(id)){report(tr("Dashboard field '%1' is missing. Configure the mapping for this layout.").arg(id));return;}
            const QString value=entry.value(field).toString();
            if(type=="comboBox") { auto *combo=qobject_cast<QComboBox*>(widgetList.value(id)); if(!combo || (combo->findData(value)<0 && combo->findText(value)<0 && !combo->isEditable())) {report(tr("The value '%1' is not available in '%2'. No fields were changed.").arg(value,id));return;} }
            QJsonObject action;action.insert("action","set");action.insert("id",id);action.insert("value",value);actions.append(action);
        }
        if(actions.isEmpty()){report(tr("No matching schedule fields were available. Configure the dashboard mapping."));return;}
        for(const auto &value:actions){const QString error=applyRemoteAction(value.toObject());if(!error.isEmpty()){report(tr("Could not apply: %1. Earlier fields may have changed.").arg(error));return;}}
    });
    QAction *actionSchedulePrevious = new QAction(QStringLiteral("←"), this);
    QAction *actionScheduleApply = new QAction(tr("Apply"), this);
    actionScheduleApply->setToolTip(tr("Apply current schedule entry to dashboard"));
    connect(actionScheduleApply, &QAction::triggered, scheduleDialog, &ScheduleDialog::applyCurrent);
    QAction *actionScheduleNext = new QAction(QStringLiteral("→"), this);
    actionSchedulePrevious->setToolTip(tr("Previous schedule entry"));
    actionScheduleNext->setToolTip(tr("Next schedule entry"));
    connect(actionSchedulePrevious, &QAction::triggered, scheduleDialog, &ScheduleDialog::previousEntry);
    connect(actionScheduleNext, &QAction::triggered, scheduleDialog, &ScheduleDialog::nextEntry);
    QAction *actionWidgetSettings = new QAction("Widget Settings", this);
    configMenu->addAction(actionWidgetSettings);
    QAction *actionWsConnections = new QAction("WebSocket Connections", this);
    configMenu->addAction(actionWsConnections);
    QAction *hideReloadLayout = new QAction("Hide Reload Layout Button", this);
    hideReloadLayout->setCheckable(true);
    configMenu->addAction(hideReloadLayout);
    QAction *actionAlwaysOnTop = new QAction("Always on top", this);
    actionAlwaysOnTop->setCheckable(true);
    configMenu->addAction(actionAlwaysOnTop);


    QToolButton* configButton = new QToolButton();
    configButton->setMenu(configMenu);
    configButton->setPopupMode(QToolButton::InstantPopup);
    QIcon configIcon;
    configIcon.addFile(QString::fromUtf8(":/StreamControl/icons/fugue/bonus/icons-24/gear.png"), QSize(), QIcon::Normal, QIcon::Off);
    configButton->setIcon(configIcon);
    connect(actionConfig,SIGNAL( triggered() ),this,SLOT( openConfig() ));
    connect(actionWidgetSettings,SIGNAL( triggered() ),this,SLOT( openWidgetSettings() ));
    connect(actionWsConnections,SIGNAL( triggered() ),this,SLOT( openWebSocketConnections() ));
    connect(actionAlwaysOnTop,SIGNAL(toggled(bool)),this,SLOT( toggleAlwaysOnTop(bool) ));
    connect(hideReloadLayout,SIGNAL(toggled(bool)),this,SLOT( toggleHideReloadLayout(bool) ));

    //code to add non buttons to toolbar
    QLabel* spaceLabel = new QLabel("   ");

    toolBar->addAction(actionSave);
    toolBar->addAction(actionReloadLayout);
    toolBar->addWidget(configButton);
    toolBar->addAction(actionSchedule);
    toolBar->addAction(actionSchedulePrevious);
    toolBar->addAction(actionScheduleApply);
    toolBar->addAction(actionScheduleNext);
    auto *toolbarSpacer = new QWidget(toolBar);
    toolbarSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolBar->addWidget(toolbarSpacer);
    toolBar->addAction(actionTimers);
    toolBar->addAction(actionAnimations);
    for (QAction *action : toolBar->actions())
        if (auto *button = qobject_cast<QToolButton *>(toolBar->widgetForAction(action))) button->setMinimumHeight(32);
    // Retain permanent controls when a different XML layout is loaded.
    for (QAction *action : toolBar->actions()) action->setProperty("scPersistent", true);
    toolBar->addWidget(spaceLabel);
    toolBar->setContextMenuPolicy(Qt::PreventContextMenu);

    #ifdef Q_OS_WIN
    keyPoller = new QTimer(this);
    connect(keyPoller, SIGNAL(timeout()), this, SLOT(keyPoll()));
    #endif

    hotkeyDown = false;

    loadSettings();
    loadLayout();
    loadData();

    if (wsServer) {
        wsServer->setLastMessage(saveJSON());
    }
}

MainWindow::~MainWindow()
{
    deleteHotkeys();
    //delete ui;
}

void MainWindow::keyPoll() {
    #ifdef Q_OS_WIN

    bool foundHotkey = false;

    int modifier = 0;
    if(GetAsyncKeyState(VK_LWIN) & 0x8000)
        modifier |= MOD_WIN;
    if(GetAsyncKeyState(VK_CONTROL) & 0x8000)
        modifier |= MOD_CONTROL;
    if(GetAsyncKeyState(VK_MENU) & 0x8000)
        modifier |= MOD_ALT;
    if(GetAsyncKeyState(VK_SHIFT) & 0x8000)
        modifier |= MOD_SHIFT;

    for (int i = 0; i < hotkeysIndex.count(); i++) {
        int ks = hotkeysIndex.at(i);
        int hotkeyMods = win_keyhelper::getModifiers(ks);
        int hotkeyKey = win_keyhelper::getKey(ks);
        if (hotkeyMods == modifier) {
            if(GetAsyncKeyState(hotkeyKey) & 0x8000) {
                foundHotkey = true;
                if (!hotkeyDown) {
                    performHotkey(i);
                    hotkeyDown = true;
                    qDebug() << "Performing " + hotkeys.at(i).at(0);
                }
            }
        }



    }
    if (!foundHotkey) {
        hotkeyDown = false;
    }
    #endif
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
    //windows
    #ifdef Q_OS_WIN
    if(eventType=="windows_generic_MSG" && !altHotkeyHandling)
    {
        MSG* msg=static_cast<MSG*>(message);
        if(msg->message==WM_HOTKEY)
        {
            UINT fuModifiers = (UINT) LOWORD(msg->lParam);  // key-modifier flags
            UINT uVirtKey = (UINT) HIWORD(msg->lParam);     // virtual-key code

            int hotkey = win_keyhelper::getQKS(fuModifiers,uVirtKey);

            int hotkeyIndex = hotkeysIndex.indexOf(hotkey);

            if (hotkeyIndex != -1) {
                performHotkey(hotkeyIndex);
            }

            return true;

         }
    }
    #endif
    //osx
    #ifdef Q_OS_MACOS
    if (eventType=="mac_generic_NSEvent")
    {
        //cast message
        EventRef* msg=static_cast<EventRef*>(message);
    }
    #endif
 return false;

}

void MainWindow::loadSettings() {
    PortableSettings portable(QStringLiteral("StreamControlPlus"), QStringLiteral("Main"));
    const QString storedXml = portable.value(QStringLiteral("xml")).toString();
    QString outputPath;
    QString layoutPath;
    if (!storedXml.isEmpty()) {

        QDomDocument doc;
        doc.setContent(storedXml);

        loadSettingsFromXml(doc.namedItem("settings"), settings);
        PortableSettings secrets(QStringLiteral("StreamControlPlus"), QStringLiteral("MainSecrets"));
        for (const QString &key : secrets.allKeys()) settings.insert(key, secrets.value(key).toString());

        //load old name (xsplitPath) if coming from 0.3 or lower
        outputPath = settings.value("outputPath", settings.value("xsplitPath"));
        settings["outputPath"] = outputPath;

        layoutPath = settings["layoutPath"];

        if (settings["useCDATA"] == "1") {
            useCDATA = true;
        } else {
            useCDATA = false;
            settings["useCDATA"] = "0";
        }

        saveFormat = settings["format"].toInt();

        if(saveFormat < 1 || saveFormat > 4) {
            saveFormat = SC_JSON;
        }

        settings["format"] = QString::number(saveFormat);

        if (settings["altHotkeyHandling"] == "1") {
            altHotkeyHandling = true;
        } else {
            altHotkeyHandling = false;
            settings["altHotkeyHandling"] = "0";
        }
        int socketport = settings["websocketPort"].toInt();
        if (socketport <= 0 || socketport > 65535){
            socketport = 41236;
        }
        settings["websocketPort"] = QString::number(socketport);

        // superseded by websocketPassword
        settings.remove("websocketSalt");
        settings.remove("websocketSecret");

        if (settings["websocketEnable"] == "1" && wsServer == nullptr) {
            wsServer = new WebSocketServer(static_cast<quint16>(socketport), this);
            wsServer->setBatchHandler([this](const QJsonArray &actions) {
                return runRemoteActions(actions);
            });
        }
        if (wsServer) {
            wsServer->setAuth(settings["websocketPassword"], settings["websocketControlDisable"] != "1");
        }
    } else {

        QMessageBox msgBox;
        msgBox.setText("Please be sure to configure StreamControl before you start.");
        msgBox.exec();

        outputPath = "";

        settings["outputPath"] = outputPath;
        settings["useCDATA"] = "0";
        useCDATA = false;
        settings["format"] = QString::number(SC_JSON);
        saveFormat = SC_JSON;
        settings["websocketPort"] = QString::number(41236);

        saveSettings();
    }
}


/*
    Saves the main settings XML inside the portable JSON file.
*/
void MainWindow::saveSettings() {
    QDomDocument doc ("StreamControl");
    // Allows us to wrap text in CDATA tags if the contents are invalid
    doc.implementation().setInvalidDataPolicy(QDomImplementation::ReturnNullNode);

    QDomElement settingsXML = doc.createElement("settings");
    doc.appendChild(settingsXML);

    PortableSettings secrets(QStringLiteral("StreamControlPlus"), QStringLiteral("MainSecrets"));
    foreach(const QString& key, settings.keys())
    {
        if (PortableSettings::sensitive(QStringLiteral("Main"), key)) {
            secrets.setValue(key, settings.value(key));
            continue;
        }
        QString elementName;

        QDomNode currentElement = settingsXML;

        // Split the key in to its parts and cycle through them,
        // attaching the later nodes to the previous node
        QStringList keyList = key.split('>');
        for (int i = 0; i < keyList.length(); i++)
        {
            QDomElement newElement;
            // Check if a node was already created in the process of saving an
            // earlier key - in which case it will already be a child node of
            // the node we are processing
            QDomNode node = currentElement.namedItem(keyList[i]);

            if (!node.isNull())
                newElement = node.toElement();
            else
                // Otherwise just make a new one
                newElement = doc.createElement(keyList[i]);

            // If this is the last node then add the value of the setting as text
            if (i == keyList.length() - 1)
            {
                QDomText value = doc.createTextNode(settings[key]);
                if (value.isNull()) value = doc.createCDATASection(settings[key]);
                newElement.appendChild(value);
            }

            // Add the newly created node, and set it as the next node to process
            currentElement = currentElement.appendChild(newElement);
        }
    }

    PortableSettings portable(QStringLiteral("StreamControlPlus"), QStringLiteral("Main"));
    portable.setValue(QStringLiteral("xml"), doc.toString());
}

/*
    Recursively scans the WidgetSettings node and loads any values it finds
    in to the settings as a way of supporting nested XML.
*/
void MainWindow::loadSettingsFromXml(const QDomNode& element,
                                         QMap<QString, QString>& settings,
                                         bool appendNodeName,
                                         QStringList prefix)
{
    QDomNodeList children = element.childNodes();
    if (children.length() > 0)
    {
        // Can choose to not append the name of the root node to the setting
        if (appendNodeName)
            prefix.append(element.toElement().tagName());

        for (int i = 0; i < children.length(); i++)
        {
            const QDomNode& child = children.at(i);
            if (child.isText())
                settings.insert(prefix.join('>'), element.toElement().text());
            else
                loadSettingsFromXml(child, settings, true, prefix);
        }
    }
}

void MainWindow::loadData()
{
    if (saveFormat == SC_XML) {
        loadXML();
    }
    if (saveFormat == SC_JSON || saveFormat == SC_Both) {
        loadJSON();
    }

}

void MainWindow::loadXML() {
    QFile file(settings["outputPath"] + "streamcontrol.xml");
    file.open(QIODevice::ReadOnly | QIODevice::Text);

    QDomDocument doc;
    doc.setContent(&file);

    file.close();

    QDomElement items = doc.namedItem("items").toElement();

    QMapIterator<QString, QObject *> i(widgetList);
    while (i.hasNext()) {
        i.next();
        // The event link comes from Bracket settings; H2H results belong to the current selection.
        if (i.key() == QStringLiteral("schedule-event-url") || i.key().startsWith(QStringLiteral("h2h-"))) continue;
        QString wType = widgetType[i.key()];
        QDomElement currElement = items.namedItem(i.key()).toElement();

        if (wType == "lineEdit") {
            ((ScLineEdit*)widgetList[i.key()])->setText(currElement.text());
        } else if (wType == "spinBox") {
            ((QSpinBox*)widgetList[i.key()])->setValue(currElement.text().toInt());
        } else if (wType == "tsButton") {
            ((ScTSButton*)widgetList[i.key()])->setTimeStamp(currElement.text().toInt());
        } else if (wType == "checkBox") {
            if (currElement.text() == "1") {
                ((QCheckBox*)widgetList[i.key()])->setChecked(true);
            } else {
                ((QCheckBox*)widgetList[i.key()])->setChecked(false);
            }
        } else if (wType == "comboBox" && i.key().startsWith(QStringLiteral("stage-strike-")) && !currElement.isNull()) {
            setComboValue(i.key(), currElement.text());
        } else if (wType == "radioGroup") {
            ((ScRadioGroup*)widgetList[i.key()])->checkFromValue(currElement.text());
        }
    }
}

void MainWindow::loadJSON() {
    QFile file(settings["outputPath"] + "streamcontrol.json");
    file.open(QIODevice::ReadOnly | QIODevice::Text);


    //doc.setContent(&file);
    QString input = file.readAll();
    QJsonDocument doc = QJsonDocument::fromJson(input.toUtf8());
    QJsonObject Obj = doc.object();
    qDebug() << doc.isNull();

    file.close();

    //QDomElement items = doc.namedItem("items").toElement();

    QMapIterator<QString, QObject *> i(widgetList);
    while (i.hasNext()) {
        i.next();
        // The event link comes from Bracket settings; H2H results belong to the current selection.
        if (i.key() == QStringLiteral("schedule-event-url") || i.key().startsWith(QStringLiteral("h2h-"))) continue;
        QString wType = widgetType[i.key()];

        if (wType == "lineEdit") {
            ((ScLineEdit*)widgetList[i.key()])->setText(Obj[i.key()].toString());
        } else if (wType == "spinBox") {
            ((QSpinBox*)widgetList[i.key()])->setValue(Obj[i.key()].toVariant().toInt());
        } else if (wType == "tsButton") {
            ((ScTSButton*)widgetList[i.key()])->setTimeStamp(Obj[i.key()].toInt());
        } else if (wType == "checkBox") {
            if (Obj[i.key()].toString() == "1") {
                ((QCheckBox*)widgetList[i.key()])->setChecked(true);
            } else {
                ((QCheckBox*)widgetList[i.key()])->setChecked(false);
            }
        } else if (wType == "comboBox" && i.key().startsWith(QStringLiteral("stage-strike-")) && Obj.contains(i.key())) {
            setComboValue(i.key(), Obj[i.key()].toString());
        } else if (wType == "radioGroup") {
            ((ScRadioGroup*)widgetList[i.key()])->checkFromValue(Obj[i.key()].toString());
        }
    }
}


void MainWindow::saveData()
{
    QString dataOutput;

    if (saveFormat == SC_XML || saveFormat == SC_Both) {
        QFile file(settings["outputPath"] + "streamcontrol.xml");
        file.open(QIODevice::WriteOnly | QIODevice::Text);

        dataOutput = saveXML();

        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << dataOutput;
        file.close();
    }

    if (saveFormat == SC_JSON || saveFormat == SC_Both) {
        QFile file(settings["outputPath"] + "streamcontrol.json");
        file.open(QIODevice::WriteOnly | QIODevice::Text);

        dataOutput = saveJSON();

        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << dataOutput;
        file.close();
    }

    //CLI
    if (cmdList.length() > 0) {
        doCLI();
    }

    //datasets

    saveDataSets();

    if (wsServer) {
        wsServer->broadcast(saveJSON());
    }
}


QString MainWindow::saveXML() {
    QDomDocument doc ("StreamControl");

    QDomElement items = doc.createElement("items");
    doc.appendChild(items);


    QDateTime current = QDateTime::currentDateTime();
    uint timestamp_t = current.toSecsSinceEpoch();

    QDomElement timestamp = doc.createElement("timestamp");
    items.appendChild(timestamp);

    QDomText timestampt = doc.createTextNode(QString::number(timestamp_t));;
    timestamp.appendChild(timestampt);

    QMapIterator<QString, QObject *> i(widgetList);
    while (i.hasNext()) {
        i.next();
        QString wType = widgetType[i.key()];
        QDomElement newItem = doc.createElement(i.key());
        items.appendChild(newItem);

        if (wType == "lineEdit") {
            if (useCDATA) {
                QDomCDATASection newItemt = doc.createCDATASection(((ScLineEdit*)widgetList[i.key()])->text());
                newItem.appendChild(newItemt);
            } else {
                QDomText newItemt = doc.createTextNode(((ScLineEdit*)widgetList[i.key()])->text());
                newItem.appendChild(newItemt);
            }
        } else if (wType == "spinBox") {
            QDomText newItemt = doc.createTextNode(((QSpinBox*)widgetList[i.key()])->text());
            newItem.appendChild(newItemt);
        } else if (wType == "checkBox") {
            QString checked = "0";
            if (((QCheckBox*)widgetList[i.key()])->isChecked()) {
                checked = "1";
            }
            QDomText newItemt = doc.createTextNode(checked);
            newItem.appendChild(newItemt);
        } else if (wType == "comboBox") {
            int currentIndex = ((ScComboBox*)widgetList[i.key()])->currentIndex();
            QString value;

            value = ((ScComboBox*)widgetList[i.key()])->currentText();

            int valueIndex = ((ScComboBox*)widgetList[i.key()])->findText(value);
            if (valueIndex != -1) {
                QVariant data = ((ScComboBox*)widgetList[i.key()])->itemData(currentIndex);
                if (data.isNull())
                    value = ((ScComboBox*)widgetList[i.key()])->itemText(currentIndex);
                else
                    value = data.toString();
            }

            QDomText newItemt = doc.createTextNode(value);
            newItem.appendChild(newItemt);
            auto *combo = static_cast<ScComboBox *>(widgetList[i.key()]);
            if (combo->property("scCatalogType").toString() == QStringLiteral("skins") || i.key().startsWith(QStringLiteral("stage-strike-stage-"))) {
                QDomElement image = doc.createElement(i.key() + QStringLiteral("-image"));
                image.appendChild(doc.createTextNode(combo->currentData(Qt::UserRole + 1).toString()));
                items.appendChild(image);
            }
        } else if (wType == "radioGroup") {
            QString value = ((ScRadioGroup*)widgetList[i.key()])->getCurrentRadio();

            QDomText newItemt = doc.createTextNode(value);
            newItem.appendChild(newItemt);
        } else if (wType == "tweet") {
            QVector<QMap<QString,QString> > urlArray = ((TwitterWidget*)widgetList[
                i.key()])->getURLs();
            QVector<QMap<QString,QString> > mediaArray = ((TwitterWidget*)widgetList[
                i.key()])->getMedia();
            if (useCDATA) {
                QDomElement username = doc.createElement("username");
                QDomCDATASection username_t = doc.createCDATASection(((TwitterWidget*)widgetList[
                    i.key()])->getUsername());

                username.appendChild(username_t);
                newItem.appendChild(username);

                QDomElement twittername = doc.createElement("twittername");
                QDomCDATASection twittername_t = doc.createCDATASection(((TwitterWidget*)widgetList[
                    i.key()])->getTwitterName());

                twittername.appendChild(twittername_t);
                newItem.appendChild(twittername);

                QDomElement tweetText = doc.createElement("text");
                QDomCDATASection tweetText_t = doc.createCDATASection(((TwitterWidget*)widgetList[
                    i.key()])->getTweetText());

                tweetText.appendChild(tweetText_t);
                newItem.appendChild(tweetText);

                QDomElement created = doc.createElement("created");
                QDomCDATASection created_t = doc.createCDATASection(((TwitterWidget*)widgetList[
                    i.key()])->getDate());

                created.appendChild(created_t);
                newItem.appendChild(created);

                QDomElement picFileName = doc.createElement("picFileName");
                QDomCDATASection picFileName_t = doc.createCDATASection(((TwitterWidget*)widgetList[
                    i.key()])->getProfilePicFilename());

                picFileName.appendChild(picFileName_t);
                newItem.appendChild(picFileName);

                QDomElement urlContainer = doc.createElement("urls");

                for(int i = 0; i < urlArray.length();i++) {
                    QDomElement urlItem = doc.createElement("url_" + QString::number(i));

                    QDomElement urlE = doc.createElement("url");
                    QDomCDATASection urlE_t = doc.createCDATASection(urlArray[i]["url"]);
                    urlE.appendChild(urlE_t);
                    urlItem.appendChild(urlE);

                    QDomElement displayE = doc.createElement("display_url");
                    QDomCDATASection displayE_t = doc.createCDATASection(urlArray[i]["display_url"]);
                    displayE.appendChild(displayE_t);
                    urlItem.appendChild(displayE);

                    QDomElement expandedE = doc.createElement("expanded_url");
                    QDomCDATASection expandedE_t = doc.createCDATASection(urlArray[i]["expanded_url"]);
                    expandedE.appendChild(expandedE_t);
                    urlItem.appendChild(expandedE);

                    urlContainer.appendChild(urlItem);
                }
                newItem.appendChild(urlContainer);

                QDomElement mediaContainer = doc.createElement("media");

                for(int i = 0; i < mediaArray.length();i++) {
                    QDomElement mediaItem = doc.createElement("media_" + QString::number(i));

                    QDomElement urlE = doc.createElement("url");
                    QDomCDATASection urlE_t = doc.createCDATASection(mediaArray[i]["url"]);
                    urlE.appendChild(urlE_t);
                    mediaItem.appendChild(urlE);

                    QDomElement displayE = doc.createElement("display_url");
                    QDomCDATASection displayE_t = doc.createCDATASection(mediaArray[i]["display_url"]);
                    displayE.appendChild(displayE_t);
                    mediaItem.appendChild(displayE);

                    QDomElement expandedE = doc.createElement("expanded_url");
                    QDomCDATASection expandedE_t = doc.createCDATASection(mediaArray[i]["expanded_url"]);
                    expandedE.appendChild(expandedE_t);
                    mediaItem.appendChild(expandedE);

                    QDomElement typeE = doc.createElement("type");
                    QDomCDATASection typeE_t = doc.createCDATASection(mediaArray[i]["type"]);
                    typeE.appendChild(typeE_t);
                    mediaItem.appendChild(typeE);

                    QDomElement mediaUrlE = doc.createElement("media_url");
                    QDomCDATASection mediaUrlE_t = doc.createCDATASection(mediaArray[i]["media_url"]);
                    mediaUrlE.appendChild(mediaUrlE_t);
                    mediaItem.appendChild(mediaUrlE);

                    if (i == 0 && mediaArray[i]["type"] == "photo"){

                        QDomElement fileNameE = doc.createElement("filename");
                        QDomCDATASection fileNameE_t = doc.createCDATASection(mediaArray[i]["filename"]);
                        fileNameE.appendChild(fileNameE_t);
                        mediaItem.appendChild(fileNameE);
                    }

                    mediaContainer.appendChild(mediaItem);
                }
                newItem.appendChild(mediaContainer);
            } else {
                QDomElement username = doc.createElement("username");
                QDomText username_t = doc.createTextNode(((TwitterWidget*)widgetList[
                    i.key()])->getUsername());

                username.appendChild(username_t);
                newItem.appendChild(username);

                QDomElement twittername = doc.createElement("twittername");
                QDomText twittername_t = doc.createTextNode(((TwitterWidget*)widgetList[
                    i.key()])->getTwitterName());

                twittername.appendChild(twittername_t);
                newItem.appendChild(twittername);

                QDomElement tweetText = doc.createElement("text");
                QDomText tweetText_t = doc.createTextNode(((TwitterWidget*)widgetList[
                    i.key()])->getTweetText());

                tweetText.appendChild(tweetText_t);
                newItem.appendChild(tweetText);

                QDomElement created = doc.createElement("created");
                QDomText created_t = doc.createTextNode(((TwitterWidget*)widgetList[
                    i.key()])->getDate());

                created.appendChild(created_t);
                newItem.appendChild(created);

                QDomElement picFileName = doc.createElement("picFileName");
                QDomText picFileName_t = doc.createTextNode(((TwitterWidget*)widgetList[
                    i.key()])->getProfilePicFilename());

                picFileName.appendChild(picFileName_t);
                newItem.appendChild(picFileName);

                QDomElement urlContainer = doc.createElement("urls");

                for(int i = 0; i < urlArray.length();i++) {
                    QDomElement urlItem = doc.createElement("url_" + QString::number(i));

                    QDomElement urlE = doc.createElement("url");
                    QDomText urlE_t = doc.createTextNode(urlArray[i]["url"]);
                    urlE.appendChild(urlE_t);
                    urlItem.appendChild(urlE);

                    QDomElement displayE = doc.createElement("display_url");
                    QDomText displayE_t = doc.createTextNode(urlArray[i]["display_url"]);
                    displayE.appendChild(displayE_t);
                    urlItem.appendChild(displayE);

                    QDomElement expandedE = doc.createElement("expanded_url");
                    QDomText expandedE_t = doc.createTextNode(urlArray[i]["expanded_url"]);
                    expandedE.appendChild(expandedE_t);
                    urlItem.appendChild(expandedE);

                    urlContainer.appendChild(urlItem);
                }
                newItem.appendChild(urlContainer);

                QDomElement mediaContainer = doc.createElement("media");

                for(int i = 0; i < mediaArray.length();i++) {
                    QDomElement mediaItem = doc.createElement("media_" + QString::number(i));

                    QDomElement urlE = doc.createElement("url");
                    QDomText urlE_t = doc.createTextNode(mediaArray[i]["url"]);
                    urlE.appendChild(urlE_t);
                    mediaItem.appendChild(urlE);

                    QDomElement displayE = doc.createElement("display_url");
                    QDomText displayE_t = doc.createTextNode(mediaArray[i]["display_url"]);
                    displayE.appendChild(displayE_t);
                    mediaItem.appendChild(displayE);

                    QDomElement expandedE = doc.createElement("expanded_url");
                    QDomText expandedE_t = doc.createTextNode(mediaArray[i]["expanded_url"]);
                    expandedE.appendChild(expandedE_t);
                    mediaItem.appendChild(expandedE);

                    QDomElement typeE = doc.createElement("type");
                    QDomText typeE_t = doc.createTextNode(mediaArray[i]["type"]);
                    typeE.appendChild(typeE_t);
                    mediaItem.appendChild(typeE);

                    QDomElement mediaUrlE = doc.createElement("media_url");
                    QDomText mediaUrlE_t = doc.createTextNode(mediaArray[i]["media_url"]);
                    mediaUrlE.appendChild(mediaUrlE_t);
                    mediaItem.appendChild(mediaUrlE);

                    if (i == 0 && mediaArray[i]["type"] == "photo"){

                        QDomElement fileNameE = doc.createElement("filename");
                        QDomText fileNameE_t = doc.createTextNode(mediaArray[i]["filename"]);
                        fileNameE.appendChild(fileNameE_t);
                        mediaItem.appendChild(fileNameE);
                    }

                    mediaContainer.appendChild(mediaItem);
                }
                newItem.appendChild(mediaContainer);
            }
        } else if (wType == "tsButton") {
            if (((ScTSButton*)widgetList[i.key()])->isActive()) {
                ((ScTSButton*)widgetList[i.key()])->setTimeStamp(timestamp_t);
                ((ScTSButton*)widgetList[i.key()])->setActive(false);
            }

            QDomText newItemt = doc.createTextNode(QString::number(((ScTSButton*)widgetList[i.key()])->getTimeStamp()));
            newItem.appendChild(newItemt);
        }

    }

    return doc.toString();
}

QString MainWindow::saveJSON() {

    QJsonObject Obj;

    QDateTime current = QDateTime::currentDateTime();
    uint timestamp_t = current.toSecsSinceEpoch();

    Obj["timestamp"] = QString::number(timestamp_t);

    QMapIterator<QString, QObject *> i(widgetList);
    while (i.hasNext()) {
        i.next();
        QString wType = widgetType[i.key()];

        if (wType == "lineEdit") {
            Obj[i.key()] = ((ScLineEdit*)widgetList[i.key()])->text();
        } else if (wType == "spinBox") {
            Obj[i.key()] = ((QSpinBox*)widgetList[i.key()])->text();
        } else if (wType == "checkBox") {
            QString checked = "0";
            if (((QCheckBox*)widgetList[i.key()])->isChecked()) {
                checked = "1";
            }
            Obj[i.key()] = checked;
        } else if (wType == "comboBox") {
            int currentIndex = ((ScComboBox*)widgetList[i.key()])->currentIndex();
            QString value;

            value = ((ScComboBox*)widgetList[i.key()])->currentText();

            int valueIndex = ((ScComboBox*)widgetList[i.key()])->findText(value);
            if (valueIndex != -1) {
                QVariant data = ((ScComboBox*)widgetList[i.key()])->itemData(currentIndex);
                if (data.isNull())
                    value = ((ScComboBox*)widgetList[i.key()])->itemText(currentIndex);
                else
                    value = data.toString();
            }

            Obj[i.key()] = value;
            auto *combo = static_cast<ScComboBox *>(widgetList[i.key()]);
            if (combo->property("scCatalogType").toString() == QStringLiteral("skins") || i.key().startsWith(QStringLiteral("stage-strike-stage-")))
                Obj[i.key() + QStringLiteral("-image")] = combo->currentData(Qt::UserRole + 1).toString();
        } else if (wType == "radioGroup") {
            QString value = ((ScRadioGroup*)widgetList[i.key()])->getCurrentRadio();
            Obj[i.key()] = value;
        } else if (wType == "tweet") {
            QVector<QMap<QString,QString> > urlArray = ((TwitterWidget*)widgetList[
                i.key()])->getURLs();
            QVector<QMap<QString,QString> > mediaArray = ((TwitterWidget*)widgetList[
                i.key()])->getMedia();
            QJsonObject tweet;

            tweet["username"] = ((TwitterWidget*)widgetList[
                i.key()])->getUsername();
            tweet["twittername"] = ((TwitterWidget*)widgetList[
                i.key()])->getTwitterName();
            tweet["text"] = ((TwitterWidget*)widgetList[
                i.key()])->getTweetText();
            tweet["created"] = ((TwitterWidget*)widgetList[
                i.key()])->getDate();
            tweet["picFileName"] = ((TwitterWidget*)widgetList[
                i.key()])->getProfilePicFilename();

            QJsonObject urlContainer;

            for(int it = 0; it < urlArray.length();it++) {
                QJsonObject urlItem;

                urlItem["url"] = urlArray[it]["url"];
                urlItem["display_url"] = urlArray[it]["display_url"];
                urlItem["expanded_url"] = urlArray[it]["expanded_url"];

                urlContainer["url_" + QString::number(it)] = urlItem;
            }

            tweet["urls"] = urlContainer;


            QJsonObject mediaContainer;

            for(int it = 0; it < mediaArray.length();it++) {
                QJsonObject mediaItem;

                mediaItem["url"] = mediaArray[it]["url"];
                mediaItem["display_url"] = mediaArray[it]["display_url"];
                mediaItem["expanded_url"] = mediaArray[it]["expanded_url"];
                mediaItem["type"] = mediaArray[it]["type"];
                mediaItem["media_url"] = mediaArray[it]["media_url"];

                if (it == 0 && mediaArray[it]["type"] == "photo"){

                    mediaItem["filename"] = mediaArray[it]["filename"];
                }

                mediaContainer["media_" + QString::number(it)] = mediaItem;
            }

            tweet["media"] = mediaContainer;

            if (((TwitterWidget*)widgetList[
                 i.key()])->isEmbed() == true){
                QJsonValue jsonValue;

                jsonValue = ((TwitterWidget*)widgetList[i.key()])->getJSON();

                tweet.insert(QString("tweetjson"),jsonValue);
            }

            Obj[i.key()] = tweet;

        } else if (wType == "tsButton") {
            if (((ScTSButton*)widgetList[i.key()])->isActive()) {
                ((ScTSButton*)widgetList[i.key()])->setTimeStamp(timestamp_t);
                ((ScTSButton*)widgetList[i.key()])->setActive(false);
            }
            Obj[i.key()] = QString::number(((ScTSButton*)widgetList[i.key()])->getTimeStamp());
        }

    }

    return QJsonDocument(Obj).toJson(QJsonDocument::Indented);

}

void MainWindow::doCLI() {

    qDebug() << "doing cli";

    QList<int> cmdQueue;
    QStringList varsChanged;

    QMapIterator<QString, QString> i(cmdOldValues);
    while (i.hasNext()) {
        i.next();

        QString var = i.key();
        QString oldVal = i.value();
        QString val = "";

        val = getValueByID(var);
        if (val != oldVal) {
            qDebug() << var + " has changed! value:" + val;
            cmdOldValues[var] = val;
            if (!varsChanged.contains(var)) {
                varsChanged.append(var);
            }
        }

    }

    QString var;
    foreach (var,varsChanged) {
        QList<int> cmds = cmdVars[var];
        int cmd;

        foreach (cmd,cmds) {
            if (!cmdQueue.contains(cmd)) {
                if(!cmdIgnores.at(cmd).contains(var))
                cmdQueue.append(cmd);
            }
        }
    }

    execCmdQueue(cmdQueue);

}

QString MainWindow::getValueByID(QString id) {
    QString newValue = "";
    QString wType = "";

    QStringList idSplit = id.split(".");

    if (idSplit.length() > 1) {
        wType = widgetType[idSplit.at(0)];
        if (wType == "tweet") {
                    newValue = "tweet";

        }
    } else {
        wType = widgetType[id];
        if (wType == "lineEdit") {
            newValue = ((ScLineEdit*)widgetList[id])->text();
        } else if (wType == "spinBox") {
            newValue = ((QSpinBox*)widgetList[id])->text();
        } else if (wType == "checkBox") {
            QString checked = "0";
            if (((QCheckBox*)widgetList[id])->isChecked()) {
                checked = "1";
            }
            newValue = checked;
        } else if (wType == "comboBox") {
            int currentIndex = ((ScComboBox*)widgetList[id])->currentIndex();
            QString value;

            value = ((ScComboBox*)widgetList[id])->currentText();

            int valueIndex = ((ScComboBox*)widgetList[id])->findText(value);
            if (valueIndex != -1) {
                QVariant data = ((ScComboBox*)widgetList[id])->itemData(currentIndex);
                if (data.isNull())
                    value = ((ScComboBox*)widgetList[id])->itemText(currentIndex);
                else
                    value = data.toString();
            }

            newValue = value;
        } else if (wType == "radioGroup") {
            QString value = ((ScRadioGroup*)widgetList[id])->getCurrentRadio();
            newValue = value;
        } else if (wType == "tsButton") {
            newValue = QString::number(((ScTSButton*)widgetList[id])->getTimeStamp());
        }
    }

    return newValue;
}

void MainWindow::execCmdQueue(QList<int> cmdQueue) {
    int cmd;
    //QString cmdStr;

    foreach (cmd,cmdQueue) {
        QString cmdStr = cmdList.at(cmd);
        cmdStr = parseCmd(cmdStr);
        qDebug() << cmdStr;
        QProcess *process = new QProcess(this);
        process->start(cmdStr);
    }
}

QString MainWindow::parseCmd(QString cmdStr) {
    QRegularExpression rx("(\\[\\$[\\w\\.]+\\])");
    int pos = 0;
    QStringList vars;

    /*while ((pos = rx.indexIn(cmdStr, pos)) != -1) {
        QString var = rx.cap(0).remove(0,2);
        var.chop(1);
        vars << var;
        pos += rx.matchedLength();
    }*/

    while (rx.match(cmdStr,pos).hasMatch()) {
        QString var = rx.match(cmdStr,pos).captured(0).remove(0,2);
        var.chop(1);
        vars << var;
        pos = rx.match(cmdStr,pos).capturedEnd(0);
    }

    QString var;
    foreach(var, vars) {
        cmdStr.replace("[$"+var+"]",cmdOldValues[var]);
    }
    return cmdStr;
}

void MainWindow::resetFields(QString widget)
{

    QListIterator<QString> i(resetList[widget]);
    while (i.hasNext()) {
        QString key = i.next();
        if (widgetType[key] == "spinBox") {
            ((QSpinBox*)widgetList[key])->setValue(0);
        } else if (widgetType[key] == "lineEdit") {
            ((ScLineEdit*)widgetList[key])->setText("");
        } else if (widgetType[key] == "checkBox") {
            ((QCheckBox*)widgetList[key])->setChecked(false);
        } else if (widgetType[key] == "comboBox") {
            ((ScComboBox*)widgetList[key])->setCurrentIndex(0);
        }
    }
}

void MainWindow::swapFields(QString widget)
{

    QString swSet1 = swapSets[widget][0];
    QString swSet2 = swapSets[widget][1];


    QList<QString> swList1 = swapList[swSet1];
    QList<QString> swList2 = swapList[swSet2];

    for (int i = 0; i < swList1.size(); ++i) {
        QString currField = swList1[i];
        QString newField = swList2[i];
        QString tempData;

        if (widgetType[currField] != widgetType[newField]){
            return;
        }

        if (widgetType[currField] == "lineEdit"){
            tempData = ((ScLineEdit*)widgetList[currField])->text();
            ((ScLineEdit*)widgetList[currField])->setText(((ScLineEdit*)widgetList[newField])->text());
            ((ScLineEdit*)widgetList[newField])->setText(tempData);
        } else if (widgetType[currField] == "spinBox") {
            tempData = ((QSpinBox*)widgetList[currField])->text();
            ((QSpinBox*)widgetList[currField])->setValue(((QSpinBox*)widgetList[newField])->text().toInt());
            ((QSpinBox*)widgetList[newField])->setValue(tempData.toInt());
        } else if (widgetType[currField] == "checkBox") {
            bool tempCheck = ((QCheckBox*)widgetList[currField])->isChecked();
            ((QCheckBox*)widgetList[currField])->setChecked(((QCheckBox*)widgetList[newField])->isChecked());
            ((QCheckBox*)widgetList[newField])->setChecked(tempCheck);
        } else if (widgetType[currField] == "comboBox") {
            ScComboBox *currComboBox = ((ScComboBox*)widgetList[currField]);
            ScComboBox *newComboBox  = ((ScComboBox*)widgetList[newField]);
            if (!currComboBox->isEditable()) {
                if (currComboBox->findText(newComboBox->currentText()) == -1)
                    return;
            }
            if (!newComboBox->isEditable()) {
                if (newComboBox->findText(currComboBox->currentText()) == -1)
                    return;
            }
            QString tempText = newComboBox->currentText();
            newComboBox->setCurrentText(currComboBox->currentText());
            currComboBox->setCurrentText(tempText);
        } else if (widgetType[currField] == "tsButton") {
            uint tempStamp = ((ScTSButton*)widgetList[currField])->getTimeStamp();
            ((ScTSButton*)widgetList[currField])->setTimeStamp(((ScTSButton*)widgetList[newField])->getTimeStamp());
            ((ScTSButton*)widgetList[newField])->setTimeStamp(tempStamp);
        }
     }

}

void MainWindow::openConfig() {
    cWindow->setConfig(settings);
    cWindow->show();

    if (cWindow->exec() == 1) {
        settings = cWindow->getConfig();

        useCDATA = (settings["useCDATA"] == "1");
        altHotkeyHandling = (settings["altHotkeyHandling"] == "1");
        saveFormat = settings["format"].toInt();

        if(saveFormat < 1 || saveFormat > 3)
            saveFormat = SC_JSON;

        if (wsServer) {
            wsServer->setAuth(settings["websocketPassword"], settings["websocketControlDisable"] != "1");
        }

        saveSettings();
        loadLayout();
        loadData();
    }
}

void MainWindow::openWidgetSettings() {
    widgetSettingsDialog->setConfig(settings);
    widgetSettingsDialog->show();

    if (widgetSettingsDialog->exec() == 1) {
        settings = widgetSettingsDialog->getConfig();
        saveSettings();
    }
}

void MainWindow::openWebSocketConnections() {
    wsConnectionsDialog->setServer(wsServer);
    wsConnectionsDialog->show();
    wsConnectionsDialog->raise();
    wsConnectionsDialog->activateWindow();
}

void MainWindow::toggleAlwaysOnTop(bool on_top) {

    #ifdef Q_OS_WIN
    HWND hWnd = (HWND)this->winId();

    if (on_top)
        SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    else
        SetWindowPos(hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    #else
    Qt::WindowFlags oldflags = windowFlags(), newflags;

    if( on_top )
        newflags = oldflags | Qt::WindowStaysOnTopHint;
    else
        newflags = oldflags & ~Qt::WindowStaysOnTopHint;

    setWindowFlags( newflags );
    show();
    #endif
}

void MainWindow::toggleHideReloadLayout(bool hide ) {
    if (hide) {
        actionReloadLayout->setVisible(false);
    } else {
        actionReloadLayout->setVisible(true);
    }
}

void MainWindow::loadLayout() {

    layoutIterator = 0;

    // Stop callbacks belonging to the old layout before clearing its widget maps.
    // QMainWindow::setCentralWidget normally deletes the previous widget later.
    if (QWidget *oldLayout = takeCentralWidget()) {
        oldLayout->blockSignals(true);
        for (QObject *child : oldLayout->findChildren<QObject *>()) child->blockSignals(true);
        delete oldLayout;
    }
    clearMaps();
    //keyPoller->stop();
    deleteHotkeys();
    //add global save hotkey first
    addHotkey("CTRL+ALT+SHIFT+S","Main","Save");

    QStringList errors;
    QString parseError;
    int parseErrorLine;
    int parseErrorColumn;

    QDomDocument doc;

    QFile file(settings["layoutPath"]);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {

        doc.setContent(&file,&parseError,&parseErrorLine,&parseErrorColumn);

        file.close();
    } else {

        doc = getDefaultLayout();

    }

    if (parseError.length() > 0) {
        errors << "XML Error:\n" + parseError + "\n\nLine:" + QString::number(parseErrorLine) + "\nColumn:" + QString::number(parseErrorColumn);
    } else {
        errors << checkLayout(doc);
    }

    if (errors.length() > 0) {

        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Warning);
        QString errorMessage = errors.join("\n");
        errorMessage += "\n\nLoading default layout";
        msgBox.setText(errorMessage);
        msgBox.exec();

        doc = getDefaultLayout();
    }

    QDomElement layout = doc.namedItem("layout").toElement();

    //Set up the main window
    int layoutWidth = layout.attribute("width").toInt();
    int layoutHeight = layout.attribute("height").toInt();

    if (layoutWidth < 50) {
        layoutWidth = 50;
    }
    if (layoutHeight < 50) {
        layoutHeight = 50;
    }


    // Leave enough room for the permanent broadcast controls in narrow layouts.
    layoutWidth = qMax(layoutWidth, 700);
    resize(layoutWidth, layoutHeight);
    setFixedSize(layoutWidth, layoutHeight);

    if(layout.attribute("tabbed") == "1") {
        centralWidget = new QTabWidget(this);
    } else {
        centralWidget = new QWidget(this);
    }

    setWidgetFontSize(centralWidget, layout.attribute("fontSize", 0).toInt());

    setCentralWidget(centralWidget);

    if(layout.attribute("tabbed") == "1"){
        parseTabLayout(layout,centralWidget);
    } else {
        parseLayout(layout, centralWidget);
    }

    //Do ToolBar
    QDomNode toolBarNode = layout.namedItem("toolBar");

    if (!toolBarNode.isNull()) {
        parseToolBar(toolBarNode);
    }

    QDomNode cliNode = layout.namedItem("cli");

    if (!cliNode.isNull()) {
        parseCLI(cliNode);
    }

    // Populate stage choices from the shared catalog using the layout's game selector.
    {
        auto *stageGame = qobject_cast<QComboBox *>(widgetList.value(QStringLiteral("stage-strike-game-select"), nullptr));
        if (stageGame) {
            const QString path = QFileInfo(settings.value(QStringLiteral("layoutPath"))).dir().filePath(QStringLiteral("character-catalog.json"));
            QFile file(path);
            QJsonObject stages;
            if (file.open(QIODevice::ReadOnly)) {
                const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                if (doc.isObject()) stages = doc.object().value(QStringLiteral("stages")).toObject();
            }
            QList<QComboBox *> boxes;
            for (int i = 1; i <= 9; ++i) {
                auto *box = qobject_cast<QComboBox *>(widgetList.value(QStringLiteral("stage-strike-stage-%1").arg(i), nullptr));
                if (box) boxes.append(box);
            }
            const auto updateStages = [stageGame, boxes, stages] {
                const QString game = stageGame->currentData().toString();
                const QJsonArray choices = stages.value(game).toArray();
                for (QComboBox *box : boxes) {
                    const QString selected = box->currentData().toString();
                    box->clear();
                    box->addItem(QObject::tr("Select stage"), QString());
                    for (const QJsonValue &entry : choices) {
                        const QJsonObject item = entry.toObject();
                        const QString name = item.value(QStringLiteral("name")).toString();
                        const QString value = item.value(QStringLiteral("value")).toString();
                        if (!name.isEmpty() && !value.isEmpty()) {
                            box->addItem(name, value);
                            const QJsonArray images = item.value(QStringLiteral("images")).toArray();
                            if (!images.isEmpty()) box->setItemData(box->count() - 1, images.first().toString(), Qt::UserRole + 1);
                        }
                    }
                    const int index = box->findData(selected);
                    if (index >= 0) box->setCurrentIndex(index);
                }
            };
            connect(stageGame, qOverload<int>(&QComboBox::currentIndexChanged), stageGame,
                    [updateStages](int) { updateStages(); });
            updateStages();
        }
    }

    // Optional team database: present only on layouts with the matching team widgets.
    // The team CSV stores player names; details remain in the existing player dataset.
    {
        const QString teamPath=QFileInfo(settings.value("layoutPath")).dir().filePath(QStringLiteral("dataset-teams.csv"));
        const auto teamField=[this](int team,const QString &field) -> ScLineEdit * {
            return qobject_cast<ScLineEdit *>(widgetList.value(QStringLiteral("teams-team-%1-%2").arg(team).arg(field)));
        };
        const auto teamName=[teamField](int team) { return teamField(team,QStringLiteral("name")); };
        auto *first=teamName(1); auto *second=teamName(2);
        if(first && second && !settings.value("layoutPath").isEmpty()) {
            QList<TeamRecord> initial; QString initialError;
            TeamStore::read(teamPath,&initial,&initialError);
            QStringList names; for(const TeamRecord &r:initial) names.append(r.name);
            auto *firstCompleter=new QCompleter(names,first); auto *secondCompleter=new QCompleter(names,second);
            for(auto *c : {firstCompleter,secondCompleter}) { c->setCaseSensitivity(Qt::CaseInsensitive); c->setFilterMode(Qt::MatchContains); c->setCompletionMode(QCompleter::PopupCompletion); }
            first->setCompleter(firstCompleter); second->setCompleter(secondCompleter);
            const auto refreshNames=[teamPath, firstCompleter,secondCompleter]() {
                QList<TeamRecord> all;QString err;if(!TeamStore::read(teamPath,&all,&err))return;
                QStringList choices;for(const TeamRecord &r:all)choices.append(r.name);
                for(auto *c : {firstCompleter,secondCompleter}) {
                    auto *model=qobject_cast<QStringListModel *>(c->model()); if(model)model->setStringList(choices);
                }
            };
            const auto loadTeam=[this, teamPath,teamField,teamName](int team,const QString &requested) {
                QList<TeamRecord> all;QString error;
                if(!TeamStore::read(teamPath,&all,&error)) { QMessageBox::warning(this,tr("Team database"),tr("Could not read %1: %2").arg(teamPath,error));return; }
                int found=-1; for(int i=0;i<all.size();++i)if(all.at(i).name.compare(requested.trimmed(),Qt::CaseInsensitive)==0){found=i;break;}
                if(found<0){QMessageBox::information(this,tr("Team database"),tr("Team '%1' was not found in %2.").arg(requested,teamPath));return;}
                const TeamRecord record=all.at(found);
                auto *color=teamField(team,QStringLiteral("color")); if(!color)return;
                // Resolve all widgets before changing any fields.
                for(int i=1;i<=9;++i)if(!teamField(team,QStringLiteral("player-%1").arg(i)) || !teamField(team,QStringLiteral("player-%1-prefix").arg(i)) || !teamField(team,QStringLiteral("player-%1-pronouns").arg(i))) { QMessageBox::warning(this,tr("Team database"),tr("The Teams layout is missing player fields."));return; }
                teamName(team)->setText(record.name);color->setText(record.color);
                if(auto *shortName=teamField(team,QStringLiteral("short")))shortName->setText(record.shortName);
                QStringList unmatched;
                for(int i=1;i<=9;++i) {
                    const QString base=QStringLiteral("teams-team-%1-player-%2").arg(team).arg(i);
                    auto *name=qobject_cast<ScLineEdit *>(widgetList.value(base)); auto *prefix=qobject_cast<ScLineEdit *>(widgetList.value(base+"-prefix")); auto *pronouns=qobject_cast<ScLineEdit *>(widgetList.value(base+"-pronouns"));
                    const QString player=record.players.value(i-1);name->setText(player);prefix->clear();pronouns->clear(); if(player.trimmed().isEmpty())continue;
                    const QString playerSet=name->getDataSetName();
                    if(playerSet.isEmpty() || prefix->getDataSetName()!=playerSet || pronouns->getDataSetName()!=playerSet){unmatched<<player;continue;}
                    const QList<QStringList> data=dataSets.value(playerSet);
                    const int nameCol=dataAssoc.value(base,-1), prefixCol=dataAssoc.value(base+"-prefix",-1), pronounCol=dataAssoc.value(base+"-pronouns",-1);
                    if(nameCol<0||prefixCol<0||pronounCol<0||nameCol>=data.size()||prefixCol>=data.size()||pronounCol>=data.size()){unmatched<<player;continue;}
                    const QStringList &playerNames=data.at(nameCol); int index=-1, matches=0;
                    for(int j=0;j<playerNames.size();++j)if(playerNames.at(j).compare(player,Qt::CaseInsensitive)==0){index=j;++matches;}
                    if(matches!=1||index>=data.at(prefixCol).size()||index>=data.at(pronounCol).size()){unmatched<<player;continue;}
                    prefix->setText(data.at(prefixCol).at(index));pronouns->setText(data.at(pronounCol).at(index));
                }
                if(!unmatched.isEmpty())QMessageBox::warning(this,tr("Player lookup"),tr("These players were missing or duplicated in the player dataset. Their prefix and pronouns were left blank:\n%1").arg(unmatched.join("\n")));
            };
            const auto saveTeam=[this,teamPath,teamField,teamName,refreshNames](int team) {
                auto *name=teamName(team); auto *color=teamField(team,QStringLiteral("color"));if(!name||!color)return;
                auto *shortField=teamField(team,QStringLiteral("short"));
                TeamRecord record;record.name=name->text().trimmed();record.color=color->text().trimmed();if(shortField)record.shortName=shortField->text().trimmed();
                if(record.name.isEmpty()){QMessageBox::warning(this,tr("Team database"),tr("Enter a team name first."));return;}
                if(!record.color.isEmpty()&&!QRegularExpression(QStringLiteral("^#[0-9a-fA-F]{6}$")).match(record.color).hasMatch()){QMessageBox::warning(this,tr("Team database"),tr("Use a team color in #RRGGBB format, or leave it blank."));return;}
                for(int i=1;i<=9;++i){auto *player=teamField(team,QStringLiteral("player-%1").arg(i));if(!player){QMessageBox::warning(this,tr("Team database"),tr("The Teams layout is missing player fields."));return;}record.players.append(player->text().trimmed());}
                QList<TeamRecord> all;QString error;if(!TeamStore::read(teamPath,&all,&error)){QMessageBox::warning(this,tr("Team database"),tr("Could not read %1: %2. Nothing was saved.").arg(teamPath,error));return;}
                int index=-1;for(int i=0;i<all.size();++i)if(all.at(i).name.compare(record.name,Qt::CaseInsensitive)==0){index=i;break;}
                if(index>=0){const TeamRecord old=all.at(index);if(!shortField)record.shortName=old.shortName;if(old.name==record.name && old.color==record.color && old.shortName==record.shortName && old.players==record.players){QMessageBox::information(this,tr("Team database"),tr("This team already matches the saved record."));return;}
                    const QString comparison=tr("Replace the existing team?\n\nSaved short name: %1\nNew short name: %2\n\nSaved color: %3\nNew color: %4\n\nSaved players:\n%5\n\nNew players:\n%6").arg(old.shortName,record.shortName,old.color,record.color,old.players.join("\n"),record.players.join("\n"));
                    if(QMessageBox::question(this,tr("Update team"),comparison,QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;all[index]=record;
                }else all.append(record);
                if(!TeamStore::write(teamPath,all,&error)){QMessageBox::warning(this,tr("Team database"),tr("Could not save %1: %2").arg(teamPath,error));return;}
                refreshNames();QMessageBox::information(this,tr("Team database"),tr("Saved '%1' to %2.").arg(record.name,teamPath));
            };
            const auto deleteTeam=[this,teamPath,teamName,refreshNames](int team) {
                auto *nameField=teamName(team);
                if(!nameField) return;
                const QString requested=nameField->text().trimmed();
                if(requested.isEmpty()){QMessageBox::warning(this,tr("Team database"),tr("Enter the name of the saved team to delete."));return;}
                QList<TeamRecord> all; QString error;
                if(!TeamStore::read(teamPath,&all,&error)){QMessageBox::warning(this,tr("Team database"),tr("Could not read %1: %2").arg(teamPath,error));return;}
                int index=-1;for(int i=0;i<all.size();++i)if(all.at(i).name.compare(requested,Qt::CaseInsensitive)==0){index=i;break;}
                if(index<0){QMessageBox::information(this,tr("Team database"),tr("Team '%1' was not found.").arg(requested));return;}
                const TeamRecord selected=all.at(index);
                const QString savedName=selected.name;
                if(QMessageBox::question(this,tr("Delete saved team"),tr("Delete '%1' from the team database? The player database and current dashboard fields will remain unchanged.").arg(savedName),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
                if(!TeamStore::read(teamPath,&all,&error)){QMessageBox::warning(this,tr("Team database"),tr("Could not re-read %1: %2").arg(teamPath,error));return;}
                index=-1;for(int i=0;i<all.size();++i)if(all.at(i).name.compare(savedName,Qt::CaseInsensitive)==0){index=i;break;}
                if(index<0 || all.at(index).name!=selected.name || all.at(index).color!=selected.color || all.at(index).shortName!=selected.shortName || all.at(index).players!=selected.players){QMessageBox::warning(this,tr("Team database"),tr("The saved team changed while the confirmation was open. Nothing was deleted."));return;}
                all.removeAt(index);
                if(!TeamStore::write(teamPath,all,&error)){QMessageBox::warning(this,tr("Team database"),tr("Could not delete team from %1: %2").arg(teamPath,error));return;}
                refreshNames();QMessageBox::information(this,tr("Team database"),tr("Deleted '%1' from the team database.").arg(savedName));
            };
            for(int team=1;team<=2;++team){
                auto *load=qobject_cast<QPushButton *>(visualList.value(QStringLiteral("teams-load-%1").arg(team)));
                auto *save=qobject_cast<QPushButton *>(visualList.value(QStringLiteral("teams-save-%1").arg(team)));
                if(load)connect(load,&QPushButton::clicked,load,[loadTeam,team,teamName]{loadTeam(team,teamName(team)->text());});
                if(save)connect(save,&QPushButton::clicked,save,[saveTeam,team]{saveTeam(team);});
                auto *del=qobject_cast<QPushButton *>(visualList.value(QStringLiteral("teams-delete-%1").arg(team)));
                if(del)connect(del,&QPushButton::clicked,del,[deleteTeam,team]{deleteTeam(team);});
                QCompleter *c=team==1?firstCompleter:secondCompleter;
                connect(c,qOverload<const QString &>(&QCompleter::activated),teamName(team),[loadTeam,team](const QString &text){loadTeam(team,text);});
            }
        }
    }

    // Load shared game/character entries once per catalog, relative to the layout file.
    QMap<QString, QJsonObject> characterCatalogs;
    QSet<QString> invalidCatalogs;
    for (auto it = widgetType.cbegin(); it != widgetType.cend(); ++it) {
        if (it.value() != QStringLiteral("comboBox")) continue;
        auto *box = qobject_cast<ScComboBox *>(widgetList.value(it.key(), nullptr));
        if (!box) continue;
        const QString filename = box->property("scCatalogFile").toString();
        if (filename.isEmpty()) continue;
        const QString path = QFileInfo(settings.value(QStringLiteral("layoutPath"))).dir().filePath(filename);
        if (!characterCatalogs.contains(path) && !invalidCatalogs.contains(path)) {
            QFile file(path); QJsonParseError error;
            if (!file.open(QIODevice::ReadOnly)) { invalidCatalogs.insert(path); }
            else {
                const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
                if (error.error != QJsonParseError::NoError || !document.isObject() || !document.object().value(QStringLiteral("games")).isObject()) invalidCatalogs.insert(path);
                else characterCatalogs.insert(path, document.object().value(QStringLiteral("games")).toObject());
            }
        }
        if (invalidCatalogs.contains(path)) continue;
        if (box->property("scCatalogType").toString() == QStringLiteral("skins")) continue;
        QVariantList items;
        QVariantMap placeholder; placeholder.insert(QStringLiteral("text"), tr("Select character")); placeholder.insert(QStringLiteral("value"), QString()); placeholder.insert(QStringLiteral("when"), QString()); items.append(placeholder);
        const QJsonObject games = characterCatalogs.value(path);
        for (auto game = games.constBegin(); game != games.constEnd(); ++game) {
            for (const QJsonValue &character : game.value().toArray()) {
                const QJsonObject item = character.toObject();
                const QString name = item.value(QStringLiteral("name")).toString();
                const QString value = item.value(QStringLiteral("value")).toString();
                if (name.isEmpty() || value.isEmpty()) continue;
                QVariantMap option; option.insert(QStringLiteral("text"), name); option.insert(QStringLiteral("value"), value); option.insert(QStringLiteral("when"), game.key()); items.append(option);
            }
        }
        box->setProperty("scConditionalItems", items);
    }
    if (!invalidCatalogs.isEmpty())
        QMessageBox::warning(this, tr("Character catalog"), tr("Could not read the character catalog. Place character-catalog.json beside the layout and reload it.\n%1").arg(QStringList(invalidCatalogs.values()).join(QStringLiteral("\n"))));

    // Wire conditional choices only after every layout widget exists.
    QList<std::function<void()>> conditionalRefreshes;
    for (auto it = widgetType.cbegin(); it != widgetType.cend(); ++it) {
        if (it.value() != "comboBox") continue;
        auto *dependent = qobject_cast<ScComboBox *>(widgetList.value(it.key()));
        if (!dependent) continue;
        if (dependent->property("scCatalogType").toString() == QStringLiteral("skins")) continue;
        const QString sourceId = dependent->property("scDependsOn").toString();
        if (sourceId.isEmpty() || widgetType.value(sourceId) != "comboBox") continue;
        // Ignore cycles rather than recursively emitting selection changes.
        QSet<QString> seen; QString walk = it.key(); bool cycle = false;
        while (!walk.isEmpty()) {
            if (seen.contains(walk)) { cycle = true; break; }
            seen.insert(walk);
            auto *node = qobject_cast<ScComboBox *>(widgetList.value(walk));
            walk = node ? node->property("scDependsOn").toString() : QString();
        }
        if (cycle) continue;
        auto *source = qobject_cast<ScComboBox *>(widgetList.value(sourceId));
        if (!source) continue;
        const auto update = [source, dependent] {
            const QString selected = dependent->currentData().toString();
            const QString parentValue = source->currentData().isValid() ? source->currentData().toString() : source->currentText();
            const QVariantList entries = dependent->property("scConditionalItems").toList();
            dependent->clear();
            for (const QVariant &entry : entries) {
                const QVariantMap item = entry.toMap(); const QString when = item.value("when").toString();
                if (when.isEmpty() || when == parentValue) dependent->addItem(item.value("text").toString(), item.value("value"));
            }
            const int restored = dependent->findData(selected);
            if (restored >= 0) dependent->setCurrentIndex(restored);
        };
        connect(source, qOverload<int>(&QComboBox::currentIndexChanged), dependent, [update](int) { update(); });
        conditionalRefreshes.append(update);
    }
    for (const auto &update : conditionalRefreshes) update();

    // Skin choices depend on both the selected character and its game.
    for (auto it = widgetType.cbegin(); it != widgetType.cend(); ++it) {
        if (it.value() != QStringLiteral("comboBox")) continue;
        auto *skinBox = qobject_cast<ScComboBox *>(widgetList.value(it.key(), nullptr));
        if (!skinBox || skinBox->property("scCatalogType").toString() != QStringLiteral("skins")) continue;
        const QString characterId = skinBox->property("scDependsOn").toString();
        auto *characterBox = qobject_cast<ScComboBox *>(widgetList.value(characterId, nullptr));
        if (!characterBox) continue;
        const QString gameId = characterBox->property("scDependsOn").toString();
        auto *gameBox = qobject_cast<ScComboBox *>(widgetList.value(gameId, nullptr));
        if (!gameBox) continue;
        const QString path = QFileInfo(settings.value(QStringLiteral("layoutPath"))).dir().filePath(
            skinBox->property("scCatalogFile").toString());
        const QJsonObject games = characterCatalogs.value(path);
        const QVariantList fallback = skinBox->property("scConditionalItems").toList();
        const auto updateSkins = [skinBox, characterBox, gameBox, games, fallback] {
            const QString game = gameBox->currentData().toString();
            const QString character = characterBox->currentData().toString();
            const QString previousGame = skinBox->property("scSkinGame").toString();
            const QString previousCharacter = skinBox->property("scSkinCharacter").toString();
            const QString selected = skinBox->currentData().toString();
            skinBox->setProperty("scSkinGame", game);
            skinBox->setProperty("scSkinCharacter", character);
            skinBox->clear();
            skinBox->addItem(QObject::tr("Select skin"), QString());
            if (!game.isEmpty() && !character.isEmpty()) {
                QJsonArray skins;
                const QJsonArray characters = games.value(game).toArray();
                for (const QJsonValue &entry : characters) {
                    const QJsonObject item = entry.toObject();
                    if (item.value(QStringLiteral("value")).toString() == character) {
                        skins = item.value(QStringLiteral("skins")).toArray();
                        break;
                    }
                }
                for (const QJsonValue &entry : skins) {
                    const QJsonObject skin = entry.toObject();
                    const QString name = skin.value(QStringLiteral("name")).toString();
                    const QString value = skin.value(QStringLiteral("value")).toString();
                    if (!name.isEmpty() && !value.isEmpty()) {
                        skinBox->addItem(name, value);
                        const QJsonArray images = skin.value(QStringLiteral("images")).toArray();
                        if (!images.isEmpty()) skinBox->setItemData(skinBox->count() - 1, images.first().toString(), Qt::UserRole + 1);
                    }
                }
                // Keep the layout's existing choices when no cataloged skins exist yet.
                if (skinBox->count() == 1) {
                    for (const QVariant &entry : fallback) {
                        const QVariantMap option = entry.toMap();
                        const QString value = option.value(QStringLiteral("value")).toString();
                        if (!value.isEmpty()) skinBox->addItem(option.value(QStringLiteral("text")).toString(), value);
                    }
                }
            }
            if (game == previousGame && character == previousCharacter) {
                const int index = skinBox->findData(selected);
                if (index >= 0) skinBox->setCurrentIndex(index);
            }
        };
        connect(characterBox, qOverload<int>(&QComboBox::currentIndexChanged), skinBox,
                [updateSkins](int) { updateSkins(); });
        connect(gameBox, qOverload<int>(&QComboBox::currentIndexChanged), skinBox,
                [updateSkins](int) { updateSkins(); });
        updateSkins();
    }

    // Start.gg stream queue controls are optional in custom layouts.
    auto *sggList = qobject_cast<QComboBox *>(widgetList.value(QStringLiteral("schedule-startgg-queue"), nullptr));
    auto *sggRefresh = qobject_cast<QPushButton *>(visualList.value(QStringLiteral("schedule-startgg-refresh"), nullptr));
    auto *sggAdd = qobject_cast<QPushButton *>(visualList.value(QStringLiteral("schedule-startgg-add"), nullptr));
    if (sggList && sggRefresh && sggAdd) {
        auto *network = new QNetworkAccessManager(sggList);
        connect(sggRefresh, &QPushButton::clicked, sggList, [this, sggList, sggRefresh, network] {
            PortableSettings config(QStringLiteral("StreamControlPlus"), QStringLiteral("BracketImport"));
            const QUrl eventUrl(config.value(QStringLiteral("startgg/url")).toString().trimmed());
            const QString token = config.value(QStringLiteral("startgg/token")).toString().trimmed();
            const QStringList parts = eventUrl.path().split('/', Qt::SkipEmptyParts);
            if (eventUrl.scheme() != QStringLiteral("https") || !eventUrl.userInfo().isEmpty() ||
                (eventUrl.host().toLower() != QStringLiteral("start.gg") && eventUrl.host().toLower() != QStringLiteral("www.start.gg")) ||
                parts.size() < 2 || parts.at(0) != QStringLiteral("tournament") || token.isEmpty() || token.contains('\r') || token.contains('\n')) {
                QMessageBox::warning(this, tr("Start.gg queue"), tr("Set a Start.gg tournament/event URL in the bracket importer and an API key in Widget Settings → Bracket.")); return;
            }
            QJsonObject variables; variables.insert(QStringLiteral("tourneySlug"), QStringLiteral("tournament/") + parts.at(1));
            QJsonObject body; body.insert(QStringLiteral("variables"), variables);
            body.insert(QStringLiteral("query"), QStringLiteral("query StreamQueue($tourneySlug: String!) { tournament(slug: $tourneySlug) { streamQueue { stream { streamName streamSource } sets { id fullRoundText event { videogame { id } } slots { entrant { id name participants { player { id } } } } } } } }"));
            QNetworkRequest req(QUrl(QStringLiteral("https://api.start.gg/gql/alpha")));
            req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
            req.setRawHeader("Authorization", QByteArray("Bearer ") + token.toUtf8()); req.setTransferTimeout(15000);
            req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
            sggRefresh->setEnabled(false);
            auto *reply = network->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
            connect(reply, &QNetworkReply::finished, sggList, [this, reply, sggList, sggRefresh] {
                const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                QJsonParseError parse; const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &parse);
                const bool failed = reply->error() != QNetworkReply::NoError; reply->deleteLater(); sggRefresh->setEnabled(true);
                const QJsonObject root = doc.object(); const QJsonArray errors = root.value(QStringLiteral("errors")).toArray();
                const QJsonValue tournament = root.value(QStringLiteral("data")).toObject().value(QStringLiteral("tournament"));
                if (failed || http != 200 || parse.error != QJsonParseError::NoError || !errors.isEmpty() || !tournament.isObject() || !tournament.toObject().value(QStringLiteral("streamQueue")).isArray()) {
                    const QString detail = errors.isEmpty() ? tr("HTTP %1 or invalid response").arg(http) : errors.first().toObject().value(QStringLiteral("message")).toString();
                    QMessageBox::warning(this, tr("Start.gg queue"), tr("Could not load stream queue: %1").arg(detail)); return;
                }
                sggList->clear(); sggList->addItem(tr("Select a Start.gg match"), QVariant()); int count = 0;
                for (const auto &streamEntry : tournament.toObject().value(QStringLiteral("streamQueue")).toArray()) {
                    const QJsonObject entry = streamEntry.toObject(); const QString stream = entry.value(QStringLiteral("stream")).toObject().value(QStringLiteral("streamName")).toString();
                    for (const auto &setValue : entry.value(QStringLiteral("sets")).toArray()) {
                        const QJsonObject set = setValue.toObject(); const QJsonArray setSlots = set.value(QStringLiteral("slots")).toArray();
                        const QString p1 = setSlots.size() > 0 ? setSlots.at(0).toObject().value(QStringLiteral("entrant")).toObject().value(QStringLiteral("name")).toString() : QString();
                        const QString p2 = setSlots.size() > 1 ? setSlots.at(1).toObject().value(QStringLiteral("entrant")).toObject().value(QStringLiteral("name")).toString() : QString();
                        if (p1.isEmpty() || p2.isEmpty()) continue;
                        QVariantMap item; item.insert(QStringLiteral("p1"), p1); item.insert(QStringLiteral("p2"), p2); item.insert(QStringLiteral("round"), set.value(QStringLiteral("fullRoundText")).toString());
                        for (int player = 0; player < 2; ++player) {
                            const QJsonObject entrant = setSlots.at(player).toObject().value(QStringLiteral("entrant")).toObject();
                            const QJsonArray participants = entrant.value(QStringLiteral("participants")).toArray();
                            if (participants.size() == 1) {
                                const QJsonValue id = participants.first().toObject().value(QStringLiteral("player")).toObject().value(QStringLiteral("id"));
                                item.insert(QStringLiteral("playerId%1").arg(player + 1), id.toVariant().toString());
                            }
                        }
                        const QJsonValue gameId = set.value(QStringLiteral("event")).toObject().value(QStringLiteral("videogame")).toObject().value(QStringLiteral("id"));
                        item.insert(QStringLiteral("gameId"), gameId.toVariant().toString());
                        sggList->addItem(QStringLiteral("%1: %2 vs %3 — %4").arg(stream.isEmpty() ? tr("Stream") : stream, p1, p2, item.value(QStringLiteral("round")).toString()), item); ++count;
                    }
                }
                statusBar()->showMessage(tr("Loaded %1 Start.gg queue matches.").arg(count), 10000);
            });
        });
        connect(sggAdd, &QPushButton::clicked, sggList, [this, sggList] {
            const QVariantMap item = sggList->currentData().toMap();
            if (item.isEmpty()) { QMessageBox::information(this, tr("Start.gg queue"), tr("Select a match first.")); return; }
            for (int row = 1; row <= 10; ++row) {
                auto *p1 = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-queue-%1-p1").arg(row), nullptr));
                auto *p2 = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-queue-%1-p2").arg(row), nullptr));
                auto *round = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-queue-%1-round").arg(row), nullptr));
                if (!p1 || !p2 || !round) { QMessageBox::warning(this, tr("Start.gg queue"), tr("The layout is missing queue fields.")); return; }
                if (p1->text().trimmed().isEmpty() && p2->text().trimmed().isEmpty() && round->text().trimmed().isEmpty()) {
                    p1->setText(item.value(QStringLiteral("p1")).toString()); p2->setText(item.value(QStringLiteral("p2")).toString()); round->setText(item.value(QStringLiteral("round")).toString());
                    statusBar()->showMessage(tr("Match added to queue. Save the dashboard to publish it."), 10000); return;
                }
            }
            QMessageBox::warning(this, tr("Start.gg queue"), tr("All ten queue rows contain information. Clear a row before adding a match."));
        });
    }


    // Mirror the bracket event URL on the Schedule and Head-to-Head pages.
    auto *scheduleUrl = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-event-url"), nullptr));
    auto *h2hUrl = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("h2h-event-url"), nullptr));
    auto *bracketPanel = findChild<QWidget *>(QStringLiteral("bracket-import-panel"));
    auto *bracketUrl = bracketPanel ? bracketPanel->findChild<QLineEdit *>(QStringLiteral("bracket-event-url")) : nullptr;
    auto *bracketProvider = bracketPanel ? bracketPanel->findChild<QComboBox *>(QStringLiteral("bracket-provider-selector")) : nullptr;
    const auto mirrorUrl = [scheduleUrl, h2hUrl](const QString &value) {
        for (QLineEdit *edit : {scheduleUrl, h2hUrl}) {
            if (edit && edit->text() != value) { QSignalBlocker blocked(edit); edit->setText(value); }
        }
    };
    if (bracketUrl) {
        mirrorUrl(bracketUrl->text());
        connect(bracketUrl, &QLineEdit::textChanged, this, mirrorUrl);
        if (bracketProvider) connect(bracketProvider, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [bracketUrl, mirrorUrl](int) { mirrorUrl(bracketUrl->text()); });
    } else {
        PortableSettings config(QStringLiteral("StreamControlPlus"), QStringLiteral("BracketImport"));
        mirrorUrl(config.value(QStringLiteral("startgg/url")).toString());
    }
    const auto commitUrl = [bracketUrl, bracketProvider, mirrorUrl](QLineEdit *source) {
        if (!source) return;
        const QString value = source->text().trimmed();
        if (bracketUrl) {
            const QString host = QUrl(value).host().toLower();
            if (bracketProvider) {
                QString provider;
                if (host == QStringLiteral("start.gg") || host == QStringLiteral("www.start.gg")) provider = QStringLiteral("startgg");
                else if (host == QStringLiteral("parry.gg")) provider = QStringLiteral("parry");
                if (!provider.isEmpty()) { const int index = bracketProvider->findData(provider); if (index >= 0) bracketProvider->setCurrentIndex(index); }
            }
            bracketUrl->setText(value);
        } else {
            PortableSettings config(QStringLiteral("StreamControlPlus"), QStringLiteral("BracketImport"));
            config.setValue(QStringLiteral("startgg/url"), value);
        }
        mirrorUrl(value);
    };
    for (QLineEdit *edit : {scheduleUrl, h2hUrl}) if (edit)
        connect(edit, &QLineEdit::editingFinished, this, [edit, commitUrl] { commitUrl(edit); });

    // Load participants for the selected start.gg event and look up their H2H.
    auto *h2hPlayer1 = qobject_cast<QComboBox *>(widgetList.value(QStringLiteral("h2h-player-1"), nullptr));
    auto *h2hPlayer2 = qobject_cast<QComboBox *>(widgetList.value(QStringLiteral("h2h-player-2"), nullptr));
    auto *h2hGame = qobject_cast<QComboBox *>(widgetList.value(QStringLiteral("h2h-game"), nullptr));
    auto *h2hLoad = qobject_cast<QPushButton *>(visualList.value(QStringLiteral("h2h-load-entrants"), nullptr));
    auto *h2hFilter = qobject_cast<QComboBox *>(widgetList.value(QStringLiteral("h2h-filter"), nullptr));
    auto *h2hRefresh = qobject_cast<QPushButton *>(visualList.value(QStringLiteral("h2h-refresh"), nullptr));
    auto *h2hStatus = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("h2h-status"), nullptr));
    auto *h2hP1Sets = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("h2h-p1-sets"), nullptr));
    auto *h2hP2Sets = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("h2h-p2-sets"), nullptr));
    auto *h2hP1Games = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("h2h-p1-games"), nullptr));
    auto *h2hP2Games = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("h2h-p2-games"), nullptr));
    if (h2hUrl && h2hPlayer1 && h2hPlayer2 && h2hGame && h2hLoad && h2hFilter && h2hRefresh && h2hStatus && h2hP1Sets && h2hP2Sets && h2hP1Games && h2hP2Games) {
        auto *network = new QNetworkAccessManager(h2hRefresh);
        QList<QLineEdit *> recentFields;
        for (int i = 1; i <= 5; ++i) if (auto *field = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("h2h-recent-%1").arg(i), nullptr))) recentFields.append(field);
        QList<QList<QLineEdit *>> recentParts;
        const QStringList partNames = {QStringLiteral("player-1"), QStringLiteral("player-2"), QStringLiteral("score"), QStringLiteral("event"), QStringLiteral("round")};
        for (int i = 1; i <= 5; ++i) {
            QList<QLineEdit *> fields;
            for (const QString &part : partNames) {
                const QString id = QStringLiteral("h2h-recent-%1-%2").arg(i).arg(part);
                auto *field = qobject_cast<ScLineEdit *>(widgetList.value(id, nullptr));
                if (!field) {
                    field = new ScLineEdit(h2hRefresh);
                    field->setObjectName(id);
                    field->hide();
                    widgetList.insert(id, field);
                    widgetType.insert(id, QStringLiteral("lineEdit"));
                }
                fields.append(field);
            }
            recentParts.append(fields);
        }
        for (QComboBox *box : {h2hPlayer1, h2hPlayer2}) {
            if (!box->isEditable()) box->setEditable(true);
            if (box->completer()) { box->completer()->setCaseSensitivity(Qt::CaseInsensitive); box->completer()->setFilterMode(Qt::MatchContains); box->completer()->setCompletionMode(QCompleter::PopupCompletion); }
            box->lineEdit()->setPlaceholderText(QObject::tr("Select an entrant or search Supermajor by name"));
        }
        const auto playerId = [](QComboBox *box) -> QString {
            const QString text = box->currentText().trimmed();
            const int index = box->currentIndex();
            if (index >= 0 && box->itemText(index) == text) {
                const QString id = box->itemData(index).toString();
                if (QRegularExpression(QStringLiteral("^S[0-9]+$")).match(id).hasMatch()) return id;
            }
            QString matchedId;
            for (int i = 0; i < box->count(); ++i) {
                const QString candidateId = box->itemData(i).toString();
                if (!QRegularExpression(QStringLiteral("^S[0-9]+$")).match(candidateId).hasMatch()) continue;
                const QString suffix = QStringLiteral(" (%1)").arg(candidateId);
                const QString label = box->itemText(i);
                QString tag = box->itemData(i, Qt::UserRole + 2).toString();
                if (tag.isEmpty()) tag = label.endsWith(suffix) ? label.left(label.size() - suffix.size()) : label;
                if (tag.compare(text, Qt::CaseInsensitive) == 0) {
                    if (!matchedId.isEmpty() && matchedId != candidateId) return QString();
                    matchedId = candidateId;
                }
            }
            if (!matchedId.isEmpty()) return matchedId;
            QString id = text;
            const QUrl url(text);
            if (url.isValid() && (url.host().compare(QStringLiteral("supermajor.gg"), Qt::CaseInsensitive) == 0 || url.host().compare(QStringLiteral("www.supermajor.gg"), Qt::CaseInsensitive) == 0)) id = QUrlQuery(url).queryItemValue(QStringLiteral("id"));
            if (QRegularExpression(QStringLiteral("^[0-9]+$")).match(id).hasMatch()) id.prepend(QLatin1Char('S'));
            return QRegularExpression(QStringLiteral("^S[0-9]+$")).match(id).hasMatch() ? id : QString();
        };
        const auto clearResults = [h2hP1Sets, h2hP2Sets, h2hP1Games, h2hP2Games, recentFields, recentParts] {
            for (QLineEdit *field : {h2hP1Sets, h2hP2Sets, h2hP1Games, h2hP2Games}) field->clear();
            for (QLineEdit *field : recentFields) field->clear();
            for (const auto &row : recentParts) for (QLineEdit *field : row) field->clear();
        };
        const auto lookup = [h2hPlayer1, h2hPlayer2, h2hGame, h2hFilter, h2hRefresh, h2hStatus, h2hP1Sets, h2hP2Sets, h2hP1Games, h2hP2Games, recentFields, recentParts, network, playerId, clearResults] {
            const int serial = h2hRefresh->property("h2hSerial").toInt() + 1;
            h2hRefresh->setProperty("h2hSerial", serial); clearResults();
            const QString p1 = playerId(h2hPlayer1), p2 = playerId(h2hPlayer2), sport = h2hGame->currentData().toString();
            if (p1.isEmpty() || p2.isEmpty()) { h2hStatus->setText(QObject::tr("Select two players from the suggestions (or enter Supermajor IDs)")); return; }
            if (p1 == p2) { h2hStatus->setText(QObject::tr("Select two different players")); return; }
            if (sport.isEmpty()) { h2hStatus->setText(QObject::tr("Select Melee or Ultimate")); return; }
            QJsonObject body; body.insert(QStringLiteral("sport"), sport); body.insert(QStringLiteral("p1PlayerId"), p1); body.insert(QStringLiteral("p2PlayerId"), p2);
            body.insert(QStringLiteral("tab"), QStringLiteral("overview")); body.insert(QStringLiteral("globalFilterContext"), h2hFilter->currentData().toString()); body.insert(QStringLiteral("globalDateRange"), QStringLiteral("All Time"));
            QJsonObject matches; matches.insert(QStringLiteral("filterToP1Wins"), false); matches.insert(QStringLiteral("filterToP2Wins"), false); matches.insert(QStringLiteral("sortBy"), QStringLiteral("date_desc"));
            QJsonObject placements; placements.insert(QStringLiteral("sortBy"), QStringLiteral("date_desc"));
            body.insert(QStringLiteral("matchesTabSettings"), matches); body.insert(QStringLiteral("placementsTabSettings"), placements); body.insert(QStringLiteral("filterToVods"), false); body.insert(QStringLiteral("limitToOldRankings"), false);
            QNetworkRequest request(QUrl(QStringLiteral("https://slpurukxtnleofuopadw.supabase.co/functions/v1/h2h-sheet-v2")));
            request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
            const QByteArray publicKey("sb_publishable_ENByDjqQbyXQHXqtfx5EHQ_VZMrFtEu");
            request.setRawHeader("apikey", publicKey); request.setRawHeader("Authorization", QByteArray("Bearer ") + publicKey);
            request.setRawHeader("Origin", "https://www.supermajor.gg"); request.setRawHeader("Referer", "https://www.supermajor.gg/"); request.setTransferTimeout(15000); request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
            h2hStatus->setText(QObject::tr("Loading head-to-head..."));
            auto *reply = network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
            QObject::connect(reply, &QNetworkReply::finished, h2hRefresh, [reply, serial, h2hRefresh, h2hStatus, h2hP1Sets, h2hP2Sets, h2hP1Games, h2hP2Games, recentFields, recentParts, p1, p2] {
                const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); const bool failed = reply->error() != QNetworkReply::NoError; const QByteArray bytes = reply->readAll(); reply->deleteLater();
                if (h2hRefresh->property("h2hSerial").toInt() != serial) return;
                QJsonParseError error; const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
                if (failed || http != 200 || error.error != QJsonParseError::NoError || !doc.isObject()) { h2hStatus->setText(QObject::tr("Supermajor request failed (HTTP %1)").arg(http)); return; }
                const QJsonObject data = doc.object().value(QStringLiteral("data")).toObject(), one = data.value(QStringLiteral("p1_info")).toObject(), two = data.value(QStringLiteral("p2_info")).toObject();
                const QJsonObject sets = data.value(QStringLiteral("matches_winrate")).toObject(), games = data.value(QStringLiteral("games_winrate")).toObject();
                if (one.value(QStringLiteral("player_id")).toString() != p1 || two.value(QStringLiteral("player_id")).toString() != p2 || !sets.value(QStringLiteral("wins")).isDouble() || !sets.value(QStringLiteral("losses")).isDouble() || !games.value(QStringLiteral("wins")).isDouble() || !games.value(QStringLiteral("losses")).isDouble()) { h2hStatus->setText(QObject::tr("No verified Supermajor record for these IDs")); return; }
                h2hP1Sets->setText(QString::number(sets.value(QStringLiteral("wins")).toInt())); h2hP2Sets->setText(QString::number(sets.value(QStringLiteral("losses")).toInt())); h2hP1Games->setText(QString::number(games.value(QStringLiteral("wins")).toInt())); h2hP2Games->setText(QString::number(games.value(QStringLiteral("losses")).toInt()));
                const QJsonArray recent = data.value(QStringLiteral("last_5_matches")).toArray();
                for (int i = 0; i < recent.size() && i < recentParts.size(); ++i) {
                    const QJsonObject set = recent.at(i).toObject(), left = set.value(QStringLiteral("p1_info")).toObject(), right = set.value(QStringLiteral("p2_info")).toObject(), event = set.value(QStringLiteral("event_info")).toObject();
                    const QString leftScore = left.value(QStringLiteral("score")).isDouble() ? QString::number(left.value(QStringLiteral("score")).toInt()) : QStringLiteral("?");
                    const QString rightScore = right.value(QStringLiteral("score")).isDouble() ? QString::number(right.value(QStringLiteral("score")).toInt()) : QStringLiteral("?");
                    const QString leftName = left.value(QStringLiteral("tag")).toString();
                    const QString rightName = right.value(QStringLiteral("tag")).toString();
                    const QString eventName = event.value(QStringLiteral("tournament_name")).toString();
                    const QString round = set.value(QStringLiteral("location_name")).toString();
                    const QString score = QStringLiteral("%1 - %2").arg(leftScore, rightScore);
                    const QList<QLineEdit *> row = recentParts.at(i);
                    row.at(0)->setText(leftName); row.at(1)->setText(rightName);
                    row.at(2)->setText(score); row.at(3)->setText(eventName); row.at(4)->setText(round);
                    if (i < recentFields.size()) recentFields.at(i)->setText(QStringLiteral("%1 %2 - %3 %4 | %5 | %6").arg(leftName, leftScore, rightScore, rightName, eventName, round));
                }
                h2hStatus->setText(QObject::tr("Loaded %1 vs %2; Save to publish").arg(one.value(QStringLiteral("tag")).toString(), two.value(QStringLiteral("tag")).toString()));
            });
        };
        const auto loadEntrants = [h2hUrl, h2hPlayer1, h2hPlayer2, h2hGame, h2hLoad, h2hRefresh, h2hStatus, network, commitUrl, clearResults] {
            commitUrl(h2hUrl);
            const int generation = h2hLoad->property("rosterGeneration").toInt() + 1; h2hLoad->setProperty("rosterGeneration", generation);
            h2hRefresh->setProperty("h2hSerial", h2hRefresh->property("h2hSerial").toInt() + 1); clearResults();
            const QUrl url(h2hUrl->text().trimmed()); const QStringList parts = url.path().split('/', Qt::SkipEmptyParts);
            if (url.scheme() != QStringLiteral("https") || (url.host().toLower() != QStringLiteral("start.gg") && url.host().toLower() != QStringLiteral("www.start.gg")) || parts.size() < 4 || parts.at(0) != QStringLiteral("tournament") || parts.at(2) != QStringLiteral("event")) { h2hStatus->setText(QObject::tr("Enter a start.gg tournament/event URL")); return; }
            const QString slug = QStringLiteral("tournament/") + parts.at(1) + QStringLiteral("/event/") + parts.at(3);
            PortableSettings config(QStringLiteral("StreamControlPlus"), QStringLiteral("BracketImport")); const QByteArray token = config.value(QStringLiteral("startgg/token")).toString().trimmed().toUtf8();
            if (token.isEmpty() || token.contains('\r') || token.contains('\n')) { h2hStatus->setText(QObject::tr("Set your start.gg API key in Widget Settings")); return; }
            h2hLoad->setEnabled(false); h2hStatus->setText(QObject::tr("Loading event entrants..."));
            struct RosterState {
                QMap<QString, QString> roster;
                QString eventId;
                QString gameId;
                int nextPage = 1;
                int totalPages = -1;
                int inFlight = 0;
                int completed = 0;
                bool failed = false;
            };
            const auto state = std::make_shared<RosterState>();
            auto pump = std::make_shared<std::function<void()>>();
            const std::weak_ptr<std::function<void()>> weakPump = pump;
            *pump = [network, h2hLoad, h2hStatus, h2hPlayer1, h2hPlayer2, h2hGame, slug, token, state, generation, weakPump] {
                auto next = weakPump.lock();
                if (!next || state->failed || h2hLoad->property("rosterGeneration").toInt() != generation) return;
                if (state->totalPages >= 0 && state->nextPage > state->totalPages) return;
                if (state->inFlight >= 3) {
                    QTimer::singleShot(1000, h2hLoad, [next] { (*next)(); });
                    return;
                }
                const int page = state->nextPage++;
                ++state->inFlight;
                QJsonObject variables; variables.insert(QStringLiteral("slug"), slug); variables.insert(QStringLiteral("page"), page);
                QJsonObject body; body.insert(QStringLiteral("query"), QStringLiteral("query EventPlayers($slug:String!,$page:Int!){event(slug:$slug){id videogame{id} entrants(query:{page:$page,perPage:50}){pageInfo{totalPages} nodes{participants{gamerTag player{id}}}}}}")); body.insert(QStringLiteral("variables"), variables);
                QNetworkRequest request(QUrl(QStringLiteral("https://api.start.gg/gql/alpha"))); request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json")); request.setRawHeader("Authorization", QByteArray("Bearer ") + token); request.setTransferTimeout(15000); request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
                auto *reply = network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
                if (state->totalPages >= 0 && state->nextPage <= state->totalPages)
                    QTimer::singleShot(1000, h2hLoad, [next] { (*next)(); });
                QObject::connect(reply, &QNetworkReply::finished, h2hLoad, [reply, h2hLoad, h2hStatus, h2hPlayer1, h2hPlayer2, h2hGame, state, generation, page, next] {
                    const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); const bool failed = reply->error() != QNetworkReply::NoError; const QByteArray bytes = reply->readAll(); reply->deleteLater();
                    if (state->failed || h2hLoad->property("rosterGeneration").toInt() != generation) return;
                    --state->inFlight;
                    QJsonParseError error; const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error); const QJsonObject root = doc.object(); const QJsonArray errors = root.value(QStringLiteral("errors")).toArray();
                    const QJsonObject event = root.value(QStringLiteral("data")).toObject().value(QStringLiteral("event")).toObject(), entrants = event.value(QStringLiteral("entrants")).toObject();
                    if (failed || http != 200 || error.error != QJsonParseError::NoError || !errors.isEmpty() || entrants.isEmpty()) {
                        state->failed = true; h2hLoad->setEnabled(true);
                        h2hStatus->setText(errors.isEmpty() ? QObject::tr("Could not load entrants (HTTP %1); retry").arg(http) : errors.first().toObject().value(QStringLiteral("message")).toString()); return;
                    }
                    const QJsonValue pagesValue = entrants.value(QStringLiteral("pageInfo")).toObject().value(QStringLiteral("totalPages"));
                    const int totalPages = pagesValue.toInt(-1);
                    const QString eventId = event.value(QStringLiteral("id")).toVariant().toString();
                    if (!pagesValue.isDouble() || totalPages < 0 || totalPages > 1000 || eventId.isEmpty() || !entrants.value(QStringLiteral("nodes")).isArray() || (totalPages == 0 && !entrants.value(QStringLiteral("nodes")).toArray().isEmpty()) || (page != 1 && (totalPages != state->totalPages || eventId != state->eventId))) {
                        state->failed = true; h2hLoad->setEnabled(true); h2hStatus->setText(QObject::tr("Event entrants changed or pagination was invalid; retry")); return;
                    }
                    if (page == 1) {
                        state->totalPages = totalPages; state->eventId = eventId;
                        state->gameId = event.value(QStringLiteral("videogame")).toObject().value(QStringLiteral("id")).toVariant().toString();
                    }
                    for (const QJsonValue &entry : entrants.value(QStringLiteral("nodes")).toArray()) {
                        const QJsonArray participants = entry.toObject().value(QStringLiteral("participants")).toArray();
                        if (participants.size() != 1) continue;
                        const QJsonObject participant = participants.first().toObject(), player = participant.value(QStringLiteral("player")).toObject();
                        const QString id = player.value(QStringLiteral("id")).toVariant().toString(), tag = participant.value(QStringLiteral("gamerTag")).toString().trimmed();
                        if (!id.isEmpty() && !tag.isEmpty()) state->roster.insert(QStringLiteral("S") + id, tag);
                    }
                    ++state->completed;
                    if (page == 1 && totalPages > 1) QTimer::singleShot(1000, h2hLoad, [next] { (*next)(); });
                    if (state->completed < totalPages) { h2hStatus->setText(QObject::tr("Loading entrants: %1 of %2 pages").arg(state->completed).arg(totalPages)); return; }
                    QList<QPair<QString, QString>> sorted; for (auto i = state->roster.cbegin(); i != state->roster.cend(); ++i) sorted.append(qMakePair(i.value(), i.key()));
                    std::sort(sorted.begin(), sorted.end(), [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) { return QString::localeAwareCompare(a.first, b.first) < 0; });
                    for (QComboBox *box : {h2hPlayer1, h2hPlayer2}) {
                        QSignalBlocker guard(box); box->clear(); box->addItem(QString(), QString());
                        for (const auto &player : sorted) box->addItem(QStringLiteral("%1 (%2)").arg(player.first, player.second), player.second);
                        box->setCurrentIndex(0); box->setEditText(QString());
                    }
                    const QString sport = state->gameId == QStringLiteral("1") ? QStringLiteral("melee") : state->gameId == QStringLiteral("1386") ? QStringLiteral("ultimate") : QString();
                    const int index = h2hGame->findData(sport); if (index >= 0) h2hGame->setCurrentIndex(index);
                    h2hLoad->setEnabled(true); h2hStatus->setText(QObject::tr("Loaded %1 singles entrants. Select two players.").arg(sorted.size()));
                });
            };
            (*pump)();
        };
        const auto invalidate = [h2hLoad, h2hRefresh, h2hPlayer1, h2hPlayer2, h2hStatus, clearResults] {
            h2hLoad->setProperty("rosterGeneration", h2hLoad->property("rosterGeneration").toInt() + 1); h2hRefresh->setProperty("h2hSerial", h2hRefresh->property("h2hSerial").toInt() + 1); h2hLoad->setEnabled(true);
            for (QComboBox *box : {h2hPlayer1, h2hPlayer2}) { QSignalBlocker guard(box); box->clear(); box->addItem(QString(), QString()); box->setEditText(QString()); }
            clearResults(); h2hStatus->setText(QObject::tr("Event changed; load entrants"));
        };
        if (bracketUrl) {
            connect(bracketUrl, &QLineEdit::textChanged, h2hLoad, [invalidate](const QString &) { invalidate(); });
            if (bracketProvider) connect(bracketProvider, qOverload<int>(&QComboBox::currentIndexChanged), h2hLoad, [invalidate](int) { invalidate(); });
        } else for (QLineEdit *edit : {scheduleUrl, h2hUrl}) if (edit) connect(edit, &QLineEdit::editingFinished, h2hLoad, invalidate);
        connect(h2hLoad, &QPushButton::clicked, h2hLoad, [loadEntrants] { loadEntrants(); });
        const auto autoLoad = [h2hUrl, h2hLoad, loadEntrants] {
            const QUrl url(h2hUrl->text().trimmed()); const QStringList parts = url.path().split('/', Qt::SkipEmptyParts);
            PortableSettings config(QStringLiteral("StreamControlPlus"), QStringLiteral("BracketImport"));
            if (h2hLoad->isEnabled() && (url.host().toLower() == QStringLiteral("start.gg") || url.host().toLower() == QStringLiteral("www.start.gg")) && parts.size() >= 4 && parts.at(0) == QStringLiteral("tournament") && parts.at(2) == QStringLiteral("event") && !config.value(QStringLiteral("startgg/token")).toString().trimmed().isEmpty()) loadEntrants();
        };
        for (QLineEdit *edit : {scheduleUrl, h2hUrl, bracketUrl}) if (edit) connect(edit, &QLineEdit::editingFinished, h2hLoad, [autoLoad] { autoLoad(); });
        QTimer::singleShot(0, h2hLoad, [autoLoad] { autoLoad(); });
        for (QComboBox *box : {h2hPlayer1, h2hPlayer2}) {
            auto *searchTimer = new QTimer(box);
            searchTimer->setSingleShot(true);
            searchTimer->setInterval(500);
            connect(box->lineEdit(), &QLineEdit::textEdited, searchTimer, [box, searchTimer](const QString &) {
                box->setProperty("h2hSearchGeneration", box->property("h2hSearchGeneration").toInt() + 1);
                searchTimer->start();
            });
            connect(box, qOverload<int>(&QComboBox::activated), searchTimer, [box, searchTimer](int) {
                box->setProperty("h2hSearchGeneration", box->property("h2hSearchGeneration").toInt() + 1);
                searchTimer->stop();
            });
            connect(h2hGame, qOverload<int>(&QComboBox::currentIndexChanged), searchTimer, [box, searchTimer](int) {
                box->setProperty("h2hSearchGeneration", box->property("h2hSearchGeneration").toInt() + 1);
                searchTimer->stop();
                const QString current = box->currentText();
                const QString selectedId = box->currentData().toString();
                QSignalBlocker guard(box);
                for (int i = box->count() - 1; i >= 0; --i)
                    if (box->itemData(i, Qt::UserRole + 1).toBool() && box->itemData(i).toString() != selectedId) box->removeItem(i);
                box->setEditText(current);
            });
            connect(searchTimer, &QTimer::timeout, box, [box, h2hGame, h2hStatus, network] {
                const QString term = box->currentText().trimmed();
                const QString sport = h2hGame->currentData().toString();
                const int generation = box->property("h2hSearchGeneration").toInt();
                if (term.size() < 2 || sport.isEmpty() || term.contains(QStringLiteral("://")) || QRegularExpression(QStringLiteral("^S?[0-9]+$"), QRegularExpression::CaseInsensitiveOption).match(term).hasMatch()) return;
                QJsonObject body; body.insert(QStringLiteral("sport"), sport); body.insert(QStringLiteral("searchTerm"), term);
                body.insert(QStringLiteral("searchMode"), QStringLiteral("all-players")); body.insert(QStringLiteral("limitToOldRankings"), false);
                QNetworkRequest request(QUrl(QStringLiteral("https://slpurukxtnleofuopadw.supabase.co/functions/v1/player-search-v2")));
                request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
                const QByteArray publicKey("sb_publishable_ENByDjqQbyXQHXqtfx5EHQ_VZMrFtEu");
                request.setRawHeader("apikey", publicKey); request.setRawHeader("Authorization", QByteArray("Bearer ") + publicKey);
                request.setRawHeader("Origin", "https://www.supermajor.gg"); request.setRawHeader("Referer", "https://www.supermajor.gg/");
                request.setTransferTimeout(15000); request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
                auto *reply = network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
                QObject::connect(reply, &QNetworkReply::finished, box, [reply, box, h2hGame, h2hStatus, term, sport, generation] {
                    const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    const bool failed = reply->error() != QNetworkReply::NoError;
                    const QByteArray bytes = reply->readAll(); reply->deleteLater();
                    if (box->property("h2hSearchGeneration").toInt() != generation || box->currentText().trimmed() != term || h2hGame->currentData().toString() != sport) return;
                    QJsonParseError error; const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
                    if (failed || http != 200 || error.error != QJsonParseError::NoError || !doc.isObject() || !doc.object().value(QStringLiteral("data")).isArray()) {
                        h2hStatus->setText(QObject::tr("Supermajor player search failed (HTTP %1)").arg(http)); return;
                    }
                    const QJsonArray results = doc.object().value(QStringLiteral("data")).toArray();
                    int added = 0;
                    {
                        QSignalBlocker guard(box);
                        for (int i = box->count() - 1; i >= 0; --i)
                            if (box->itemData(i, Qt::UserRole + 1).toBool()) box->removeItem(i);
                        for (const QJsonValue &value : results) {
                            const QJsonObject player = value.toObject();
                            const QString id = player.value(QStringLiteral("player_id")).toString();
                            const QString tag = player.value(QStringLiteral("tag")).toString().trimmed();
                            if (tag.isEmpty() || !QRegularExpression(QStringLiteral("^S[0-9]+$")).match(id).hasMatch() || box->findData(id) >= 0) continue;
                            const QString sponsor = player.value(QStringLiteral("sponsor")).toString().trimmed();
                            const QString country = player.value(QStringLiteral("country")).toString().trimmed();
                            const QString state = player.value(QStringLiteral("state")).toString().trimmed();
                            QString label = sponsor.isEmpty() ? tag : sponsor + QStringLiteral(" | ") + tag;
                            QStringList location; if (!country.isEmpty()) location.append(country); if (!state.isEmpty()) location.append(state);
                            if (!location.isEmpty()) label += QStringLiteral(" [%1]").arg(location.join(QStringLiteral(", ")));
                            label += QStringLiteral(" (%1)").arg(id);
                            box->addItem(label, id);
                            const int index = box->count() - 1;
                            box->setItemData(index, true, Qt::UserRole + 1);
                            box->setItemData(index, tag, Qt::UserRole + 2);
                            ++added;
                        }
                        box->setEditText(term);
                    }
                    h2hStatus->setText(QObject::tr("Supermajor search: %1 new matches; select a player").arg(added));
                    if (box->lineEdit()->hasFocus() && box->completer()) {
                        box->completer()->setCompletionPrefix(term);
                        box->completer()->complete();
                    }
                });
            });
            connect(box->lineEdit(), &QLineEdit::textEdited, h2hRefresh, [h2hRefresh, h2hStatus, clearResults](const QString &) {
                h2hRefresh->setProperty("h2hSerial", h2hRefresh->property("h2hSerial").toInt() + 1); clearResults(); h2hStatus->setText(QObject::tr("Select two players and refresh H2H"));
            });
            connect(box, qOverload<int>(&QComboBox::activated), h2hRefresh, [lookup](int) { lookup(); });
            connect(box->lineEdit(), &QLineEdit::editingFinished, h2hRefresh, [lookup] { lookup(); });
        }
        connect(h2hFilter, qOverload<int>(&QComboBox::currentIndexChanged), h2hRefresh, [lookup](int) { lookup(); });
        connect(h2hGame, qOverload<int>(&QComboBox::currentIndexChanged), h2hRefresh, [lookup](int) { lookup(); });
        connect(h2hRefresh, &QPushButton::clicked, h2hRefresh, [lookup] { lookup(); });
        h2hStatus->setText(QObject::tr("Load event entrants or search players by name"));
    }

    auto *sggDestination = qobject_cast<QComboBox *>(widgetList.value(QStringLiteral("schedule-startgg-destination"), nullptr));
    auto *sggSend = qobject_cast<QPushButton *>(visualList.value(QStringLiteral("schedule-startgg-send"), nullptr));
    if (sggList && sggDestination && sggSend) {
        connect(sggSend, &QPushButton::clicked, sggList, [this, sggList, sggDestination, sggSend] {
            sggSend->setProperty("queueSendSucceeded", false);
            const QVariantMap match = sggList->currentData().toMap();
            if (match.isEmpty()) { QMessageBox::information(this, tr("Start.gg"), tr("Select a Start.gg match first.")); return; }
            const QString target = sggDestination->currentData().toString();
            QString first, second, round;
            if (target == QStringLiteral("main")) { first = "scoreboard-player-1"; second = "scoreboard-player-2"; round = "scoreboard-round"; }
            else if (target.size() == 1 && QStringLiteral("abcd").contains(target)) { first = "multi-player-1" + target; second = "multi-player-2" + target; round = "multi-round"; }
            else { QMessageBox::warning(this, tr("Start.gg"), tr("Select a scoreboard destination.")); return; }
            struct PlayerChoice { QString name; QString prefix; QString dataset; int index = -1; };
            auto choose = [this](const QString &id, const QString &source, PlayerChoice *out) -> bool {
                auto *field = qobject_cast<ScLineEdit *>(widgetList.value(id, nullptr));
                if (!field) { QMessageBox::warning(this, tr("Start.gg"), tr("The destination player field is missing: %1").arg(id)); return false; }
                const QString dataset = field->getDataSetName(); const QList<QStringList> columns = dataSets.value(dataset);
                const int column = dataAssoc.value(id, -1); const QStringList names = column >= 0 && column < columns.size() ? columns.at(column) : QStringList();
                QList<int> exact; for (int i = 0; i < names.size(); ++i) if (names.at(i).trimmed().compare(source.trimmed(), Qt::CaseInsensitive) == 0) exact.append(i);
                QString tag = source.section('|', -1).trimmed();
                if (exact.isEmpty() && tag != source.trimmed()) for (int i = 0; i < names.size(); ++i) if (names.at(i).trimmed().compare(tag, Qt::CaseInsensitive) == 0) exact.append(i);
                out->dataset = dataset;
                if (exact.size() == 1) { out->index = exact.first(); out->name = names.at(out->index); return true; }
                // Prefer close player-name matches; compare without sponsor tags or punctuation.
                auto normalize = [](const QString &input) {
                    QString text = input.section('|', -1).trimmed().toCaseFolded();
                    text.remove(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]")));
                    return text;
                };
                const QString wanted = normalize(source);
                auto similar = [&wanted, &normalize](const QString &candidate) {
                    const QString name = normalize(candidate);
                    if (wanted.isEmpty() || name.isEmpty()) return false;
                    if (wanted == name) return true;
                    if (qMin(wanted.size(), name.size()) >= 3 && (wanted.contains(name) || name.contains(wanted))) return true;
                    if (wanted.size() > 100 || name.size() > 100) return false;
                    QVector<int> prev(name.size() + 1), next(name.size() + 1);
                    for (int j = 0; j <= name.size(); ++j) prev[j] = j;
                    for (int i = 1; i <= wanted.size(); ++i) {
                        next[0] = i;
                        for (int j = 1; j <= name.size(); ++j) next[j] = qMin(qMin(next[j - 1] + 1, prev[j] + 1), prev[j - 1] + (wanted.at(i - 1) == name.at(j - 1) ? 0 : 1));
                        prev.swap(next);
                    }
                    const int maxLen = qMax(wanted.size(), name.size());
                    return maxLen >= 3 && prev[name.size()] <= qMax(1, maxLen / 3);
                };
                QStringList options; options << tr("Keep Start.gg name: %1").arg(source);
                QList<int> matchingRows;
                for (int i = 0; i < names.size(); ++i) if (similar(names.at(i))) {
                    matchingRows.append(i); options << QStringLiteral("%1 — %2").arg(i + 1).arg(names.at(i));
                }
                bool ok = false;
                const QString chosen = QInputDialog::getItem(this, tr("Choose player"), tr("Choose a database entry for %1, or keep the Start.gg name:").arg(source), options, 0, false, &ok);
                if (!ok) return false;
                const int choice = options.indexOf(chosen);
                if (choice <= 0) {
                    out->index = -1;
                    const int separator = source.lastIndexOf('|');
                    if (separator >= 0 && !source.mid(separator + 1).trimmed().isEmpty()) {
                        out->name = source.mid(separator + 1).trimmed();
                        out->prefix = source.left(separator).trimmed();
                    } else out->name = source.trimmed();
                } else { out->index = matchingRows.at(choice - 1); out->name = names.at(out->index); }
                return true;
            };
            PlayerChoice a, b;
            if (!choose(first, match.value("p1").toString(), &a) || !choose(second, match.value("p2").toString(), &b)) return;
            auto apply = [this](const QString &id, const PlayerChoice &player) {
                auto *name = qobject_cast<ScLineEdit *>(widgetList.value(id)); name->setText(player.name);
                const QList<QStringList> columns = dataSets.value(player.dataset);
                for (const QString &slave : dataMaster.value(id)) {
                    const int column = dataAssoc.value(slave, -1);
                    QString value = player.index >= 0 && column >= 0 && column < columns.size() && player.index < columns.at(column).size() ? columns.at(column).at(player.index) : QString();
                    if (player.index < 0 && slave.contains(QStringLiteral("-sponsor-"))) value = player.prefix;
                    QObject *object = widgetList.value(slave, nullptr);
                    if (auto *line = qobject_cast<QLineEdit *>(object)) line->setText(value);
                    else if (auto *combo = qobject_cast<QComboBox *>(object)) { if (combo->isEditable()) combo->setEditText(value); else combo->setCurrentIndex(combo->findText(value)); }
                }
            };
            apply(first, a); apply(second, b);
            if (auto *field = qobject_cast<QComboBox *>(widgetList.value(round, nullptr))) { const QString value = match.value("round").toString(); const int pos = field->findText(value); if (pos >= 0) field->setCurrentIndex(pos); else if (field->isEditable()) field->setEditText(value); }
            sggSend->setProperty("queueSendSucceeded", true);
            statusBar()->showMessage(tr("Match loaded. Save the dashboard to publish it."), 10000);
        });
    }

    auto *localQueue = qobject_cast<QComboBox *>(widgetList.value(QStringLiteral("schedule-local-queue"), nullptr));
    auto *localRecall = qobject_cast<QPushButton *>(visualList.value(QStringLiteral("schedule-local-recall"), nullptr));
    // Layouts with direct destination buttons do not need a visible recall button.
    if (localQueue && !localRecall) {
        localRecall = new QPushButton(localQueue);
        localRecall->hide();
    }
    if (localQueue && localRecall && sggList && sggSend && sggDestination) {
        auto updateQueue = [this, localQueue] {
            const int selectedRow = localQueue->currentData().toInt();
            QSignalBlocker guard(localQueue);
            localQueue->clear(); localQueue->addItem(tr("Select a queued match"), 0);
            for (int row = 1; row <= 10; ++row) {
                auto *first = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-queue-%1-p1").arg(row), nullptr));
                auto *second = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-queue-%1-p2").arg(row), nullptr));
                auto *round = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-queue-%1-round").arg(row), nullptr));
                if (first && second && round && (!first->text().trimmed().isEmpty() || !second->text().trimmed().isEmpty() || !round->text().trimmed().isEmpty()))
                    localQueue->addItem(tr("%1: %2 vs %3 — %4").arg(row).arg(first->text(), second->text(), round->text()), row);
            }
            const int index = localQueue->findData(selectedRow); if (index >= 0) localQueue->setCurrentIndex(index);
        };
        const auto compactQueue = [this, localQueue, updateQueue] {
            const int oldSelection = localQueue->currentData().toInt();
            QList<QStringList> rows;
            int newSelection = 0;
            for (int row = 1; row <= 10; ++row) {
                auto *p1 = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-queue-%1-p1").arg(row), nullptr));
                auto *p2 = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-queue-%1-p2").arg(row), nullptr));
                auto *round = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-queue-%1-round").arg(row), nullptr));
                if (!p1 || !p2 || !round) return;
                const QStringList values{p1->text(), p2->text(), round->text()};
                if (!values.at(0).trimmed().isEmpty() || !values.at(1).trimmed().isEmpty() || !values.at(2).trimmed().isEmpty()) {
                    rows.append(values);
                    if (row == oldSelection) newSelection = rows.size();
                }
            }
            for (int row = 1; row <= 10; ++row) {
                auto *p1 = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-queue-%1-p1").arg(row), nullptr));
                auto *p2 = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-queue-%1-p2").arg(row), nullptr));
                auto *round = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-queue-%1-round").arg(row), nullptr));
                QSignalBlocker block1(p1), block2(p2), block3(round);
                const QStringList values = row <= rows.size() ? rows.at(row - 1) : QStringList{QString(), QString(), QString()};
                p1->setText(values.at(0)); p2->setText(values.at(1)); round->setText(values.at(2));
            }
            updateQueue();
            const int index = localQueue->findData(newSelection);
            if (index >= 0) localQueue->setCurrentIndex(index);
        };
        for (int row = 1; row <= 10; ++row) {
            for (const QString &key : {QStringLiteral("p1"), QStringLiteral("p2"), QStringLiteral("round")}) {
                if (auto *field = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-queue-%1-%2").arg(row).arg(key), nullptr))) {
                    connect(field, &QLineEdit::textChanged, localQueue, [this, localQueue, updateQueue, compactQueue, row](const QString &) {
                        updateQueue();
                        auto *p1 = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-queue-%1-p1").arg(row), nullptr));
                        auto *p2 = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-queue-%1-p2").arg(row), nullptr));
                        auto *round = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-queue-%1-round").arg(row), nullptr));
                        if (p1 && p2 && round && p1->text().trimmed().isEmpty() && p2->text().trimmed().isEmpty() && round->text().trimmed().isEmpty())
                            QTimer::singleShot(0, localQueue, [compactQueue] { compactQueue(); });
                    });
                }
            }
        }
        updateQueue();
        connect(localRecall, &QPushButton::clicked, localQueue, [this, localQueue, sggList, sggSend] {
            const int row = localQueue->currentData().toInt();
            if (row < 1 || row > 10) { QMessageBox::information(this, tr("Match queue"), tr("Select a queue entry first.")); return; }
            auto *first = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-queue-%1-p1").arg(row), nullptr));
            auto *second = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-queue-%1-p2").arg(row), nullptr));
            auto *round = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-queue-%1-round").arg(row), nullptr));
            if (!first || !second || !round || first->text().trimmed().isEmpty() || second->text().trimmed().isEmpty()) {
                QMessageBox::warning(this, tr("Match queue"), tr("The queue entry needs both player names.")); return;
            }
            const QString firstValue = first->text(), secondValue = second->text(), roundValue = round->text();
            QVariantMap data; data.insert(QStringLiteral("p1"), firstValue); data.insert(QStringLiteral("p2"), secondValue); data.insert(QStringLiteral("round"), roundValue);
            const int previous = sggList->currentIndex();
            sggList->addItem(tr("Queued match"), data); const int temporary = sggList->count() - 1; sggList->setCurrentIndex(temporary);
            sggSend->setProperty("queueSendSucceeded", false); sggSend->click();
            const bool success = sggSend->property("queueSendSucceeded").toBool();
            sggList->removeItem(temporary); if (previous >= 0 && previous < sggList->count()) sggList->setCurrentIndex(previous);
            if (!success) return;
            if (first->text() == firstValue && second->text() == secondValue && round->text() == roundValue) {
                first->clear(); second->clear(); round->clear();
                statusBar()->showMessage(tr("Match recalled and queue row cleared. Save the dashboard to publish."), 12000);
            } else QMessageBox::information(this, tr("Match queue"), tr("The queue row changed during recall, so it was retained."));
        });
        // Optional direct destination buttons for the selected local queue row.
        for (const QString &destination : {QStringLiteral("main"), QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")}) {
            auto *button = qobject_cast<QPushButton *>(visualList.value(QStringLiteral("schedule-local-send-") + destination, nullptr));
            if (!button) continue;
            connect(button, &QPushButton::clicked, localQueue, [this, destination, sggDestination, localRecall] {
                const int destinationIndex = sggDestination->findData(destination);
                if (destinationIndex < 0) { QMessageBox::warning(this, tr("Match queue"), tr("The scoreboard destination is missing from the layout.")); return; }
                const int previousIndex = sggDestination->currentIndex();
                sggDestination->setCurrentIndex(destinationIndex);
                localRecall->click();
                if (previousIndex >= 0 && previousIndex < sggDestination->count()) sggDestination->setCurrentIndex(previousIndex);
            });
        }
    }

    //connect signal mappers

    connect (resetMapper, SIGNAL(mappedString(QString)), this, SLOT(resetFields(QString))) ;
    connect (swapMapper, SIGNAL(mappedString(QString)), this, SLOT(swapFields(QString))) ;
    connect (tsMapper, SIGNAL(mappedString(QString)), this, SLOT(tsClick(QString))) ;
    connect (setButtonMapper, SIGNAL(mappedString(QString)), this, SLOT(setButtonClick(QString))) ;

    //start the keyPoller
    if (altHotkeyHandling) {
        keyPoller->start(16);
    }

}

void MainWindow::reloadLayout() {
    loadLayout();
    loadData();
}

void MainWindow::setWidgetFontSize(QWidget *widget, int fontSize) {
    if (fontSize > 0) {
        QFont* font = new QFont(widget->font());
        font->setPointSize(fontSize);
        widget->setFont(*font);
    }
}

void MainWindow::parseLayout(QDomElement element, QWidget *parent) {
    QDomNode child = element.firstChildElement();
    while (!child.isNull()) {
        QString tagName = child.toElement().tagName();
        if (tagName == "label") {
            addLabel(child.toElement(), parent);
        } else if (tagName == "line") {
            addLine(child.toElement(), parent);
        } else if (tagName == "button") {
            addButton(child.toElement(), parent);
        } else if (tagName == "lineEdit") {
            addLineEdit(child.toElement(), parent);
        } else if (tagName == "spinBox") {
            addSpinBox(child.toElement(), parent);
        } else if (tagName == "checkBox") {
            addCheckBox(child.toElement(), parent);
        } else if (tagName == "radioGroup") {
            addRadioGroup(child.toElement(), parent);
        } else if (tagName == "comboBox") {
            addComboBox(child.toElement(), parent);
        } else if (tagName == "bracketImporter") {
            const QDomElement e = child.toElement();
            auto *panel = new BracketImportDialog(parent);
            panel->setWindowFlags(Qt::Widget);
            panel->setObjectName(QStringLiteral("bracket-import-panel"));
            panel->setGeometry(e.attribute("x", "0").toInt(), e.attribute("y", "0").toInt(),
                               e.attribute("width", "1180").toInt(), e.attribute("height", "600").toInt());
            panel->show();
        } else if (tagName == "tweet") {
            addTweetWidget(child.toElement(), parent);
        } else if (tagName == "tabSet") {
            QString newTabSet = addTabWidget(child.toElement(), parent);
            parseTabLayout(child.toElement(), visualList[newTabSet]);
        }

        child = child.nextSiblingElement();
    }
}

void MainWindow::parseTabLayout(QDomElement element, QWidget *parent) {
    QDomNode child = element.firstChildElement();
    while (!child.isNull()) {
        QString tagName = child.toElement().tagName();
        if(tagName == "tab") {
            QString newTabName = child.toElement().attribute("name");
            QString newTab = "tab"+QString::number(layoutIterator);

            visualList[newTab] = new QWidget(parent);

            parseLayout(child.toElement(),visualList[newTab]);

            if (child.toElement().attribute("scrollable") == "1") {
                visualList[newTab+"Scroll"] = new QScrollArea(parent);
                visualList[newTab]->adjustSize();
                ((QScrollArea*)visualList[newTab+"Scroll"])->setWidget(((QWidget*)visualList[newTab]));
                ((QScrollArea*)visualList[newTab+"Scroll"])->setFrameShape(QFrame::NoFrame);
                ((QTabWidget*)parent)->addTab(visualList[newTab+"Scroll"],newTabName);
            } else {
                ((QTabWidget*)parent)->addTab(visualList[newTab],newTabName);
            }

            layoutIterator++;
        }

        child = child.nextSiblingElement();
    }
}

void MainWindow::parseToolBar(QDomNode toolBarNode) {

    QDomNode child = toolBarNode.firstChildElement();
    while (!child.isNull()) {
        QString tagName = child.toElement().tagName();

        if (tagName == "label") {
            //add labels
            QString newLabel = "label"+QString::number(layoutIterator);
            visualList[newLabel] = new QLabel();
            visualList[newLabel]->setObjectName(newLabel);
            ((QLabel*)visualList[newLabel])->setText(child.toElement().text());

            toolBar->addWidget(((QLabel*)visualList[newLabel]));

            layoutIterator++;
            //end labels
        } else if (tagName == "comboBox") {
            //add comboboxes
            QDomElement comboBoxElement = child.toElement();
            QString newComboBox = comboBoxElement.attribute("id");
            widgetType[newComboBox] = "comboBox";
            widgetList[newComboBox] = new ScComboBox();
            widgetList[newComboBox]->setObjectName(newComboBox);

            int comboBoxWidth = 100;
            if (comboBoxElement.hasAttribute("width")) {
                comboBoxWidth = comboBoxElement.attribute("width").toInt();
            }
            ((ScComboBox*)widgetList[newComboBox])->setMinimumWidth(comboBoxWidth);

            if (comboBoxElement.attribute("editable") == "1" || comboBoxElement.attribute("editable") == "true") {
                ((ScComboBox*)widgetList[newComboBox])->setEditable(true);

            }

            toolBar->addWidget(((ScComboBox*)widgetList[newComboBox]));

            QDomNode combochild = comboBoxElement.firstChildElement();
            int selectedIndex = 0;
            for (int itemIndex = 0; !combochild.isNull(); itemIndex++) {
                QDomElement itemElement = combochild.toElement();
                if (itemElement.tagName() == "comboItem") {

                    QString text = itemElement.text();
                    QString value = itemElement.attribute("value");
                    if (value.isNull())
                        value = text;

                    ((ScComboBox*)widgetList[newComboBox])->addItem(text, value);

                    if (itemElement.attribute("selected") == "true")
                        selectedIndex = itemIndex;
                }

                combochild = combochild.nextSiblingElement();
            }

            ((ScComboBox*)widgetList[newComboBox])->setCurrentIndex(selectedIndex);
            //end comboboxes
        } else if (tagName == "button") {
            QDomElement buttonElement = child.toElement();
            /*if (element.attribute("type") == "reset") {
                QString newButton = buttonElement.attribute("id");

                QList<QString> resetL = CSV::parseFromString(buttonElement.attribute("reset"))[0];

                widgetType[newButton] = "resetButton";

                resetList.insert(newButton,resetL);

                int buttonWidth = 100;
                if (buttonElement.hasAttribute("width")) {
                    buttonWidth = buttonElement.attribute("width").toInt();
                }
                visualList[newButton] = new QPushButton();
                visualList[newButton]->setObjectName(newButton);

                ((QPushButton*)visualList[newButton])->setMinimumWidth(buttonWidth);

                toolBar->addWidget(((QPushButton*)visualList[newButton]));

                ((QPushButton*)visualList[newButton])->setText(buttonElement.text());
                if (!buttonElement.attribute("tooltip").isEmpty()) {
                    ((QPushButton*)visualList[newButton])->setToolTip(buttonElement.attribute("tooltip"));
                }

                connect(((QPushButton*)visualList[newButton]), SIGNAL(clicked()), resetMapper, SLOT(map()));
                resetMapper -> setMapping (((QPushButton*)visualList[newButton]), newButton) ;
                if(!element.attribute("hotkey").isEmpty()) {
                    addHotkey(element.attribute("hotkey"),newButton,"Reset");
                }


            } else if (element.attribute("type") == "swap") {

                QString newButton = element.attribute("id");

                QList<QString> swapl1 = CSV::parseFromString(element.attribute("swapSet1"))[0];
                QList<QString> swapl2 = CSV::parseFromString(element.attribute("swapSet2"))[0];

                QList<QString> swapset;

                swapset.insert(0,newButton + "1");
                swapset.insert(1,newButton + "2");

                swapSets.insert(newButton,swapset);

                swapList.insert(newButton + "1",swapl1);

                swapList.insert(newButton + "2",swapl2);

                widgetType[newButton] = "swapButton";

                visualList[newButton] = new QPushButton(parent);
                visualList[newButton]->setObjectName(newButton);
                visualList[newButton]->setGeometry(QRect(element.attribute("x").toInt(),
                                                         element.attribute("y").toInt(),
                                                         element.attribute("width").toInt(),
                                                         element.attribute("height").toInt()));
                ((QPushButton*)visualList[newButton])->setText(element.text());
                if (!element.attribute("tooltip").isEmpty()) {
                    ((QPushButton*)visualList[newButton])->setToolTip(element.attribute("tooltip"));
                }

                connect(((QPushButton*)visualList[newButton]), SIGNAL(clicked()), swapMapper, SLOT(map()));
                swapMapper -> setMapping (((QPushButton*)visualList[newButton]), newButton) ;
                if(!element.attribute("hotkey").isEmpty()) {
                    addHotkey(element.attribute("hotkey"),newButton,"Swap");
                }

            } else if (element.attribute("type") == "timestamp") {
                bool nSaveOnClick = false;

                if (element.attribute("saveonclick") == "true" || element.attribute("saveonclick") == "1") {
                    nSaveOnClick = true;
                }
                QString newButton = element.attribute("id");
                widgetType[newButton] = "tsButton";

                widgetList[newButton] = new ScTSButton(nSaveOnClick,parent);
                widgetList[newButton]->setObjectName(newButton);
                ((ScTSButton*)widgetList[newButton])->setGeometry(QRect(element.attribute("x").toInt(),
                                                   element.attribute("y").toInt(),
                                                   element.attribute("width").toInt(),
                                                   element.attribute("height").toInt()));

                ((ScTSButton*)widgetList[newButton])->setText(element.text());

                if (!element.attribute("tooltip").isEmpty()) {
                    ((ScTSButton*)widgetList[newButton])->setToolTip(element.attribute("tooltip"));
                }

                connect(((ScTSButton*)widgetList[newButton]), SIGNAL(clicked()), tsMapper, SLOT(map()));
                tsMapper -> setMapping (((ScTSButton*)widgetList[newButton]), newButton) ;
                if(!element.attribute("hotkey").isEmpty()) {
                    addHotkey(element.attribute("hotkey"),newButton,"Timestamp");
                }
            } else*/ if (buttonElement.attribute("type") == "setButton") {
                bool nSaveOnClick = false;
                if (buttonElement.attribute("saveonclick") == "true" || buttonElement.attribute("saveonclick") == "1") {
                    nSaveOnClick = true;
                }
                QString newButton = buttonElement.attribute("id");
                QString nWidget = buttonElement.attribute("widget");
                QString nValue = buttonElement.attribute("value");
                widgetType[newButton] = "setButton";

                visualList[newButton] = new ScSetButton(nSaveOnClick,nWidget,nValue,0);
                visualList[newButton]->setObjectName(newButton);

                int buttonWidth = 100;
                if (buttonElement.hasAttribute("width")) {
                    buttonWidth = buttonElement.attribute("width").toInt();
                }

                ((ScSetButton*)visualList[newButton])->setMinimumWidth(buttonWidth);

                toolBar->addWidget(((ScSetButton*)visualList[newButton]));

                ((ScSetButton*)visualList[newButton])->setText(buttonElement.text());

                if (!buttonElement.attribute("tooltip").isEmpty()) {
                    ((ScSetButton*)visualList[newButton])->setToolTip(buttonElement.attribute("tooltip"));
                }

                connect(((ScSetButton*)visualList[newButton]), SIGNAL(clicked()), setButtonMapper, SLOT(map()));
                setButtonMapper -> setMapping (((ScSetButton*)visualList[newButton]), newButton) ;
                if(!buttonElement.attribute("hotkey").isEmpty()) {
                    addHotkey(buttonElement.attribute("hotkey"),newButton,"setButton");
                }
            }
        }

        child = child.nextSiblingElement();
    }

}

void MainWindow::parseCLI(QDomNode cliNode) {

    QDomNode child = cliNode.firstChildElement();

    QRegularExpression rx("(\\[\\$[\\w\\.]+\\])");

    while (!child.isNull()) {
        if (child.toElement().tagName() == "cmd") {
            QString cmd = child.toElement().text();
            QString ignore = "";
            ignore = child.toElement().attribute("ignore");
            QStringList ignoreList = CSV::parseFromString(ignore)[0];
            int pos = 0;
            QStringList vars;
            /*while ((pos = rx.indexIn(cmd, pos)) != -1) {
                QString var = rx.cap(0).remove(0,2);
                var.chop(1);
                vars << var;
                pos += rx.matchedLength();
            }*/

            while (rx.match(cmd,pos).hasMatch()) {
                QString var = rx.match(cmd,pos).captured(0).remove(0,2);
                var.chop(1);
                vars << var;
                pos = rx.match(cmd,pos).capturedEnd(0);
            }


            int cmdIndex = cmdList.length();

            cmdList.append(cmd);
            cmdIgnores.append(ignoreList);
            qDebug() << cmd + " @ " + QString::number(cmdList.length());

            QString var;
            foreach( var, vars ) {
                if(!cmdVars[var].contains(cmdIndex)) {
                    cmdVars[var].append(cmdIndex);
                }
                cmdOldValues[var] = "";
            }

            //cmdValues[var] = "";

        }
        child = child.nextSiblingElement();
    }

    QMapIterator<QString, QList<int> > i(cmdVars);
    while (i.hasNext()) {
        i.next();

        QString list = "";
        int cmd;
        foreach (cmd,i.value()) {
            list += QString::number(cmd) + "! ";
        }

        qDebug() << i.key() << ": " << list;
    }

}

void MainWindow::addLabel(QDomElement element, QWidget *parent) {

    QString newLabel = "label"+QString::number(layoutIterator);
    visualList[newLabel] = new QLabel(parent);
    visualList[newLabel]->setObjectName(newLabel);
    visualList[newLabel]->setGeometry(QRect(element.attribute("x").toInt(),
                                                          element.attribute("y").toInt(),
                                                          element.attribute("width").toInt(),
                                                          element.attribute("height").toInt()));
    setWidgetFontSize(visualList[newLabel], element.attribute("fontSize", 0).toInt());
    ((QLabel*)visualList[newLabel])->setText(element.text());
    const QString alignment = element.attribute("align").trimmed().toLower();
    if (alignment == "left") ((QLabel*)visualList[newLabel])->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    else if (alignment == "center") ((QLabel*)visualList[newLabel])->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    else if (alignment == "right") ((QLabel*)visualList[newLabel])->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Optional read-only mirror of a scalar value in a local JSON file.
    // @schedule follows the schedule dialog's saved output location.
    const QString mirrorFile = element.attribute("mirrorFile").trimmed();
    const QString mirrorPath = element.attribute("mirrorPath").trimmed();
    if (!mirrorFile.isEmpty() && !mirrorPath.isEmpty()) {
        visualList[newLabel]->setProperty("scRemoteMirrorKey", mirrorFile + QLatin1Char('\n') + mirrorPath);
        QLabel *label = static_cast<QLabel *>(visualList[newLabel]);
        const QString fallback = element.text();
        auto *timer = new QTimer(label);
        timer->setInterval(1000);
        const auto updateMirror = [this, label, mirrorFile, mirrorPath, fallback] {
            QString path = mirrorFile;
            if (path == QStringLiteral("@schedule")) {
                PortableSettings settings(QStringLiteral("StreamControlPlus"), QStringLiteral("Schedule"));
                path = settings.value(QStringLiteral("outputPath")).toString();
            } else if (path == QStringLiteral("@bingo")) {
                PortableSettings bingoSettings(QStringLiteral("StreamControlPlus"), QStringLiteral("BingoScores"));
                path = bingoSettings.value(QStringLiteral("outputPath")).toString();
            } else if (QFileInfo(path).isRelative()) {
                path = QFileInfo(settings.value("layoutPath")).dir().filePath(path);
            }
            QFile file(path);
            if (path.isEmpty() || !file.open(QIODevice::ReadOnly) || file.size() > 2 * 1024 * 1024) { label->setText(fallback); return; }
            QJsonParseError error;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
            if (error.error != QJsonParseError::NoError) { label->setText(fallback); return; }
            QJsonValue value;
            if (doc.isObject()) value = QJsonValue(doc.object());
            else if (doc.isArray()) value = QJsonValue(doc.array());
            else { label->setText(fallback); return; }
            for (const QString &part : mirrorPath.split(QLatin1Char('.'))) {
                if (value.isObject()) value = value.toObject().value(part);
                else if (value.isArray()) {
                    bool ok = false; int index = part.toInt(&ok);
                    const QJsonArray array = value.toArray();
                    value = ok && index >= 0 && index < array.size() ? array.at(index) : QJsonValue();
                } else { value = QJsonValue(); break; }
            }
            if (value.isString()) label->setText(value.toString());
            else if (value.isDouble()) label->setText(QString::number(value.toDouble(), 'g', 15));
            else if (value.isBool()) label->setText(value.toBool() ? QStringLiteral("true") : QStringLiteral("false"));
            else label->setText(fallback);
        };
        connect(timer, &QTimer::timeout, label, updateMirror);
        updateMirror(); timer->start();
    }

    layoutIterator++;
}

void MainWindow::addTweetWidget(QDomElement element, QWidget *parent) {

    QString newTweet = element.attribute("id");
    widgetList[newTweet] = new TwitterWidget(parent);
    widgetType[newTweet] = "tweet";
    widgetList[newTweet]->setObjectName(newTweet);
    ((TwitterWidget*)widgetList[newTweet])->setGeometry(QRect(element.attribute("x").toInt(),
                                                          element.attribute("y").toInt(),
                                                          element.attribute("width").toInt(),
                                                          element.attribute("height").toInt()));
    setWidgetFontSize((TwitterWidget*)widgetList[newTweet], element.attribute("fontSize", 0).toInt());

    QString picPath = "twitter"; // default path to twitter so the output directory isn't filled with pictures

    if(element.attribute("picPath") != "") {
        picPath = element.attribute("picPath");
    }

    QDir newPath(element.attribute(picPath));

    if (!newPath.isAbsolute()) {
        QString path = settings["outputPath"] + element.attribute("picPath");
        newPath.setPath(path);

    }

    qDebug() << "path:" + newPath.path();

    if(!newPath.exists()) {
        newPath.mkpath("media");
    }

    ((TwitterWidget*)widgetList[newTweet])->setPath(newPath.path() + "/");

    if(element.attribute("embedJSON") == "1") {
        ((TwitterWidget*)widgetList[newTweet])->setEmbed(true);
    }

    layoutIterator++;
}

void MainWindow::addProviderWidget(QDomElement element, QWidget *parent,
                                         QMap<QString, QObject*>, ProviderWidgetBuilder::Provider provider)
{
    // a map with the expected number of children for a tournament stage
    QMap<QString, int> stages;
    stages.insert("grandFinal", 1);
    stages.insert("grandFinalReset", 1);
    stages.insert("winnersFinal", 1);
    stages.insert("winnersSemiFinal", 2);
    stages.insert("winnersQuarterFinal", 4);
    stages.insert("losersFinal", 1);
    stages.insert("losersSemiFinal", 1);
    stages.insert("top6Losers", 2);
    stages.insert("top8Losers", 2);
    stages.insert("top12Losers", 4);
    stages.insert("top16Losers", 4);

    QList<QString> stageNames = stages.keys();

    ProviderWidgetBuilder builder(parent, widgetList, settings);
    QString newWidgetId = element.attribute("id");

    QString outputType = element.attribute("outputType");

    builder.setOutputFileName(element.attribute("outputFileName"));

    foreach(const QString& stage, stageNames)
    {
        // Get the xml elements for the tournament stage
        QDomNodeList stageElements = element.elementsByTagName(stage);

        //permit there to be no elements for a stage
        if (stageElements.length() != 0 && stageElements.length() != stages[stage])
        {
            //Incorrect
            qDebug() << "Incorrect number of elements (" << stageElements.length() <<
                        ") for stage " << stage;
        }

        for (int i = 0; i < stageElements.length(); i++)
        {
            const QDomElement& stageElement = stageElements.item(i).toElement();
            builder.addMatchWidget(stage,
                                   stageElement.attribute("p1name"), stageElement.attribute("p1score"),
                                   stageElement.attribute("p2name"), stageElement.attribute("p2score"));
        }
    }

    builder.setPlayerNameWidgets(element.attribute("playerOneWidget"),
                                 element.attribute("playerTwoWidget"));
    builder.setPlayerCountryWidgets(element.attribute("playerOneCountryWidget"),
                                    element.attribute("playerTwoCountryWidget"));

    builder.setTournamentStageWidget(element.attribute("tournamentStageWidget"));
    builder.setBracketStageWidget(element.attribute("bracketWidget"));

    builder.setClearWidgets(CSV::parseFromString(element.attribute("clearWidgets"))[0]);

    ProviderWidget* newWidget = builder.build(provider);

    newWidget->setObjectName(newWidgetId);
    newWidget->setGeometry(QRect(element.attribute("x").toInt(),
                                 element.attribute("y").toInt(),
                                 element.attribute("width").toInt(),
                                 element.attribute("height").toInt()));
    setWidgetFontSize(newWidget, element.attribute("fontSize", 0).toInt());

    widgetList[newWidgetId] = newWidget;
    widgetType[newWidgetId] = "provider";
    layoutIterator++;
}

void MainWindow::addLine(QDomElement element, QWidget *parent) {

    QString newLine = "line"+QString::number(layoutIterator);
    visualList[newLine] = new QLabel(parent);
    visualList[newLine]->setObjectName(newLine);
    visualList[newLine]->setGeometry(QRect(element.attribute("x").toInt(),
                                                          element.attribute("y").toInt(),
                                                          element.attribute("width").toInt(),
                                                          element.attribute("height").toInt()));
    setWidgetFontSize(visualList[newLine], element.attribute("fontSize", 0).toInt());
    ((QFrame*)visualList[newLine])->setFrameShape(QFrame::HLine);
    ((QFrame*)visualList[newLine])->setFrameShadow(QFrame::Sunken);

    layoutIterator++;
}

void MainWindow::addCheckBox(QDomElement element, QWidget *parent) {

    QString newCheckBox = element.attribute("id");
    widgetType[newCheckBox] = "checkBox";
    widgetList[newCheckBox] = new QCheckBox(parent);
    widgetList[newCheckBox]->setObjectName(newCheckBox);
    ((QCheckBox*)widgetList[newCheckBox])->setGeometry(QRect(element.attribute("x").toInt(),
                                                          element.attribute("y").toInt(),
                                                          element.attribute("width").toInt(),
                                                          element.attribute("height").toInt()));
    setWidgetFontSize((QCheckBox*)widgetList[newCheckBox], element.attribute("fontSize", 0).toInt());
    ((QCheckBox*)widgetList[newCheckBox])->setText(element.text());
    if(!element.attribute("toggleHotkey").isEmpty()) {
        addHotkey(element.attribute("toggleHotkey"),newCheckBox,"Toggle");
    }
    if(!element.attribute("checkHotkey").isEmpty()) {
        addHotkey(element.attribute("checkHotkey"),newCheckBox,"Check");
    }
    if(!element.attribute("uncheckHotkey").isEmpty()) {
        addHotkey(element.attribute("uncheckHotkey"),newCheckBox,"Uncheck");
    }
}

void MainWindow::addComboBox(QDomElement element, QWidget *parent) {

    QString newComboBox = element.attribute("id");
    widgetType[newComboBox] = "comboBox";
    widgetList[newComboBox] = new ScComboBox(parent);
    widgetList[newComboBox]->setObjectName(newComboBox);

    ((ScComboBox*)widgetList[newComboBox])->setGeometry(QRect(element.attribute("x").toInt(),
                                                          element.attribute("y").toInt(),
                                                          element.attribute("width").toInt(),
                                                          element.attribute("height").toInt()));
    setWidgetFontSize((ScComboBox*)widgetList[newComboBox], element.attribute("fontSize", 0).toInt());

    if (element.attribute("editable") == "true" || element.attribute("editable") == "1") {
        ((ScComboBox*)widgetList[newComboBox])->setEditable(true);

    }

    const QString catalogFile = element.attribute("catalogFile").trimmed();
    if (!catalogFile.isEmpty())
        ((ScComboBox*)widgetList[newComboBox])->setProperty("scCatalogFile", catalogFile);
    const QString catalogType = element.attribute("catalogType").trimmed();
    if (!catalogType.isEmpty())
        ((ScComboBox*)widgetList[newComboBox])->setProperty("scCatalogType", catalogType);
    QVariantList conditionalItems;
    const QString dependsOn = element.attribute("dependsOn").trimmed();
    if (!dependsOn.isEmpty() && !element.hasAttribute("dataSet")) {
        ((ScComboBox*)widgetList[newComboBox])->setProperty("scDependsOn", dependsOn);
        for (QDomElement item = element.firstChildElement("comboItem"); !item.isNull(); item = item.nextSiblingElement("comboItem")) {
            QVariantMap entry; entry.insert("text", item.text());
            entry.insert("value", item.hasAttribute("value") ? item.attribute("value") : item.text());
            entry.insert("when", item.attribute("when")); conditionalItems.append(entry);
        }
        ((ScComboBox*)widgetList[newComboBox])->setProperty("scConditionalItems", conditionalItems);
    }
    QDomNode child = element.firstChildElement();
    int selectedIndex = 0;
    for (int itemIndex = 0; !child.isNull(); itemIndex++) {
        QDomElement itemElement = child.toElement();
        if (itemElement.tagName() == "comboItem") {

            QString text = itemElement.text();
            QString value = itemElement.attribute("value");
            if (value.isNull())
                value = text;

            ((ScComboBox*)widgetList[newComboBox])->addItem(text, value);

            if (itemElement.attribute("selected") == "true" || itemElement.attribute("selected") == "1")
                selectedIndex = itemIndex;
        }

        child = child.nextSiblingElement();
    }

    ((ScComboBox*)widgetList[newComboBox])->setCurrentIndex(selectedIndex);

    if(element.hasAttribute("dataSet")) {
        QString dataSetName = element.attribute("dataSet");
        if (dataSets[dataSetName].isEmpty()) {
            QString setPath = QFileInfo(settings["layoutPath"]).path() +"/"+ dataSetName;
            QList<QStringList> newDataSet = CSV::parseFromFile(setPath,"UTF-8");

            dataSets[dataSetName] = processDataSet(newDataSet);
            condensedDataSets[dataSetName] = condenseDataSet(dataSets[dataSetName]);
        }
        int dataField = 0;
        if (element.hasAttribute("dataField")) {
            dataField = element.attribute("dataField").toInt() - 1;

            for (int i = dataSets[dataSetName].length(); i <= dataField; i++) {
                QStringList newList;

                for (int i2 = 0; i2 < dataSets[dataSetName][0].length();i2++){
                    newList.insert(i2,"");
                }
                dataSets[dataSetName].insert(i,newList);
                condensedDataSets[dataSetName] = condenseDataSet(dataSets[dataSetName]);
            }

        }

        dataAssoc[newComboBox] = dataField;
        completerList[newComboBox] = new ScCompleter(parent);
        bool hasMaster;
        ((ScLineEdit*)widgetList[newComboBox])->setName(newComboBox,dataSetName);
        if (element.hasAttribute("master")) {
            QStringListModel *model = new QStringListModel(condensedDataSets[dataSetName][dataField]);
            completerList[newComboBox]->setModel(model);
            dataMaster[element.attribute("master")].append(newComboBox);
            ((ScComboBox*)widgetList[newComboBox])->setButtonVisible(false);
            ((ScComboBox*)widgetList[newComboBox])->addModel(model);
            hasMaster = true;
        } else {
            QStringListModel *model = new QStringListModel(dataSets[dataSetName][dataField]);
            completerList[newComboBox]->setModel(model);
            connect(((ScLineEdit*)widgetList[newComboBox]),SIGNAL(editTextChanged(QString)), this, SLOT(checkComboDataSet(QString)));
            connect(((ScLineEdit*)widgetList[newComboBox]),SIGNAL(clearButtonClicked()), this, SLOT(removeComboFromDataSet()));
            ((ScComboBox*)widgetList[newComboBox])->addModel(model);
            hasMaster = false;

        }
        completerList[newComboBox]->setName(newComboBox,dataSetName,dataField,hasMaster);
        completerList[newComboBox]->setCaseSensitivity(Qt::CaseInsensitive);
        completerList[newComboBox]->setCompletionMode(QCompleter::PopupCompletion);
        completerList[newComboBox]->setFilterMode(Qt::MatchContains);

        ((ScComboBox*)widgetList[newComboBox])->setCompleter(completerList[newComboBox]);

        connect(completerList[newComboBox], SIGNAL(activated(QString)), this, SLOT(completerActivate(QString)));

    }

    if(!element.attribute("nextHotkey").isEmpty()) {
        addHotkey(element.attribute("nextHotkey"),newComboBox,"Next");
    }
    if(!element.attribute("previousHotkey").isEmpty()) {
        addHotkey(element.attribute("previousHotkey"),newComboBox,"Previous");
    }
}

void MainWindow::addRadioGroup(QDomElement element, QWidget *parent) {

    QString newRadioGroup = element.attribute("id");
    widgetType[newRadioGroup] = "radioGroup";
    widgetList[newRadioGroup] = new ScRadioGroup(parent);
    widgetList[newRadioGroup]->setObjectName(newRadioGroup);

    QDomNode child = element.firstChildElement();
    int radioIterator = 0;
    while (!child.isNull()) {
        if (child.toElement().tagName() == "radioButton"){

            QDomElement radioButton = child.toElement();
            int x = radioButton.attribute("x").toInt();
            int y = radioButton.attribute("y").toInt();
            int width = radioButton.attribute("width").toInt();
            int height = radioButton.attribute("height").toInt();
            QString text = radioButton.text();
            QString value = radioButton.attribute("value");
            QString radioName = "radio" + value;

            QRadioButton *newRadioButton = new QRadioButton(text,parent);
            newRadioButton->setGeometry(QRect(x,y,width,height));
            newRadioButton->setObjectName(radioName);
            setWidgetFontSize(newRadioButton, radioButton.attribute("fontSize", 0).toInt() || element.attribute("fontSize", 0).toInt());
            if(radioIterator == 0)
                newRadioButton->setChecked(true);
            ((ScRadioGroup*)widgetList[newRadioGroup])->addButton(newRadioButton,radioIterator);
            ((ScRadioGroup*)widgetList[newRadioGroup])->setValue(radioIterator,value);
            radioIterator++;

        }

        child = child.nextSiblingElement();
    }

}

void MainWindow::addButton(QDomElement element, QWidget *parent) {
    if (element.attribute("type") == "startggQueueAction") {
        const QString id = element.attribute("id");
        auto *button = new QPushButton(element.text(), parent); button->setObjectName(id);
        button->setGeometry(element.attribute("x").toInt(), element.attribute("y").toInt(), element.attribute("width").toInt(), element.attribute("height").toInt());
        visualList[id] = button; widgetType[id] = "startggQueueAction";
    } else if (element.attribute("type") == "matchHistory") {
        const QString id = element.attribute("id");
        const QString p1 = element.attribute("player1"), p2 = element.attribute("player2");
        const QString round = element.attribute("round"), score1 = element.attribute("score1"), score2 = element.attribute("score2");
        auto *button = new QPushButton(element.text(), parent);
        button->setObjectName(id);
        button->setProperty("remotePlayer1", p1); button->setProperty("remotePlayer2", p2);
        button->setProperty("remoteScore1", score1); button->setProperty("remoteScore2", score2);
        button->setGeometry(element.attribute("x").toInt(), element.attribute("y").toInt(), element.attribute("width").toInt(), element.attribute("height").toInt());
        visualList[id] = button; widgetType[id] = "matchHistory";
        connect(button, &QPushButton::clicked, this, [this, p1, p2, round, score1, score2] {
            auto textFor = [this](const QString &key) -> QString {
                QObject *object = widgetList.value(key, nullptr);
                if (auto *line = qobject_cast<QLineEdit *>(object)) return line->text();
                if (auto *combo = qobject_cast<QComboBox *>(object)) return combo->currentText();
                if (auto *spin = qobject_cast<QSpinBox *>(object)) return QString::number(spin->value());
                return QString();
            };
            QList<QLineEdit *> fields;
            for (int row = 1; row <= 10; ++row) {
                for (const QString &field : {QStringLiteral("p1"), QStringLiteral("p2"), QStringLiteral("round"), QStringLiteral("result")}) {
                    auto *edit = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-history-%1-%2").arg(row).arg(field), nullptr));
                    if (!edit) { QMessageBox::warning(this, tr("Match history"), tr("The layout needs all ten history rows with player, round, and result fields.")); return; }
                    fields.append(edit);
                }
            }
            if (textFor(p1).trimmed().isEmpty() || textFor(p2).trimmed().isEmpty() || !qobject_cast<QSpinBox *>(widgetList.value(score1, nullptr)) || !qobject_cast<QSpinBox *>(widgetList.value(score2, nullptr))) {
                QMessageBox::warning(this, tr("Match history"), tr("Enter both player or team names and ensure score controls are present.")); return;
            }
            QString firstName = textFor(p1), secondName = textFor(p2);
            if (p1 == QStringLiteral("scoreboard-player-1") && p2 == QStringLiteral("scoreboard-player-2")) {
                const QString firstPartner = textFor(QStringLiteral("scoreboard-player-3")).trimmed();
                const QString secondPartner = textFor(QStringLiteral("scoreboard-player-4")).trimmed();
                if (!firstPartner.isEmpty()) firstName += QStringLiteral(" / ") + firstPartner;
                if (!secondPartner.isEmpty()) secondName += QStringLiteral(" / ") + secondPartner;
            }
            const QStringList values = { firstName, secondName, textFor(round), textFor(score1) + QStringLiteral(" - ") + textFor(score2) };
            for (int row = 9; row > 0; --row)
                for (int col = 0; col < 4; ++col) fields[row * 4 + col]->setText(fields[(row - 1) * 4 + col]->text());
            for (int col = 0; col < 4; ++col) fields[col]->setText(values[col]);
            QStringList resetButtons;
            if (p1 == QStringLiteral("scoreboard-player-1")) {
                resetButtons = {QStringLiteral("scoreboard-clear-left"), QStringLiteral("scoreboard-clear-right"), QStringLiteral("scoreboard-clear-score")};
            } else if (p1.startsWith(QStringLiteral("multi-player-1"))) {
                const QString setup = p1.mid(QStringLiteral("multi-player-1").size());
                if (setup.size() == 1 && QStringLiteral("abcd").contains(setup))
                    resetButtons = {QStringLiteral("multi-clear-player-1") + setup, QStringLiteral("multi-clear-player-2") + setup, QStringLiteral("multi-clear-score-") + setup};
            } else if (p1 == QStringLiteral("teams-team-1-name")) {
                resetButtons = {QStringLiteral("teams-clear-left"), QStringLiteral("teams-clear-right"), QStringLiteral("teams-clear-score"), QStringLiteral("teams-clear-match-score")};
            }
            for (const QString &button : resetButtons) if (resetList.contains(button)) resetFields(button);
            statusBar()->showMessage(tr("Match added to history and scoreboard cleared. Save the dashboard to publish it."), 10000);
        });
    } else 
    if (element.attribute("type") == "teamLoad" || element.attribute("type") == "teamSave" || element.attribute("type") == "teamDelete") {
        const QString id=element.attribute("id");
        auto *button=new QPushButton(element.text(),parent);
        button->setObjectName(id);
        button->setGeometry(QRect(element.attribute("x").toInt(),element.attribute("y").toInt(),element.attribute("width").toInt(),element.attribute("height").toInt()));
        setWidgetFontSize(button,element.attribute("fontSize",0).toInt());
        visualList[id]=button; widgetType[id]=element.attribute("type");
    } else if (element.attribute("type") == "reset") {
        QString newButton = element.attribute("id");

        QList<QString> resetL = CSV::parseFromString(element.attribute("reset"))[0];

        widgetType[newButton] = "resetButton";

        resetList.insert(newButton,resetL);

        visualList[newButton] = new QPushButton(parent);
        visualList[newButton]->setProperty("scButtonRole", "reset");
        visualList[newButton]->setObjectName(newButton);
        visualList[newButton]->setGeometry(QRect(element.attribute("x").toInt(),
                                           element.attribute("y").toInt(),
                                           element.attribute("width").toInt(),
                                           element.attribute("height").toInt()));
        setWidgetFontSize(visualList[newButton], element.attribute("fontSize", 0).toInt());
        ((QPushButton*)visualList[newButton])->setText(element.text());
        if (!element.attribute("tooltip").isEmpty()) {
            ((QPushButton*)visualList[newButton])->setToolTip(element.attribute("tooltip"));
        }

        connect(((QPushButton*)visualList[newButton]), SIGNAL(clicked()), resetMapper, SLOT(map()));
        resetMapper -> setMapping (((QPushButton*)visualList[newButton]), newButton) ;
        if(!element.attribute("hotkey").isEmpty()) {
            addHotkey(element.attribute("hotkey"),newButton,"Reset");
        }

    } else if (element.attribute("type") == "swap") {

        QString newButton = element.attribute("id");

        QList<QString> swapl1 = CSV::parseFromString(element.attribute("swapSet1"))[0];
        QList<QString> swapl2 = CSV::parseFromString(element.attribute("swapSet2"))[0];

        QList<QString> swapset;

        swapset.insert(0,newButton + "1");
        swapset.insert(1,newButton + "2");

        swapSets.insert(newButton,swapset);

        swapList.insert(newButton + "1",swapl1);

        swapList.insert(newButton + "2",swapl2);

        widgetType[newButton] = "swapButton";

        visualList[newButton] = new QPushButton(parent);
        visualList[newButton]->setProperty("scButtonRole", "swap");
        visualList[newButton]->setObjectName(newButton);
        visualList[newButton]->setGeometry(QRect(element.attribute("x").toInt(),
                                                 element.attribute("y").toInt(),
                                                 element.attribute("width").toInt(),
                                                 element.attribute("height").toInt()));
        setWidgetFontSize(visualList[newButton], element.attribute("fontSize", 0).toInt());
        ((QPushButton*)visualList[newButton])->setText(element.text());
        if (!element.attribute("tooltip").isEmpty()) {
            ((QPushButton*)visualList[newButton])->setToolTip(element.attribute("tooltip"));
        }

        connect(((QPushButton*)visualList[newButton]), SIGNAL(clicked()), swapMapper, SLOT(map()));
        swapMapper -> setMapping (((QPushButton*)visualList[newButton]), newButton) ;
        if(!element.attribute("hotkey").isEmpty()) {
            addHotkey(element.attribute("hotkey"),newButton,"Swap");
        }

    } else if (element.attribute("type") == "timestamp") {
        bool nSaveOnClick = false;

        if (element.attribute("saveonclick") == "true" || element.attribute("saveonclick") == "1") {
            nSaveOnClick = true;
        }
        QString newButton = element.attribute("id");
        widgetType[newButton] = "tsButton";

        widgetList[newButton] = new ScTSButton(nSaveOnClick,parent);
        widgetList[newButton]->setObjectName(newButton);
        ((ScTSButton*)widgetList[newButton])->setGeometry(QRect(element.attribute("x").toInt(),
                                           element.attribute("y").toInt(),
                                           element.attribute("width").toInt(),
                                           element.attribute("height").toInt()));
        setWidgetFontSize((ScTSButton*)widgetList[newButton], element.attribute("fontSize", 0).toInt());

        ((ScTSButton*)widgetList[newButton])->setText(element.text());

        if (!element.attribute("tooltip").isEmpty()) {
            ((ScTSButton*)widgetList[newButton])->setToolTip(element.attribute("tooltip"));
        }

        connect(((ScTSButton*)widgetList[newButton]), SIGNAL(clicked()), tsMapper, SLOT(map()));
        tsMapper -> setMapping (((ScTSButton*)widgetList[newButton]), newButton) ;
        if(!element.attribute("hotkey").isEmpty()) {
            addHotkey(element.attribute("hotkey"),newButton,"Timestamp");
        }
    } else if (element.attribute("type") == "scoreStep") {
        const QString buttonId = element.attribute("id");
        const QString target = element.attribute("target");
        const int step = element.attribute("step").toInt();
        auto *button = new QPushButton(element.text(), parent);
        button->setObjectName(buttonId);
        button->setGeometry(QRect(element.attribute("x").toInt(), element.attribute("y").toInt(), element.attribute("width").toInt(), element.attribute("height").toInt()));
        setWidgetFontSize(button, element.attribute("fontSize", 0).toInt());
        button->setProperty("scoreTarget", target); button->setProperty("scoreStep", step);
        visualList[buttonId] = button; widgetType[buttonId] = "scoreStep";
        connect(button, &QPushButton::clicked, this, [this, button, target, step] {
            button->setProperty("scorePublishError",QString());
            if (widgetType.value(target) != "spinBox") {button->setProperty("scorePublishError",tr("Invalid score target."));return;}
            auto *score = qobject_cast<QSpinBox *>(widgetList.value(target));
            if (!score) {button->setProperty("scorePublishError",tr("Score control is unavailable."));return;}
            const int previous=score->value();
            if (step > 0) score->stepUp(); else score->stepDown();
            if(score->value()==previous)return;
            QString error;
            if(!publishScoreOnly(settings.value("outputPath"),saveFormat,target,score->value(),wsServer,&error)){
                button->setProperty("scorePublishError",error);
                statusBar()->showMessage(tr("Score not published: %1").arg(error),15000);
            }
        });
    } else if (element.attribute("type") == "setButton") {
        bool nSaveOnClick = false;
        if (element.attribute("saveonclick") == "true" || element.attribute("saveonclick") == "1") {
            nSaveOnClick = true;
        }
        QString newButton = element.attribute("id");
        QString nWidget = element.attribute("widget");
        QString nValue = element.attribute("value");
        widgetType[newButton] = "setButton";

        visualList[newButton] = new ScSetButton(nSaveOnClick,nWidget,nValue,parent);
        visualList[newButton]->setObjectName(newButton);

        ((ScSetButton*)visualList[newButton])->setGeometry(QRect(element.attribute("x").toInt(),
                                           element.attribute("y").toInt(),
                                           element.attribute("width").toInt(),
                                           element.attribute("height").toInt()));
        setWidgetFontSize(visualList[newButton], element.attribute("fontSize", 0).toInt());

        ((ScSetButton*)visualList[newButton])->setText(element.text());

        if (!element.attribute("tooltip").isEmpty()) {
            ((ScSetButton*)visualList[newButton])->setToolTip(element.attribute("tooltip"));
        }

        connect(((ScSetButton*)visualList[newButton]), SIGNAL(clicked()), setButtonMapper, SLOT(map()));
        setButtonMapper -> setMapping (((ScSetButton*)visualList[newButton]), newButton) ;
        if(!element.attribute("hotkey").isEmpty()) {
            addHotkey(element.attribute("hotkey"),newButton,"setButton");
        }
    }
}

void MainWindow::tsClick(QString tsButton) {
    if (((ScTSButton*)widgetList[tsButton])->isActive()) {
        ((ScTSButton*)widgetList[tsButton])->setActive(false);
    } else {
        ((ScTSButton*)widgetList[tsButton])->setActive(true);
    }

    if (((ScTSButton*)widgetList[tsButton])->isSaveOnClick()) {
        saveData();
    }
}

void MainWindow::setButtonClick(QString setButton) {
    QString wType = "";
    QString widget = ((ScSetButton*)visualList[setButton])->getWidget();
    QString value = ((ScSetButton*)visualList[setButton])->getValue();
    wType = widgetType[widget];

    if (wType == "comboBox") {
        setComboValue(widget, value);
    }

    //ADD MORE WIDGETS TYPES

    if (((ScSetButton*)visualList[setButton])->isSaveOnClick()) {
        saveData();
    }
}

/*
    Selects the combo item whose value (or text) matches. Editable combos fall
    back to setting the text directly. Returns false if nothing could be set.
*/
bool MainWindow::setComboValue(QString widget, QString value) {
    ScComboBox *comboBox = (ScComboBox*)widgetList[widget];
    QVariant data(value);
    int valueIndex = comboBox->findData(data);
    if (valueIndex == -1) {
        valueIndex = comboBox->findText(value);
    }
    if (valueIndex != -1) {
        comboBox->setCurrentIndex(valueIndex);
    } else if (comboBox->isEditable()) {
        comboBox->setCurrentText(value);
    } else {
        return false;
    }
    return true;
}

/*
    Runs a batch of actions sent by an authenticated websocket client, in order.
    Returns one {ok, error} result per action.
*/
QJsonArray MainWindow::runRemoteActions(const QJsonArray &actions) {
    QJsonArray results;
    for (const QJsonValue &actionValue : actions) {
        QJsonObject result;
        QString error;
        if (actionValue.isObject()) {
            const QJsonObject action = actionValue.toObject();
            if (action.value(QStringLiteral("action")).toString() == QStringLiteral("snapshot")) {
                QJsonObject snapshot, controls, datasets, mirrors;
                QFile layoutFile(settings.value(QStringLiteral("layoutPath")));
                if (!layoutFile.open(QIODevice::ReadOnly) || layoutFile.size() > 2 * 1024 * 1024) {
                    error = QStringLiteral("Could not read active layout, or layout exceeds 2 MiB");
                } else {
                    snapshot.insert(QStringLiteral("layout"), QString::fromUtf8(layoutFile.readAll()));
                    for (auto it = widgetList.cbegin(); it != widgetList.cend(); ++it) {
                        const QString id = it.key(); const QString type = widgetType.value(id); QObject *object = it.value();
                        QJsonObject control; control.insert(QStringLiteral("type"), type);
                        if (auto *line = qobject_cast<QLineEdit *>(object)) control.insert(QStringLiteral("value"), line->text());
                        else if (auto *combo = qobject_cast<QComboBox *>(object)) {
                            control.insert(QStringLiteral("value"), id == QStringLiteral("schedule-startgg-queue") ? QString::number(combo->currentIndex()) : getValueByID(id));
                            QJsonArray options;
                            for (int i = 0; i < combo->count(); ++i) {
                                const QVariant data = combo->itemData(i);
                                QJsonObject option; option.insert(QStringLiteral("label"), combo->itemText(i));
                                option.insert(QStringLiteral("value"), id == QStringLiteral("schedule-startgg-queue") ? QString::number(i) : (data.isNull() ? combo->itemText(i) : data.toString()));
                                options.append(option);
                            }
                            control.insert(QStringLiteral("options"), options);
                        } else if (auto *spin = qobject_cast<QSpinBox *>(object)) control.insert(QStringLiteral("value"), spin->value());
                        else if (auto *check = qobject_cast<QCheckBox *>(object)) control.insert(QStringLiteral("value"), check->isChecked());
                        else if (type == QStringLiteral("radioGroup") || type == QStringLiteral("tsButton")) control.insert(QStringLiteral("value"), getValueByID(id));
                        else continue;
                        if (auto *widget = qobject_cast<QWidget *>(object)) control.insert(QStringLiteral("enabled"), widget->isEnabled());
                        controls.insert(id, control);
                    }
                    for (auto it = dataSets.cbegin(); it != dataSets.cend(); ++it) {
                        QJsonArray columns;
                        for (const QStringList &column : it.value()) columns.append(QJsonArray::fromStringList(column));
                        datasets.insert(it.key(), columns);
                    }
                    for (auto it = visualList.cbegin(); it != visualList.cend(); ++it) {
                        auto *label = qobject_cast<QLabel *>(it.value());
                        if (!label) continue;
                        const QString key = label->property("scRemoteMirrorKey").toString();
                        if (!key.isEmpty()) mirrors.insert(key, label->text());
                    }
                    snapshot.insert(QStringLiteral("mirrors"), mirrors);
                    if (auto *dialog = findChild<QDialog *>(QStringLiteral("sc-remote-schedule")))
                        snapshot.insert(QStringLiteral("schedule"), static_cast<ScheduleDialog *>(dialog)->remoteState());
                    if (auto *dialog = findChild<QDialog *>(QStringLiteral("sc-remote-timers")))
                        snapshot.insert(QStringLiteral("timers"), static_cast<BroadcastControl *>(dialog)->remoteState());
                    snapshot.insert(QStringLiteral("controls"), controls);
                    snapshot.insert(QStringLiteral("datasets"), datasets);
                    result.insert(QStringLiteral("data"), snapshot);
                }
            } else if (action.value(QStringLiteral("action")).toString() == QStringLiteral("schedule_control")) {
                auto *dialog = findChild<QDialog *>(QStringLiteral("sc-remote-schedule"));
                if (!dialog) error = QStringLiteral("Schedule controls are unavailable");
                else error = static_cast<ScheduleDialog *>(dialog)->remoteAction(action.value(QStringLiteral("command")).toString());
            } else if (action.value(QStringLiteral("action")).toString() == QStringLiteral("timer_control")) {
                auto *dialog = findChild<QDialog *>(QStringLiteral("sc-remote-timers"));
                if (!dialog) error = QStringLiteral("Timer controls are unavailable");
                else error = static_cast<BroadcastControl *>(dialog)->remoteAction(action.value(QStringLiteral("command")).toString(), action);
            } else if (action.value(QStringLiteral("action")).toString() == QStringLiteral("dataset_select")) {
                const QString id = action.value(QStringLiteral("id")).toString();
                auto *master = qobject_cast<ScLineEdit *>(widgetList.value(id, nullptr));
                const int row = action.value(QStringLiteral("row")).toInt(-1);
                if (!master || !dataAssoc.contains(id) || row < 0) error = QStringLiteral("Invalid dataset field or row");
                else {
                    const QString dataset = master->getDataSetName();
                    const QList<QStringList> columns = dataSets.value(dataset);
                    const int masterCol = dataAssoc.value(id);
                    if (dataset.isEmpty() || masterCol < 0 || masterCol >= columns.size() || row >= columns.at(masterCol).size() || columns.at(masterCol).at(row) != action.value(QStringLiteral("expected")).toString()) error = QStringLiteral("Dataset changed; refresh and select again");
                    else {
                        bool valid = true;
                        for (const QString &slave : dataMaster.value(id)) {
                            const int col = dataAssoc.value(slave, -1);
                            if (col < 0 || col >= columns.size() || row >= columns.at(col).size()) { valid = false; break; }
                        }
                        if (!valid) error = QStringLiteral("Dataset row is incomplete");
                        else {
                            master->setText(columns.at(masterCol).at(row));
                            for (const QString &slave : dataMaster.value(id)) {
                                const QString value = columns.at(dataAssoc.value(slave)).at(row);
                                if (auto *line = qobject_cast<QLineEdit *>(widgetList.value(slave, nullptr))) line->setText(value);
                                else if (auto *combo = qobject_cast<QComboBox *>(widgetList.value(slave, nullptr))) combo->setEditText(value);
                            }
                        }
                    }
                }
            } else error = applyRemoteAction(action);
        } else {
            error = "action must be an object";
        }
        result["ok"] = error.isEmpty();
        if (!error.isEmpty())
            result["error"] = error;
        results.append(result);
    }
    return results;
}

/*
    Applies one remote action. Returns an empty string on success, otherwise the error.
*/
QString MainWindow::applyRemoteAction(const QJsonObject &action) {
    QString actionName = action["action"].toString();

    if (actionName == "save") {
        saveData();
        return "";
    }

    QString id = action["id"].toString();
    if (id.isEmpty())
        return "missing 'id'";

    // value() so unknown ids don't get inserted into the map
    QString wType = widgetType.value(id);
    if (wType.isEmpty())
        return "unknown widget '" + id + "'";

    if (actionName == "press") {
        if (wType == "startggQueueAction" && id == QStringLiteral("schedule-startgg-add")) {
            auto *queue = qobject_cast<QComboBox *>(widgetList.value(QStringLiteral("schedule-startgg-queue"), nullptr));
            if (!queue || queue->currentData().toMap().isEmpty()) return "Select a Start.gg match first";
            const QVariantMap match = queue->currentData().toMap();
            for (int row = 1; row <= 10; ++row) {
                auto *p1 = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-queue-%1-p1").arg(row), nullptr));
                auto *p2 = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-queue-%1-p2").arg(row), nullptr));
                auto *round = qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-queue-%1-round").arg(row), nullptr));
                if (!p1 || !p2 || !round) return "The layout is missing queue fields";
                if (p1->text().trimmed().isEmpty() && p2->text().trimmed().isEmpty() && round->text().trimmed().isEmpty()) {
                    p1->setText(match.value(QStringLiteral("p1")).toString());
                    p2->setText(match.value(QStringLiteral("p2")).toString());
                    round->setText(match.value(QStringLiteral("round")).toString());
                    return QString();
                }
            }
            return "All ten queue rows are occupied";
        } else if (wType == "resetButton") {
            resetFields(id);
        } else if (wType == "swapButton") {
            swapFields(id);
        } else if (wType == "tsButton") {
            tsClick(id);
        } else if (wType == "setButton") {
            setButtonClick(id);
        } else if (wType == "matchHistory") {
            auto *button = qobject_cast<QPushButton *>(visualList.value(id));
            if (!button) return "invalid match history button";
            const auto playerText = [this](const QString &key) -> QString {
                QObject *object = widgetList.value(key, nullptr);
                if (auto *line = qobject_cast<QLineEdit *>(object)) return line->text();
                if (auto *combo = qobject_cast<QComboBox *>(object)) return combo->currentText();
                return QString();
            };
            if (playerText(button->property("remotePlayer1").toString()).trimmed().isEmpty() || playerText(button->property("remotePlayer2").toString()).trimmed().isEmpty() ||
                !qobject_cast<QSpinBox *>(widgetList.value(button->property("remoteScore1").toString(), nullptr)) || !qobject_cast<QSpinBox *>(widgetList.value(button->property("remoteScore2").toString(), nullptr)))
                return "Enter both players and ensure the score controls exist";
            for (int row = 1; row <= 10; ++row) for (const QString &field : {QStringLiteral("p1"), QStringLiteral("p2"), QStringLiteral("round"), QStringLiteral("result")})
                if (!qobject_cast<QLineEdit *>(widgetList.value(QStringLiteral("schedule-history-%1-%2").arg(row).arg(field), nullptr))) return "The layout needs ten complete match history rows";
            button->click();
        } else if (wType == "scoreStep") {
            auto *button = qobject_cast<QPushButton *>(visualList.value(id));
            if (!button || widgetType.value(button->property("scoreTarget").toString()) != "spinBox") return "invalid score target for '" + id + "'";
            button->click();
            const QString publishError=button->property("scorePublishError").toString();
            if(!publishError.isEmpty())return publishError;
        } else {
            return "'" + id + "' is not a button";
        }
        return "";
    }

    if (actionName != "set")
        return "unknown action '" + actionName + "'";

    if (!action.contains("value"))
        return "missing 'value'";

    QJsonValue value = action["value"];
    // strings stay as-is, numbers become "2" rather than "2.0"
    QString valueStr = value.isString() ? value.toString() : value.toVariant().toString();

    if (wType == "lineEdit") {
        if (value.isArray() || value.isObject() || value.isNull())
            return "value for '" + id + "' must be a string";
        ((ScLineEdit*)widgetList[id])->setText(valueStr);
    } else if (wType == "spinBox") {
        QSpinBox *spinBox = (QSpinBox*)widgetList[id];
        bool ok = false;
        int intValue = 0;
        if (value.isDouble()) {
            double d = value.toDouble();
            intValue = static_cast<int>(d);
            ok = (d == intValue);
        } else if (value.isString()) {
            intValue = valueStr.trimmed().toInt(&ok);
        }
        if (!ok)
            return "value for '" + id + "' must be an integer";
        if (intValue < spinBox->minimum() || intValue > spinBox->maximum())
            return "value for '" + id + "' is out of range (" + QString::number(spinBox->minimum())
                    + "-" + QString::number(spinBox->maximum()) + ")";
        spinBox->setValue(intValue);
    } else if (wType == "checkBox") {
        bool checked;
        if (value.isBool()) {
            checked = value.toBool();
        } else if (valueStr == "1" || valueStr == "true") {
            checked = true;
        } else if (valueStr == "0" || valueStr == "false") {
            checked = false;
        } else {
            return "value for '" + id + "' must be true/false or 1/0";
        }
        ((QCheckBox*)widgetList[id])->setChecked(checked);
    } else if (wType == "comboBox" && id == QStringLiteral("schedule-startgg-queue")) {
        auto *combo = qobject_cast<QComboBox *>(widgetList.value(id, nullptr));
        bool ok = false; const int index = valueStr.toInt(&ok);
        if (!combo || !ok || index < 0 || index >= combo->count()) return "Invalid Start.gg queue selection";
        if (combo->itemText(index) != action.value(QStringLiteral("expectedLabel")).toString()) return "Start.gg queue changed; wait for a refresh and select again";
        combo->setCurrentIndex(index);
    } else if (wType == "comboBox") {
        if (!setComboValue(id, valueStr))
            return "'" + valueStr + "' is not an item of '" + id + "'";
    } else if (wType == "radioGroup") {
        if (!((ScRadioGroup*)widgetList[id])->checkFromValue(valueStr))
            return "'" + valueStr + "' is not a value of '" + id + "'";
    } else {
        return "'" + id + "' (" + wType + ") cannot be set";
    }

    return "";
}

void MainWindow::addLineEdit(QDomElement element, QWidget *parent) {

    QString newLineEdit = element.attribute("id");

    widgetList[newLineEdit] = new ScLineEdit(parent);
    widgetList[newLineEdit]->setObjectName(newLineEdit);
    ((ScLineEdit*)widgetList[newLineEdit])->setGeometry(QRect(element.attribute("x").toInt(),
                                             element.attribute("y").toInt(),
                                             element.attribute("width").toInt(),
                                             element.attribute("height").toInt()));
    setWidgetFontSize((ScLineEdit*)widgetList[newLineEdit], element.attribute("fontSize", 0).toInt());
    const QString alignment = element.attribute("align").trimmed().toLower();
    if (alignment == "left") ((ScLineEdit*)widgetList[newLineEdit])->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    else if (alignment == "center") ((ScLineEdit*)widgetList[newLineEdit])->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    else if (alignment == "right") ((ScLineEdit*)widgetList[newLineEdit])->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    widgetType[newLineEdit] = "lineEdit";

    if (element.text() != "") {
        ((ScLineEdit*)widgetList[newLineEdit])->setPlaceholderText(element.text());
    }

    if (element.hasAttribute("noSave")) {
        if (element.attribute("noSave") == "1") {
            ((ScLineEdit*)widgetList[newLineEdit])->setNoSaveOnChange(true);
        }
    }

    if(element.hasAttribute("dataSet")) {
        QString dataSetName = element.attribute("dataSet");
        if (dataSets[dataSetName].isEmpty()) {
            QString setPath = QFileInfo(settings["layoutPath"]).path() +"/"+ dataSetName;
            QList<QStringList> newDataSet = CSV::parseFromFile(setPath,"UTF-8");

            dataSets[dataSetName] = processDataSet(newDataSet);
            condensedDataSets[dataSetName] = condenseDataSet(dataSets[dataSetName]);
        }
        int dataField = 0;
        if (element.hasAttribute("dataField")) {
            dataField = element.attribute("dataField").toInt() - 1;

            for (int i = dataSets[dataSetName].length(); i <= dataField; i++) {
                QStringList newList;

                for (int i2 = 0; i2 < dataSets[dataSetName][0].length();i2++){
                    newList.insert(i2,"");
                }
                dataSets[dataSetName].insert(i,newList);
                condensedDataSets[dataSetName] = condenseDataSet(dataSets[dataSetName]);
            }

        }

        dataAssoc[newLineEdit] = dataField;
        completerList[newLineEdit] = new ScCompleter(parent);
        bool hasMaster;
        ((ScLineEdit*)widgetList[newLineEdit])->setName(newLineEdit,dataSetName);
        if (element.hasAttribute("master")) {
            QStringListModel *model = new QStringListModel(condensedDataSets[dataSetName][dataField]);
            completerList[newLineEdit]->setModel(model);
            dataMaster[element.attribute("master")].append(newLineEdit);
            ((ScLineEdit*)widgetList[newLineEdit])->setButtonVisible(false);
            hasMaster = true;
        } else {
            QStringListModel *model = new QStringListModel(dataSets[dataSetName][dataField]);
            completerList[newLineEdit]->setModel(model);
            connect(((ScLineEdit*)widgetList[newLineEdit]),SIGNAL(textChanged(QString)), this, SLOT(checkLineDataSet(QString)));
            connect(((ScLineEdit*)widgetList[newLineEdit]),SIGNAL(clearButtonClicked()), this, SLOT(removeLineFromDataSet()));
            hasMaster = false;

        }
        completerList[newLineEdit]->setName(newLineEdit,dataSetName,dataField,hasMaster);
        completerList[newLineEdit]->setCaseSensitivity(Qt::CaseInsensitive);
        completerList[newLineEdit]->setCompletionMode(QCompleter::PopupCompletion);
        completerList[newLineEdit]->setFilterMode(Qt::MatchContains);

        ((ScLineEdit*)widgetList[newLineEdit])->setCompleter(completerList[newLineEdit]);

        connect(completerList[newLineEdit], SIGNAL(activated(QString)), this, SLOT(completerActivate(QString)));

    }
}

void MainWindow::completerActivate(QString item) {
    QString scSender = ((ScCompleter*)sender())->getName();
    QString scSetName = ((ScCompleter*)sender())->getDataSetName();
    int field = dataAssoc[scSender];
    int index = dataSets[scSetName][field].indexOf(item);

    for (int i = 0; i < dataMaster[scSender].length();i++) {
        QString id = dataMaster[scSender][i];
        int newField = dataAssoc[id];
        if (widgetType[id] == "lineEdit"){
            ((ScLineEdit*)widgetList[id])->setText(dataSets[scSetName][newField][index]);
        }
        if (widgetType[id] == "comboBox"){
            ((ScComboBox*)widgetList[id])->setEditText(dataSets[scSetName][newField][index]);
        }
    }
}

void MainWindow::removeLineFromDataSet() {
    QString scSender = ((ScLineEdit*)sender())->getName();
    QString scSetName = ((ScLineEdit*)sender())->getDataSetName();

    QString item = ((ScLineEdit*)sender())->text();

    int field = dataAssoc[scSender];
    int index = dataSets[scSetName][field].indexOf(item);

    int numSets = dataSets[scSetName].length();

    for (int i = 0; i < numSets; i++) {
        dataSets[scSetName][i].removeAt(index);
    }

    condensedDataSets[scSetName] = condenseDataSet(dataSets[scSetName]);

    int numSlave = dataMaster[scSender].length();
    for (int i = 0; i < numSlave; i++) {
        QString widget = dataMaster[scSender][i];
        if (widget == "lineEdit") {
            ((ScLineEdit*)widgetList[widget])->clear();
        } else if (widget == "comboBox") {
            ((ScComboBox*)widgetList[widget])->clearEditText();
        }
    }

    removedSetQueue.append(scSetName);

    updateCompleters();

    ((ScLineEdit*)sender())->clear();


}

void MainWindow::removeComboFromDataSet() {
    QString scSender = ((ScComboBox*)sender())->getName();
    QString scSetName = ((ScComboBox*)sender())->getDataSetName();

    QString item;

        int currentIndex = ((ScComboBox*)widgetList[scSender])->currentIndex();
        //QString value;

        item = ((ScComboBox*)widgetList[scSender])->currentText();

        int valueIndex = ((ScComboBox*)widgetList[scSender])->findText(item);
        if (valueIndex != -1) {
            QVariant data = ((ScComboBox*)widgetList[scSender])->itemData(currentIndex);
            if (data.isNull())
                item = ((ScComboBox*)widgetList[scSender])->itemText(currentIndex);
            else
                item = data.toString();
        }

    //QString item = ((ScComboBox*)sender())->editTextChanged();

    int field = dataAssoc[scSender];
    int index = dataSets[scSetName][field].indexOf(item);

    int numSets = dataSets[scSetName].length();

    for (int i = 0; i < numSets; i++) {
        dataSets[scSetName][i].removeAt(index);
    }

    condensedDataSets[scSetName] = condenseDataSet(dataSets[scSetName]);

    int numSlave = dataMaster[scSender].length();
    for (int i = 0; i < numSlave; i++) {
        QString widget = dataMaster[scSender][i];
        if (widget == "lineEdit") {
            ((ScLineEdit*)widgetList[widget])->clear();
        } else if (widget == "comboBox") {
            ((ScComboBox*)widgetList[widget])->clearEditText();
        }
    }

    removedSetQueue.append(scSetName);

    updateCompleters();

    ((ScComboBox*)sender())->clearEditText();


}

void MainWindow::updateCompleters() {
    QMapIterator<QString, ScCompleter *> i(completerList);
    while (i.hasNext()) {
        i.next();
        QString name = i.key();
        QString setName = completerList[name]->getDataSetName();
        int dataField = completerList[name]->getDataField();

        if (completerList[name]->hasMaster() == true) {
            QStringListModel *model = new QStringListModel(condensedDataSets[setName][dataField]);
            completerList[name]->setModel(model);
        } else {
            QStringListModel *model = new QStringListModel(dataSets[setName][dataField]);
            completerList[name]->setModel(model);
        }
    }
}

void MainWindow::checkLineDataSet(QString line) {
    QString scSender = ((ScLineEdit*)sender())->getName();
    QString scSetName = ((ScLineEdit*)sender())->getDataSetName();
    int field = dataAssoc[scSender];
    int index = dataSets[scSetName][field].indexOf(line);

    if(index != -1) {
        ((ScLineEdit*)widgetList[scSender])->setButtonVisible(true);
    } else {
        ((ScLineEdit*)widgetList[scSender])->setButtonVisible(false);
    }
}

void MainWindow::checkComboDataSet(QString line) {
    QString scSender = ((ScComboBox*)sender())->getName();
    QString scSetName = ((ScComboBox*)sender())->getDataSetName();
    int field = dataAssoc[scSender];
    int index = dataSets[scSetName][field].indexOf(line);

    if(index != -1) {
        ((ScComboBox*)widgetList[scSender])->setButtonVisible(true);
    } else {
        ((ScComboBox*)widgetList[scSender])->setButtonVisible(false);
    }
}

QList<QStringList> MainWindow::processDataSet(QList<QStringList> oldSet) {
    int items = oldSet.length() - 1;
    int sets = oldSet[0].length() - 1;

    //this bit is confusing should probably fix it
    bool empty = true;
    for (int i = 0; i <= items; i++) {
        if (items > 0 || items == -1)
            empty = false;
        if (items == 0 && oldSet[0][i] != "")
            empty = false;
    }

    QList<QStringList> newSet;
    for (int i = 0; i <= sets; i++){
        QStringList newList;
        for (int i2 = 0; i2 <= items; i2++){
            if (!empty)
                newList.append(oldSet[i2][i]);
        }
        newSet.append(newList);
    }
    return newSet;
}

QList<QStringList> MainWindow::condenseDataSet(QList<QStringList> oldSet) {
    QList<QStringList> newSet;

    int sets = oldSet.length() - 1;
    for (int i = 0; i <= sets; i++){
        //QStringList newList = oldSet[i].toSet().toList();
        QStringList newList(oldSet[i].begin(),oldSet[i].end());
        newSet.append(newList);
    }

    return newSet;
}

void MainWindow::saveDataSets() {
    // Validate master/slave dataset links before changing any dataset in memory.
    // A mismatched file can otherwise index a different dataset's columns.
    for(auto it=dataMaster.cbegin();it!=dataMaster.cend();++it) {
        ScCompleter *master=completerList.value(it.key(),nullptr);
        if(!master){QMessageBox::warning(this,tr("Dataset configuration"),tr("Player dataset master '%1' is missing. Dashboard output was saved, but datasets were not saved.").arg(it.key()));return;}
        const QString masterSet=master->getDataSetName();
        for(const QString &slaveName:it.value()) {
            ScCompleter *slave=completerList.value(slaveName,nullptr);
            if(!slave || slave->getDataSetName()!=masterSet){
                QMessageBox::warning(this,tr("Dataset configuration"),tr("Field '%1' and its master '%2' must use the same dataset. Dashboard output was saved, but datasets were not saved.").arg(slaveName,it.key()));return;
            }
        }
    }
    for(auto it=dataSets.cbegin();it!=dataSets.cend();++it) {
        if(it.value().isEmpty())continue;
        const qsizetype rows=it.value().first().size();
        for(const QStringList &column:it.value())if(column.size()!=rows){
            QMessageBox::warning(this,tr("Dataset configuration"),tr("Dataset '%1' has columns with different numbers of entries. Dashboard output was saved, but datasets were not saved. Check the CSV before retrying.").arg(it.key()));return;
        }
    }
    for(auto it=completerList.cbegin();it!=completerList.cend();++it) {
        ScCompleter *completer=it.value();
        if(!completer || !widgetList.value(it.key()) || !dataSets.contains(completer->getDataSetName()) || completer->getDataField()<0 || completer->getDataField()>=dataSets.value(completer->getDataSetName()).size()){
            QMessageBox::warning(this,tr("Dataset configuration"),tr("Field '%1' has an invalid dataset column. Dashboard output was saved, but datasets were not saved.").arg(it.key()));return;
        }
    }
    QMapIterator<QString, ScCompleter *> i(completerList);
    QStringList updatedSets;
    while (i.hasNext()) {
        i.next();
        QString name = i.key();
        QString setName = completerList[name]->getDataSetName();
        int dataField = completerList[name]->getDataField();
        //check if it's set to not save on change (note to self: please rewrite this.)
        bool noSaveOnChange;
        if (widgetType[name] == "lineEdit") {
            noSaveOnChange = ((ScLineEdit*)widgetList[i.key()])->noSaveOnChange();
        } else if (widgetType[name] == "comboBox") {
            noSaveOnChange = ((ScComboBox*)widgetList[i.key()])->noSaveOnChange();
        }

        if (completerList[name]->hasMaster() == false && noSaveOnChange != true) { //if it's a master & doesn't have noSave set
            //check if current value is in dataSet
            QString currentVal;
            if (widgetType[name] == "lineEdit") {
                currentVal = ((ScLineEdit*)widgetList[name])->text();
            } else if (widgetType[name] == "comboBox") {
                int currentIndex = ((ScComboBox*)widgetList[i.key()])->currentIndex();
                //QString value;

                currentVal = ((ScComboBox*)widgetList[name])->currentText();

                int valueIndex = ((ScComboBox*)widgetList[name])->findText(currentVal);
                if (valueIndex != -1) {
                    QVariant data = ((ScComboBox*)widgetList[name])->itemData(currentIndex);
                    if (data.isNull())
                        currentVal = ((ScComboBox*)widgetList[name])->itemText(currentIndex);
                    else
                        currentVal = data.toString();
                }
            }
            int currIndex =  dataSets[setName][dataField].indexOf(currentVal);

            if (currIndex == -1 && currentVal != "") {
                //set all to blank first
                int numFields = dataSets[setName].length();
                for (int i = 0; i < numFields; i++) {
                    if (!checkDataSet1Blank(setName))
                        dataSets[setName][i].append("");
                }

                int newIndex = dataSets[setName][dataField].length() - 1;

                QString newValue;

                if (widgetType[name] == "lineEdit") {
                    newValue = ((ScLineEdit*)widgetList[name])->text();
                } else if (widgetType[name] == "comboBox") {
                    int currentIndex = ((ScComboBox*)widgetList[name])->currentIndex();
                    //QString value;

                    newValue = ((ScComboBox*)widgetList[name])->currentText();

                    int valueIndex = ((ScComboBox*)widgetList[name])->findText(newValue);
                    if (valueIndex != -1) {
                        QVariant data = ((ScComboBox*)widgetList[name])->itemData(currentIndex);
                        if (data.isNull())
                            newValue = ((ScComboBox*)widgetList[name])->itemText(currentIndex);
                        else
                            newValue = data.toString();
                    }
                }
                dataSets[setName][dataField][newIndex] = newValue;

                //find each field that has name as master
                int slaveNum = dataMaster[name].length();
                for (int i = 0; i < slaveNum; i++) {
                    QString slaveName = dataMaster[name][i];
                    int field = dataAssoc[slaveName];
                    if (field != dataField) {
                        QString newValue;

                        if (widgetType[slaveName] == "lineEdit") {
                            newValue = ((ScLineEdit*)widgetList[slaveName])->text();
                        } else if (widgetType[slaveName] == "comboBox") {
                            int currentIndex = ((ScComboBox*)widgetList[slaveName])->currentIndex();
                            //QString value;

                            newValue = ((ScComboBox*)widgetList[slaveName])->currentText();

                            int valueIndex = ((ScComboBox*)widgetList[slaveName])->findText(newValue);
                            if (valueIndex != -1) {
                                QVariant data = ((ScComboBox*)widgetList[slaveName])->itemData(currentIndex);
                                if (data.isNull())
                                    newValue = ((ScComboBox*)widgetList[slaveName])->itemText(currentIndex);
                                else
                                    newValue = data.toString();
                            }
                        }
                        //QString newValue = ((ScLineEdit*)widgetList[slaveName])->text();
                        dataSets[setName][field][newIndex] = newValue;
                    }
                }
                if (updatedSets.indexOf(setName) == -1)
                    updatedSets.append(setName);
                if (widgetType[name] == "lineEdit") {
                    ((ScLineEdit*)widgetList[name])->setButtonVisible(true);
                } else if (widgetType[name] == "comboBox") {
                    ((ScComboBox*)widgetList[name])->setButtonVisible(true);
                }

            } else if (currentVal != "") { //if has in set
                //find each field that has name as master
                int slaveNum = dataMaster[name].length();
                for (int i = 0; i < slaveNum; i++) {
                    QString slaveName = dataMaster[name][i];
                    int field = dataAssoc[slaveName];
                    if (field != dataField) {
                        QString newValue;

                        if (widgetType[slaveName] == "lineEdit") {
                            newValue = ((ScLineEdit*)widgetList[slaveName])->text();
                        } else if (widgetType[slaveName] == "comboBox") {
                            int currentIndex = ((ScComboBox*)widgetList[slaveName])->currentIndex();
                            //QString value;

                            newValue = ((ScComboBox*)widgetList[slaveName])->currentText();

                            int valueIndex = ((ScComboBox*)widgetList[slaveName])->findText(newValue);
                            if (valueIndex != -1) {
                                QVariant data = ((ScComboBox*)widgetList[slaveName])->itemData(currentIndex);
                                if (data.isNull())
                                    newValue = ((ScComboBox*)widgetList[slaveName])->itemText(currentIndex);
                                else
                                    newValue = data.toString();
                            }
                        }
                        //QString newValue = ((ScLineEdit*)widgetList[slaveName])->text();
                        if (dataSets[setName][field][currIndex] != newValue){ // if not new value update
                            dataSets[setName][field][currIndex] = newValue;
                            if (updatedSets.indexOf(setName) == -1)
                                updatedSets.append(setName);
                        }//end update
                    }
                }


            }// end if has in set


        }// end if has master


    }

    if (!removedSetQueue.isEmpty()) {
        updatedSets.append(removedSetQueue);
        removedSetQueue.clear();
    }

    int numUpdate = updatedSets.length();

    for (int i = 0; i < numUpdate; i++) {
        condensedDataSets[updatedSets[i]] = condenseDataSet(dataSets[updatedSets[i]]);

        QList<QStringList> newList = processDataSet(dataSets[updatedSets[i]]);

        QString outputFile = QFileInfo(settings["layoutPath"]).path() +"/"+ updatedSets[i];
        CSV::write(newList,outputFile,"UTF-8");

    }

    updateCompleters();
}

void MainWindow::addSpinBox(QDomElement element, QWidget *parent) {

    QString newSpinBox = element.attribute("id");

    widgetList[newSpinBox] = new QSpinBox(parent);
    widgetList[newSpinBox]->setObjectName(newSpinBox);
    ((QSpinBox*)widgetList[newSpinBox])->setGeometry(QRect(element.attribute("x").toInt(),
                                              element.attribute("y").toInt(),
                                              element.attribute("width").toInt(),
                                              element.attribute("height").toInt()));
    setWidgetFontSize((QSpinBox*)widgetList[newSpinBox], element.attribute("fontSize", 0).toInt());
    if(!element.attribute("maximum").isEmpty()) {
        ((QSpinBox*)widgetList[newSpinBox])->setMaximum(element.attribute("maximum").toInt());
    }
    if(!element.attribute("increaseHotkey").isEmpty()) {
        addHotkey(element.attribute("increaseHotkey"),newSpinBox,"Increase");
    }
    if(!element.attribute("decreaseHotkey").isEmpty()) {
        addHotkey(element.attribute("decreaseHotkey"),newSpinBox,"Decrease");
    }
    widgetType[newSpinBox] = "spinBox";

}

QString MainWindow::addTabWidget(QDomElement element, QWidget *parent) {

    QString tabSet = "tabSet"+QString::number(layoutIterator);
    visualList[tabSet] = new QTabWidget(parent);
    visualList[tabSet]->setObjectName(tabSet);
    visualList[tabSet]->setGeometry(QRect(element.attribute("x").toInt(),
                                                          element.attribute("y").toInt(),
                                                          element.attribute("width").toInt(),
                                                          element.attribute("height").toInt()));
    setWidgetFontSize(visualList[tabSet], element.attribute("fontSize", 0).toInt());

    layoutIterator++;
    return tabSet;
}

QDomDocument MainWindow::getDefaultLayout() {
    QString xmlcontent = "<!DOCTYPE StreamControlLayout>\r\n<layout width=\"400\" height=\"140\" tabbed=\"1\">\r\n <toolBar>\r\n  <label>Game</label>\r\n  <comboBox id=\"game\" editable=\"true\">\r\n   <comboItem>Super Street Fighter IV</comboItem>\r\n   <comboItem>Ultimate Marvel vs Capcom 3</comboItem>\r\n   <comboItem>Persona 4: Arena</comboItem>\r\n   <comboItem>Tekken Tag Tournament 2</comboItem>\r\n   <comboItem>King of Fighters XIII</comboItem>\r\n  </comboBox>\r\n </toolBar>\r\n <tab name=\"Match Info\">\r\n  <label x=\"10\" y=\"14\" width=\"46\" height=\"13\">Player 1</label>\r\n  <label x=\"10\" y=\"44\" width=\"46\" height=\"13\">Player 2</label>\r\n  <label x=\"300\" y=\"14\" width=\"46\" height=\"13\">Rounds</label>\r\n  <lineEdit id=\"pName1\" x=\"60\" y=\"10\" width=\"171\" height=\"20\" />\r\n  <lineEdit id=\"pName2\" x=\"60\" y=\"40\" width=\"171\" height=\"20\" />\r\n  <spinBox id=\"pScore1\" x=\"250\" y=\"10\" width=\"42\" height=\"22\" maximum=\"999\" />\r\n  <spinBox id=\"pScore2\" x=\"250\" y=\"40\" width=\"42\" height=\"22\" maximum=\"999\" />\r\n  <spinBox id=\"rounds\" x=\"340\" y=\"10\" width=\"42\" height=\"22\" maximum=\"999\" />\r\n  <button type=\"reset\" x=\"300\" y=\"40\" width=\"41\" height=\"23\" tooltip=\"Reset the Scores\" id=\"reset\" reset=\"pScore1,pScore2\">Reset</button>\r\n  <button type=\"swap\" x=\"340\" y=\"40\" width=\"41\" height=\"23\" tooltip=\"Swap the Scores\" id=\"swap1\" swapSet1=\"pName1,pScore1\" swapSet2=\"pName2,pScore2\">Swap</button>\r\n </tab>\r\n <tab name=\"Commentary\">\r\n  <label x=\"10\" y=\"14\" width=\"46\" height=\"13\">Title 1</label>\r\n  <label x=\"10\" y=\"44\" width=\"46\" height=\"13\">Title 2</label>\r\n  <lineEdit id=\"cTitle1\" x=\"60\" y=\"10\" width=\"321\" height=\"20\" />\r\n  <lineEdit id=\"cTitle2\" x=\"60\" y=\"40\" width=\"321\" height=\"20\" />\r\n </tab>\r\n <tab name=\"Misc 1\">\r\n  <label x=\"10\" y=\"14\" width=\"46\" height=\"13\">mText 1</label>\r\n  <label x=\"10\" y=\"44\" width=\"46\" height=\"13\">mText 2</label>\r\n  <lineEdit id=\"mText1\" x=\"60\" y=\"10\" width=\"321\" height=\"20\" />\r\n  <lineEdit id=\"mText2\" x=\"60\" y=\"40\" width=\"321\" height=\"20\" />\r\n </tab>\r\n <tab name=\"Misc 2\">\r\n  <label x=\"10\" y=\"14\" width=\"46\" height=\"13\">mText 3</label>\r\n  <label x=\"10\" y=\"44\" width=\"46\" height=\"13\">mText 4</label>\r\n  <lineEdit id=\"mText3\" x=\"60\" y=\"10\" width=\"321\" height=\"20\" />\r\n  <lineEdit id=\"mText4\" x=\"60\" y=\"40\" width=\"321\" height=\"20\" />\r\n </tab>\r\n</layout>\r\n";
    QDomDocument doc;
    doc.setContent(xmlcontent);
    return doc;
}

void MainWindow::clearMaps() {
    QList<QAction *> tbList = toolBar->actions();

    for (QAction *action : tbList) {
        if (!action->property("scPersistent").toBool()) toolBar->removeAction(action);
    }

    widgetList.clear();
    visualList.clear();
    widgetType.clear();
    resetList.clear();
    swapSets.clear();
    swapList.clear();
    dataSets.clear();
    condensedDataSets.clear();
    completerList.clear();
    dataAssoc.clear();
    dataMaster.clear();
    resetMapper = new QSignalMapper (this) ;
    swapMapper = new QSignalMapper (this) ;
    tsMapper = new QSignalMapper (this) ;
    setButtonMapper = new QSignalMapper (this) ;

    cmdList.clear();
    cmdOldValues.clear();
    cmdVars.clear();
    cmdIgnores.clear();
}

bool MainWindow::checkDataSet1Blank(QString setName) {
    bool empty = true;
    int numFields = dataSets[setName].length();
    for (int i = 0; i < numFields; i++) {
        if (dataSets[setName][i].length() > 1 || dataSets[setName][i].length() == 0)
            empty = false;
        if (dataSets[setName][i].length() == 1 && dataSets[setName][i][0] != "")
            empty = false;

    }
    return empty;
}

void MainWindow::addHotkey(QString hotkey,QString widget, QString action) {
    QKeySequence qks(hotkey);
    int ks = qks[0];
    int modifiers = win_keyhelper::getModifiers(ks) | 0x4000; //add no-repeat modifier
    int key = win_keyhelper::getKey(ks);

    QStringList hotkeyItem;

    hotkey = qks.toString();

    hotkeyItem.append(hotkey);
    hotkeyItem.append(widget);
    hotkeyItem.append(action);

    hotkeys.append(hotkeyItem);
    hotkeysIndex.append(qks[0]);

    #ifdef Q_OS_WIN
    if (!altHotkeyHandling) {
        if (RegisterHotKey(
                (HWND(this->winId())),
                hotkeysIndex.count(),
                modifiers,
                key))
            {
                qDebug() << "registered hotkey: \"" + hotkey + "\" at " + QString::number(hotkeysIndex.count());
            }
    }
    #endif
}

void MainWindow::performHotkey(int hotkeyIndex) {
    QStringList hotkeyItem = hotkeys.at(hotkeyIndex);
    QString widget = hotkeyItem.at(1);
    QString action = hotkeyItem.at(2);

    if (widget == "Main") {
        if (action == "Save") {
            qDebug() << "Saving Data via Hotkey";
            saveData();
        }
    } else {
        //do widget actions and stuff
        QString wType = widgetType[widget];
        if (wType == "spinBox") {
            int step = ((QSpinBox*)widgetList[widget])->singleStep();
            int value = ((QSpinBox*)widgetList[widget])->value();
            if (action == "Increase") {
                ((QSpinBox*)widgetList[widget])->setValue(value + step);
                qDebug() << "increasing " + widget;
            } else if (action == "Decrease") {
                ((QSpinBox*)widgetList[widget])->setValue(value - step);
                qDebug() << "decreasing " + widget;
            }
        } else if (wType == "resetButton") {
            resetFields(widget);
        } else if (wType == "swapButton") {
            swapFields(widget);
        } else if (wType == "tsButton") {
            tsClick(widget);
        } else if (wType == "setButton") {
            setButtonClick(widget);
        } else if (wType == "checkBox") {
            if (action == "Toggle") {
                bool checked = ((QCheckBox*)widgetList[widget])->isChecked();
                if (checked){
                    ((QCheckBox*)widgetList[widget])->setChecked(false);
                } else {
                    ((QCheckBox*)widgetList[widget])->setChecked(true);
                }
            } else if (action == "Check") {
                ((QCheckBox*)widgetList[widget])->setChecked(true);
            } else if (action == "Uncheck") {
                ((QCheckBox*)widgetList[widget])->setChecked(false);
            }
        } else if (wType == "comboBox") {
            int currentIndex = ((ScComboBox*)widgetList[widget])->currentIndex();
            int length = ((ScComboBox*)widgetList[widget])->count();
            if (action == "Next") {
                int nextIndex = (currentIndex + 1 == length) ? 0 : currentIndex + 1;
                ((ScComboBox*)widgetList[widget])->setCurrentIndex(nextIndex);
            } else if (action == "Previous") {
                int previousIndex = (currentIndex - 1 == -1) ? length - 1 : currentIndex - 1;
                ((ScComboBox*)widgetList[widget])->setCurrentIndex(previousIndex);
            }
        }
    }
}

void MainWindow::deleteHotkeys() {
    int num = hotkeysIndex.count();
    for (int i = 1; i <= num; i++) {
        #ifdef Q_OS_WIN
        if(UnregisterHotKey(HWND(this->winId()), i))
        {
            qDebug() << "UNREGISTED " + hotkeys.at(i-1).at(0);
        }
        #endif
    }

    hotkeys.clear();
    hotkeysIndex.clear();
}
