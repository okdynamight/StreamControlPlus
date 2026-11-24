#include "broadcastcontrol.h"
#include "twitchbroadcastdialog.h"
#include "twitchpointsdialog.h"

#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include "../portablesettings.h"
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGlobal>
#include <climits>

BroadcastControl::BroadcastControl(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Broadcast timers"));
    PortableSettings saved(QStringLiteral("StreamControlPlus"), QStringLiteral("BroadcastControls"));
    outputPath = saved.value("outputPath").toString();
    resize(480, 250);
    auto *layout = new QVBoxLayout(this);
    auto *duration = new QHBoxLayout;
    hours = new QSpinBox(this); hours->setRange(0, 999); hours->setSuffix(tr(" h"));
    minutes = new QSpinBox(this); minutes->setRange(0, 59); minutes->setSuffix(tr(" m"));
    seconds = new QSpinBox(this); seconds->setRange(0, 59); seconds->setSuffix(tr(" s"));
    duration->addWidget(new QLabel(tr("Duration:"), this));
    duration->addWidget(hours); duration->addWidget(minutes); duration->addWidget(seconds);
    layout->addLayout(duration);
    timerLabel = new QLabel(this); layout->addWidget(timerLabel);
    auto *controls = new QHBoxLayout;
    startButton = new QPushButton(tr("Start"), this);
    pauseButton = new QPushButton(tr("Pause"), this);
    stopButton = new QPushButton(tr("Stop"), this);
    resetButton = new QPushButton(tr("Reset"), this);
    controls->addWidget(startButton); controls->addWidget(pauseButton);
    controls->addWidget(stopButton); controls->addWidget(resetButton);
    layout->addLayout(controls);
    runLabel = new QLabel(this); layout->addWidget(runLabel);
    auto *runControls = new QHBoxLayout;
    runStartButton = new QPushButton(tr("Start"), this);
    runPauseButton = new QPushButton(tr("Pause"), this);
    runStopButton = new QPushButton(tr("Stop"), this);
    runResetButton = new QPushButton(tr("Reset"), this);
    runControls->addWidget(runStartButton); runControls->addWidget(runPauseButton);
    runControls->addWidget(runStopButton); runControls->addWidget(runResetButton);
    layout->addLayout(runControls);
    auto *destination = new QPushButton(tr("Choose output JSON…"), this); layout->addWidget(destination);
    outputLabel = new QLabel(this); outputLabel->setWordWrap(true); layout->addWidget(outputLabel);
    statusLabel = new QLabel(this); statusLabel->setWordWrap(true); layout->addWidget(statusLabel);

    animationDialog = new QDialog(parent);
    animationDialog->setWindowTitle(tr("Broadcast Control"));
    auto *animationLayout = new QVBoxLayout(animationDialog);
    auto *triggers = new QHBoxLayout;
    animationOne = new QPushButton(animationDialog); animationTwo = new QPushButton(animationDialog);
    animationThree = new QPushButton(animationDialog); animationFour = new QPushButton(animationDialog);
    triggers->addWidget(animationOne); triggers->addWidget(animationTwo); triggers->addWidget(animationThree); triggers->addWidget(animationFour); animationLayout->addLayout(triggers);
    auto *animationDestination = new QPushButton(tr("Choose shared output JSON…"), animationDialog);
    animationLayout->addWidget(animationDestination);
    animationOutputLabel = new QLabel(animationDialog); animationOutputLabel->setWordWrap(true); animationLayout->addWidget(animationOutputLabel);
    animationStatusLabel = new QLabel(animationDialog); animationStatusLabel->setWordWrap(true); animationLayout->addWidget(animationStatusLabel);
    auto *twitchControls = new TwitchBroadcastDialog(animationDialog);
    twitchControls->setWindowFlags(Qt::Widget);
    animationLayout->addWidget(twitchControls);
    auto *points = new TwitchPointsDialog(animationDialog);
    auto *pointsButton = new QPushButton(tr("Open subscription points…"), animationDialog);
    animationLayout->addWidget(pointsButton);
    connect(pointsButton, &QPushButton::clicked, points, &QDialog::show);
    connect(animationDestination, &QPushButton::clicked, this, [this] { chooseOutput(); });
    ticker = new QTimer(this); ticker->setInterval(250);
    connect(ticker, &QTimer::timeout, this, [this] { tick(); });
    connect(destination, &QPushButton::clicked, this, [this] { chooseOutput(); });
    connect(startButton, &QPushButton::clicked, this, [this] { startOrResume(); });
    connect(pauseButton, &QPushButton::clicked, this, [this] { pause(); });
    connect(stopButton, &QPushButton::clicked, this, [this] { stop(); });
    connect(resetButton, &QPushButton::clicked, this, [this] { reset(); });
    connect(runStartButton, &QPushButton::clicked, this, [this] { startRun(); });
    connect(runPauseButton, &QPushButton::clicked, this, [this] { pauseRun(); });
    connect(runStopButton, &QPushButton::clicked, this, [this] { stopRun(); });
    connect(runResetButton, &QPushButton::clicked, this, [this] { resetRun(); });
    const auto durationChanged = [this](int) { if (state == "idle") reset(); };
    hours->setValue(saved.value("hours", 0).toInt()); minutes->setValue(saved.value("minutes", 0).toInt()); seconds->setValue(saved.value("seconds", 0).toInt());
    const auto saveDuration = [this](int) { PortableSettings settings(QStringLiteral("StreamControlPlus"), QStringLiteral("BroadcastControls")); settings.setValue("hours", hours->value()); settings.setValue("minutes", minutes->value()); settings.setValue("seconds", seconds->value()); };
    connect(hours, qOverload<int>(&QSpinBox::valueChanged), this, saveDuration);
    connect(minutes, qOverload<int>(&QSpinBox::valueChanged), this, saveDuration);
    connect(seconds, qOverload<int>(&QSpinBox::valueChanged), this, saveDuration);
    connect(hours, qOverload<int>(&QSpinBox::valueChanged), this, durationChanged);
    connect(minutes, qOverload<int>(&QSpinBox::valueChanged), this, durationChanged);
    connect(seconds, qOverload<int>(&QSpinBox::valueChanged), this, durationChanged);
    connect(animationOne, &QPushButton::clicked, this, [this] {
        triggerOne = !triggerOne; revisionOne = revisionOne == INT_MAX ? 0 : revisionOne + 1;
        updateDisplay(); publish();
    });
    connect(animationTwo, &QPushButton::clicked, this, [this] {
        triggerTwo = !triggerTwo; revisionTwo = revisionTwo == INT_MAX ? 0 : revisionTwo + 1;
        updateDisplay(); publish();
    });
    connect(animationThree, &QPushButton::clicked, this, [this] { triggerThree = !triggerThree; revisionThree = revisionThree == INT_MAX ? 0 : revisionThree + 1; updateDisplay(); publish(); });
    connect(animationFour, &QPushButton::clicked, this, [this] { triggerFour = !triggerFour; revisionFour = revisionFour == INT_MAX ? 0 : revisionFour + 1; updateDisplay(); publish(); });
    updateDisplay();
}

