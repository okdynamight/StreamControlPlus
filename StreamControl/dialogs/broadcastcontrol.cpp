#include "broadcastcontrol.h"

#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGlobal>
#include <climits>

BroadcastControl::BroadcastControl(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Broadcast timers and animations"));
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
    auto *triggers = new QHBoxLayout;
    animationOne = new QPushButton(this); animationTwo = new QPushButton(this);
    triggers->addWidget(animationOne); triggers->addWidget(animationTwo); layout->addLayout(triggers);
    auto *destination = new QPushButton(tr("Choose output JSON…"), this); layout->addWidget(destination);
    outputLabel = new QLabel(this); outputLabel->setWordWrap(true); layout->addWidget(outputLabel);
    statusLabel = new QLabel(this); statusLabel->setWordWrap(true); layout->addWidget(statusLabel);
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
    updateDisplay();
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
        statusLabel->setText(tr("Choose a separate file from the existing overlay and schedule outputs.")); return;
    }
    outputPath = path;
    updateDisplay(); publish();
}

void BroadcastControl::startOrResume()
{
    if (state == "running") return;
    if (state != "paused") {
        remainingAtStart = (qint64(hours->value()) * 3600 + minutes->value() * 60 + seconds->value()) * 1000;
        if (remainingAtStart <= 0) { statusLabel->setText(tr("Enter a duration greater than zero.")); return; }
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
    animationOne->setText(tr("Animation 1: %1").arg(triggerOne ? "ON" : "OFF"));
    animationTwo->setText(tr("Animation 2: %1").arg(triggerTwo ? "ON" : "OFF"));
    startButton->setText(state == "paused" ? tr("Resume") : tr("Start"));
    startButton->setEnabled(state != "running");
    pauseButton->setEnabled(state == "running");
    stopButton->setEnabled(state == "running" || state == "paused");
    const bool editable = state != "running" && state != "paused";
    hours->setEnabled(editable); minutes->setEnabled(editable); seconds->setEnabled(editable);
    outputLabel->setText(outputPath.isEmpty() ? tr("No output file selected") : tr("Output: %1").arg(outputPath));
}

void BroadcastControl::publish()
{
    if (outputPath.isEmpty()) { statusLabel->setText(tr("Choose an output file to publish controls.")); return; }
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
    QJsonObject animations; animations.insert("animation_1", first); animations.insert("animation_2", second);
    QJsonObject root; root.insert("version", 1); root.insert("timer", timer); root.insert("run_timer", runTimer); root.insert("animations", animations);
    QSaveFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly)) { statusLabel->setText(tr("Output failed: %1").arg(file.errorString())); return; }
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (file.write(data) != data.size()) { statusLabel->setText(tr("Output failed: %1").arg(file.errorString())); file.cancelWriting(); return; }
    if (!file.commit()) { statusLabel->setText(tr("Output failed: %1").arg(file.errorString())); return; }
    statusLabel->setText(tr("Controls published."));
}
