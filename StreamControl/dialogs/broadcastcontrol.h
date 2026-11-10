#ifndef BROADCASTCONTROL_H
#define BROADCASTCONTROL_H

#include <QDialog>
#include <QElapsedTimer>
#include <QString>

class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;

// Independent local countdown and stateful animation triggers.
class BroadcastControl : public QDialog
{
public:
    explicit BroadcastControl(QWidget *parent = nullptr);

private:
    qint64 currentRemaining() const;
    qint64 currentElapsed() const;
    void updateTicker();
    void startRun();
    void pauseRun();
    void stopRun();
    void resetRun();
    void chooseOutput();
    void startOrResume();
    void pause();
    void stop();
    void reset();
    void tick();
    void publish();
    void updateDisplay();

    QSpinBox *hours;
    QSpinBox *minutes;
    QSpinBox *seconds;
    QPushButton *startButton;
    QPushButton *pauseButton;
    QPushButton *stopButton;
    QPushButton *resetButton;
    QPushButton *runStartButton;
    QPushButton *runPauseButton;
    QPushButton *runStopButton;
    QPushButton *runResetButton;
    QLabel *runLabel;
    QPushButton *animationOne;
    QPushButton *animationTwo;
    QLabel *timerLabel;
    QLabel *statusLabel;
    QLabel *outputLabel;
    QTimer *ticker;
    QElapsedTimer elapsed;
    QElapsedTimer runClock;
    qint64 elapsedBeforeRun = 0;
    QString runState = QStringLiteral("idle");
    qint64 remainingAtStart = 0;
    QString state = QStringLiteral("idle");
    QString outputPath;
    bool triggerOne = false;
    bool triggerTwo = false;
    int revisionOne = 0;
    int revisionTwo = 0;
};

#endif