void BroadcastControl::showAnimations()
{
    animationDialog->show();
    animationDialog->raise();
    animationDialog->activateWindow();
}

void BroadcastControl::setStatus(const QString &message)
{
    statusLabel->setText(message);
    animationStatusLabel->setText(message);
}

qint64 BroadcastControl::currentRemaining() const
{
    if (state != "running") return remainingAtStart;
    return qMax(qint64(0), remainingAtStart - elapsed.elapsed());
}

qint64 BroadcastControl::currentElapsed() const
{
    return runState == "running" ? elapsedBeforeRun + runClock.elapsed() : elapsedBeforeRun;
}

void BroadcastControl::updateTicker()
{
    if (state == "running" || runState == "running") ticker->start();
    else ticker->stop();
}

void BroadcastControl::startRun()
{
    if (runState == "running") return;
    if (runState != "paused") elapsedBeforeRun = 0;
    runClock.start(); runState = QStringLiteral("running");
    updateTicker(); updateDisplay(); publish();
}

void BroadcastControl::pauseRun()
{
    if (runState != "running") return;
    elapsedBeforeRun = currentElapsed(); runState = QStringLiteral("paused");
    updateTicker(); updateDisplay(); publish();
}

void BroadcastControl::stopRun()
{
    if (runState != "running" && runState != "paused") return;
    elapsedBeforeRun = currentElapsed(); runState = QStringLiteral("stopped");
    updateTicker(); updateDisplay(); publish();
}

void BroadcastControl::resetRun()
{
    elapsedBeforeRun = 0; runState = QStringLiteral("idle");
    updateTicker(); updateDisplay(); publish();
}

void BroadcastControl::chooseOutput()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Choose broadcast controls output"),
        outputPath.isEmpty() ? QStringLiteral("broadcast-controls.json") : outputPath,
        tr("JSON files (*.json)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".json", Qt::CaseInsensitive)) path += ".json";
    const QString name = QFileInfo(path).fileName();
    if (name.compare("streamcontrol.json", Qt::CaseInsensitive) == 0 ||
        name.compare("schedule.json", Qt::CaseInsensitive) == 0) {
        setStatus(tr("Choose a separate file from the existing overlay and schedule outputs.")); return;
    }
    outputPath = path;
    PortableSettings settings(QStringLiteral("StreamControlPlus"), QStringLiteral("BroadcastControls"));
    settings.setValue("outputPath", outputPath);
    updateDisplay(); publish();
}

void BroadcastControl::startOrResume()
{
    if (state == "running") return;
    if (state != "paused") {
        remainingAtStart = (qint64(hours->value()) * 3600 + minutes->value() * 60 + seconds->value()) * 1000;
        if (remainingAtStart <= 0) { setStatus(tr("Enter a duration greater than zero.")); return; }
    }
    elapsed.start(); state = QStringLiteral("running"); updateTicker();
    updateDisplay(); publish();
}

void BroadcastControl::pause()
{
    if (state != "running") return;
    remainingAtStart = currentRemaining();
    state = remainingAtStart == 0 ? QStringLiteral("done") : QStringLiteral("paused");
    updateTicker();
    updateDisplay(); publish();
}

void BroadcastControl::stop()
{
    if (state != "running" && state != "paused") return;
    remainingAtStart = currentRemaining(); state = QStringLiteral("stopped"); updateTicker();
    updateDisplay(); publish();
}

void BroadcastControl::reset()
{
    state = QStringLiteral("idle"); updateTicker();
    remainingAtStart = (qint64(hours->value()) * 3600 + minutes->value() * 60 + seconds->value()) * 1000;
    updateDisplay(); publish();
}

void BroadcastControl::tick()
{
    if (state == "running" && currentRemaining() == 0) { remainingAtStart = 0; state = QStringLiteral("done"); updateTicker(); }
    updateDisplay(); publish();
}

void BroadcastControl::updateDisplay()
{
    const qint64 remaining = currentRemaining();
    const qint64 totalSeconds = (remaining + 999) / 1000;
    timerLabel->setText(tr("Countdown: %1:%2:%3  (%4)")
        .arg(totalSeconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((totalSeconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0')).arg(state));
    const qint64 runSeconds = currentElapsed() / 1000;
    runLabel->setText(tr("Run: %1:%2:%3  (%4)")
        .arg(runSeconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((runSeconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(runSeconds % 60, 2, 10, QLatin1Char('0')).arg(runState));
    runStartButton->setText(runState == "paused" ? tr("Resume") : tr("Start"));
    runStartButton->setEnabled(runState != "running");
    runPauseButton->setEnabled(runState == "running");
    runStopButton->setEnabled(runState == "running" || runState == "paused");
    animationOne->setText(tr("Toggle casters: %1").arg(triggerOne ? "ON" : "OFF"));
    animationTwo->setText(tr("Toggle talent: %1").arg(triggerTwo ? "ON" : "OFF"));
    animationThree->setText(tr("Toggle lower 3rd: %1").arg(triggerThree ? "ON" : "OFF"));
    animationFour->setText(tr("Toggle score: %1").arg(triggerFour ? "ON" : "OFF"));
    startButton->setText(state == "paused" ? tr("Resume") : tr("Start"));
    startButton->setEnabled(state != "running");
    pauseButton->setEnabled(state == "running");
    stopButton->setEnabled(state == "running" || state == "paused");
    const bool editable = state != "running" && state != "paused";
    hours->setEnabled(editable); minutes->setEnabled(editable); seconds->setEnabled(editable);
    outputLabel->setText(outputPath.isEmpty() ? tr("No output file selected") : tr("Output: %1").arg(outputPath));
    animationOutputLabel->setText(outputLabel->text());
}

void BroadcastControl::publish()
{
    if (outputPath.isEmpty()) { setStatus(tr("Choose an output file to publish controls.")); return; }
    QJsonObject timer;
    timer.insert("state", state);
    timer.insert("remaining_ms", double(currentRemaining()));
    timer.insert("updated_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    QJsonObject runTimer;
    runTimer.insert("state", runState);
    runTimer.insert("elapsed_ms", double(currentElapsed()));
    runTimer.insert("updated_at", timer.value("updated_at"));
    QJsonObject first; first.insert("value", triggerOne); first.insert("revision", revisionOne);
    QJsonObject second; second.insert("value", triggerTwo); second.insert("revision", revisionTwo);
    QJsonObject third; third.insert("value", triggerThree); third.insert("revision", revisionThree);
    QJsonObject fourth; fourth.insert("value", triggerFour); fourth.insert("revision", revisionFour);
    QJsonObject animations; animations.insert("animation_1", first); animations.insert("animation_2", second); animations.insert("animation_3", third); animations.insert("animation_4", fourth);
    QJsonObject root; root.insert("version", 1); root.insert("timer", timer); root.insert("run_timer", runTimer); root.insert("animations", animations);
    QSaveFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly)) { setStatus(tr("Output failed: %1").arg(file.errorString())); return; }
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (file.write(data) != data.size()) { setStatus(tr("Output failed: %1").arg(file.errorString())); file.cancelWriting(); return; }
    if (!file.commit()) { setStatus(tr("Output failed: %1").arg(file.errorString())); return; }
    setStatus(tr("Controls published."));
}

QJsonObject BroadcastControl::remoteState() const
{
    QJsonObject result;
    result.insert("countdownState", state);
    result.insert("remainingMs", double(currentRemaining()));
    result.insert("runState", runState);
    result.insert("elapsedMs", double(currentElapsed()));
    result.insert("hours", hours->value()); result.insert("minutes", minutes->value()); result.insert("seconds", seconds->value());
    result.insert("hasOutput", !outputPath.isEmpty()); result.insert("status", statusLabel->text());
    return result;
}

QString BroadcastControl::remoteAction(const QString &action, const QJsonObject &args)
{
    if (action == "duration") {
        if (state == "running" || state == "paused") return tr("Stop or reset the countdown before changing its duration.");
        const QJsonValue h = args.value("hours"), m = args.value("minutes"), s = args.value("seconds");
        if (!h.isDouble() || !m.isDouble() || !s.isDouble() || h.toDouble() != h.toInt(-1) || m.toDouble() != m.toInt(-1) || s.toDouble() != s.toInt(-1) || h.toInt() < 0 || h.toInt() > 999 || m.toInt() < 0 || m.toInt() > 59 || s.toInt() < 0 || s.toInt() > 59) return tr("Duration must be 0–999 hours and 0–59 minutes and seconds.");
        hours->setValue(h.toInt()); minutes->setValue(m.toInt()); seconds->setValue(s.toInt());
        if (state != "idle") reset();
    } else if (action == "countdown_start") {
        if (state != "paused" && hours->value() == 0 && minutes->value() == 0 && seconds->value() == 0) return tr("Enter a duration greater than zero.");
        startOrResume();
    } else if (action == "countdown_pause") pause();
    else if (action == "countdown_stop") stop();
    else if (action == "countdown_reset") reset();
    else if (action == "run_start") startRun();
    else if (action == "run_pause") pauseRun();
    else if (action == "run_stop") stopRun();
    else if (action == "run_reset") resetRun();
    else return tr("Unknown timer action.");
    if (outputPath.isEmpty()) return tr("Timer changed; choose the timer output file in StreamControl to publish.");
    if (statusLabel->text().startsWith(tr("Output failed:"))) return statusLabel->text();
    return QString();
}
