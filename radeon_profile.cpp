// copyright marazmista 3.2013

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

//==================
// the logic is simple. radeon-profile create object based on gpu class.
// gpu class decides what driver is used (device.initialize()). Later, based
// on structure driverFeatures, program activate some ui features.

#include "radeon_profile.h"
#include "ui_radeon_profile.h"
#include "dialogs/dialog_defineplot.h"
#include "components/topbarcomponents.h"

#include <QTimer>
#include <QTextStream>
#include <QMenu>
#include <QDir>
#include <QDateTime>
#include <QMessageBox>
#include <QDebug>
#include <QStandardPaths>
#include <QStatusBar>
#include <cmath>
#include <unistd.h>

DaemonComm radeon_profile::dcomm;

radeon_profile::radeon_profile(QWidget *parent) :
    QMainWindow(parent),
    icon_tray(nullptr),
    refreshWhenHidden(new QAction(icon_tray)),
    timer(new QTimer(this)),
    counter_ticks(0),
    counter_statsTick(0),
    lastFanTemperature(-1),
    enableChangeEvent(false),
    savedState(nullptr),
    ui(new Ui::radeon_profile)
{
    ui->setupUi(this);

    connect(dcomm.getSocketPtr(), SIGNAL(connected()), this, SLOT(daemonConnected()));
    connect(dcomm.getSocketPtr(), SIGNAL(disconnected()), this, SLOT(daemonDisconnected()));
    connect(dcomm.getSocketPtr(), &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        if (!device.isInitialized())
            initializeDevice(); // Monitoring does not require the privileged daemon.
    });

    loadConfig();
    setupUiElements();

    // checks if running as root
    if (geteuid() == 0) {
        rootMode = true;
        ui->label_rootWarrning->setVisible(true);

        initializeDevice();
    } else {
        rootMode = false;
        ui->label_rootWarrning->setVisible(false);

        dcomm.connectToDaemon();
    }

    showWindow();
}

radeon_profile::~radeon_profile()
{
    dcomm.disconnectDaemon();
    delete ui;
}

void radeon_profile::initializeDevice() {
    if (!device.initialize(dXorg::InitializationConfig(rootMode, ui->cb_daemonData->isChecked(), ui->cb_daemonAutoRefresh->isChecked()))) {
        QMessageBox::critical(this,tr("Error"), tr("No Radeon cards have been found in the system."));

        for (int i = 0; i < ui->tw_main->count() - 1; ++i)
            ui->tw_main->setTabEnabled(i, false);

        return;
    }

    setupDeviceDependantUiElements();
    setupUiEnabledFeatures(device.getDriverFeatures(), device.gpuData);
    refreshUI();
    connectSignals();
    createPlots();

    timer->start();
}

void radeon_profile::enablePrivilegedControls() {
    if (rootMode || dcomm.isConnected())
        return;

    const QString pkexec = QStandardPaths::findExecutable("pkexec");
    const QStringList command = globalStuff::daemonServiceCommand();
    if (pkexec.isEmpty() || command.isEmpty()) {
        QMessageBox::warning(this, tr("Privileged controls"),
            tr("Install pkexec and the radeon-profile-daemon service for your init system (OpenRC or systemd)."));
        return;
    }

    auto *process = new QProcess(this);
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            QMessageBox::warning(this, tr("Privileged controls"), process->errorString());
            process->deleteLater();
        }
    });
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        if (status == QProcess::NormalExit && code == 0)
            dcomm.connectToDaemon();
        else
            QMessageBox::warning(this, tr("Privileged controls"),
                tr("Authentication was cancelled or the daemon could not be started.\n")
                + QString::fromLocal8Bit(process->readAllStandardError()));
        process->deleteLater();
    });
    process->start(pkexec, command);
}

void radeon_profile::daemonConnected() {
    qDebug() << "Daemon connected";

    if (!device.isInitialized()) {

        configureDaemonPreDeviceInit();
        initializeDevice();
        configureDaemonPostDeviceInit();

    } else {

        if (!device.getDriverFeatures().isChangeProfileAvailable) {
            configureDaemonPreDeviceInit();
            gpuChanged();
            configureDaemonPostDeviceInit();
        }
        enableUiControls(true);
        restoreFanState();
    }
}

void radeon_profile::daemonDisconnected() {
    qDebug() << "Daemon disconnected";

    enableUiControls(false);
}

void radeon_profile::configureDaemonPreDeviceInit() {
    QString command;

    if (ui->cb_daemonData->isChecked() && ui->cb_daemonAutoRefresh->isChecked()) {
        command.append(DAEMON_SIGNAL_TIMER_ON).append(SEPARATOR);
        command.append(QString::number(ui->spin_timerInterval->value())).append(SEPARATOR);
    } else
        command.append(DAEMON_SIGNAL_TIMER_OFF).append(SEPARATOR);

    if (ui->combo_connConfirmMethod->currentIndex() == 0)
        command.append(DAEMON_ALIVE).append(SEPARATOR).append('0').append(SEPARATOR);

    dcomm.sendCommand(command);
}

void radeon_profile::configureDaemonPostDeviceInit() {
    QString command;

    if (device.getDriverFeatures().isFanControlAvailable)
         command.append(DAEMON_SIGNAL_CONFIG).append(SEPARATOR).append("pwm1_enable").append(SEPARATOR).append(device.getDriverFiles().hwmonAttributes.pwm1_enable).append(SEPARATOR);

    dcomm.sendCommand(command);
}

void radeon_profile::connectSignals()
{
    // fix for warrning: QMetaObject::connectSlotsByName: No matching signal for...
    connect(ui->combo_gpus, &QComboBox::currentIndexChanged, this, &radeon_profile::gpuChanged);
    connect(ui->combo_pLevel,SIGNAL(currentIndexChanged(int)),this,SLOT(setPowerLevelFromCombo()));
    connect(timer,SIGNAL(timeout()),this,SLOT(mainTimerEvent()));
    connect(ui->combo_fanProfiles, &QComboBox::currentTextChanged, this, &radeon_profile::createFanProfileListaAndGraph);
    connect(ui->combo_ocProfiles, &QComboBox::currentTextChanged, this, &radeon_profile::createOcProfileListsAndGraph);
    connect(ui->slider_powerCap, SIGNAL(valueChanged(int)), this, SLOT(powerCapValueChange(int)));
    connect(ui->spin_powerCap, SIGNAL(valueChanged(int)), this, SLOT(powerCapValueChange(int)));
    connect(ui->group_oc, SIGNAL(toggled(bool)), this, SLOT(percentOverclockToggled(bool)));
    connect(ui->group_freq, SIGNAL(toggled(bool)), this, SLOT(frequencyControlToggled(bool)));
    connect(&group_profileControlButtons, &QButtonGroup::idClicked, this, &radeon_profile::setPowerProfile);
}

void radeon_profile::setupDeviceDependantUiElements()
{
    if (topbarManager.schemas.count() == 0)
        topbarManager.createDefaultTopbarSchema(device.gpuData.keys());

    topbarManager.createTopbar(ui->topbar_layout);

    for (int i = 0; i < device.gpuList.count(); ++i)
        ui->combo_gpus->addItem(device.gpuList.at(i).sysName);

    ui->list_glxinfo->addItems(device.getGLXInfo(ui->combo_gpus->currentText()));

    fillModInfo();
}

void radeon_profile::setupUiElements()
{
    qDebug() << "Creating ui elements";

    ui->tw_main->setCurrentIndex(0);
    ui->tw_systemInfo->setCurrentIndex(0);
    ui->list_currentGPUData->setHeaderHidden(false);
    ui->execPages->setCurrentIndex(0);
    ui->btn_general->setMenu(createGeneralMenu());
    enableUiControls(false);

    setupTrayIcon();
    addRuntmeWidgets();
    fillConnectors();
}


void radeon_profile::setupUiEnabledFeatures(const DriverFeatures &features, const GPUDataContainer &data) {
    qDebug() << "Handling found device features";

    if (features.isChangeProfileAvailable && features.currentPowerMethod < PowerMethod::PM_UNKNOWN) {
        ui->widget_pmControls->setEnabled(true);

        createPowerProfileControlButtons(features.powerProfiles);

        if (features.currentPowerMethod != PowerMethod::PROFILE)
            ui->combo_pLevel->addItems(globalStuff::createPowerLevelCombo(features.sysInfo.module));
        else
            ui->combo_pLevel->setVisible(false);

    } else {
        ui->cb_eventsTracking->setEnabled(false);
        ui->cb_eventsTracking->setChecked(false);
    }

    ui->tw_systemInfo->setTabEnabled(3,data.contains(ValueID::CLK_CORE));

    if (!device.gpuData.contains(ValueID::CLK_CORE) && !data.contains(ValueID::TEMPERATURE_CURRENT) && !device.gpuData.contains(ValueID::VOLT_CORE))
        ui->tw_main->setTabEnabled(1,false);

    ui->group_cfgDaemon->setEnabled(dcomm.isConnected());
    if (!features.isChangeProfileAvailable)
        statusBar()->showMessage(tr("Monitoring only. Use General → Enable privileged controls… to authenticate for fan and power control."));
    else
        statusBar()->clearMessage();
    createCurrentGpuDataListItems();

    ui->cb_hotspotFanControl->setEnabled(data.contains(ValueID::TEMPERATURE_HOTSPOT));

    // SETUP FAN CONTROL
    if (features.isFanControlAvailable && features.isChangeProfileAvailable) {
        qDebug() << "Fan control available";
        ui->tw_main->setTabEnabled(3,true);
        ui->l_fanProfileUnsavedIndicator->setVisible(false);

        // set pwm buttons in group
        group_pwm.addButton(ui->btn_pwmAuto);
        group_pwm.addButton(ui->btn_pwmFixed);
        group_pwm.addButton(ui->btn_pwmProfile);

        //setup fan profile graph
        createFanProfileGraph();

        for (const QString &fpName : fanProfiles.keys())
            ui->combo_fanProfiles->addItem(fpName);

        createFanProfileListaAndGraph(ui->combo_fanProfiles->currentText());

        createFanProfilesMenu();

        if (ui->cb_saveFanMode->isChecked()) {
            switch (ui->stack_fanModes->currentIndex()) {
                case 1:
                    on_btn_pwmFixed_clicked();
                    break;
                case 2:
                    device.getTemperature();
                    on_btn_pwmProfile_clicked();
                    break;
            }
        }
    } else {
        qDebug() << "Fan control not available";
        ui->tw_main->setTabEnabled(3,false);
        ui->btn_fanControl->setVisible(false);
        ui->group_cfgFan->setEnabled(false);
    }

    // SETUP OC
    ui->tw_main->setTabEnabled(2, false);
    ui->btn_ocProfileControl->setVisible(false);

    if (features.isChangeProfileAvailable && (features.isOcTableAvailable || features.isPercentCoreOcAvailable || features.isDpmCoreFreqTableAvailable)) {
        ui->tw_main->setTabEnabled(2, true);
        ui->tw_overclock->setEnabled(true);
        ui->tw_overclock->setTabEnabled(0, false);
        ui->tw_overclock->setTabEnabled(1, false);

        if (features.isPercentCoreOcAvailable) {
            ui->group_oc->setEnabled(true);
            ui->tw_overclock->setTabEnabled(0, true);
            ui->btn_applyStatesAndOc->setEnabled(true);

            if (ui->cb_restorePercentOc->isChecked())
                on_btn_applyStatesAndOc_clicked();
        }

        if (features.isDpmCoreFreqTableAvailable) {
            ui->group_freq->setEnabled(true);
            ui->tw_overclock->setTabEnabled(0, true);
            ui->btn_applyStatesAndOc->setEnabled(true);

            loadFrequencyStatesTables();

            if (ui->cb_restoreFrequencyStates->isChecked())
                on_btn_applyStatesAndOc_clicked();
        }

        if (features.isOcTableAvailable) {
            ui->tw_overclock->setTabEnabled(1, true);
            ui->btn_ocProfileControl->setVisible(true);
            createOcProfileGraph();

            if (features.isPowerCapAvailable) {
                ui->group_powerCap->setEnabled(true);

                ui->slider_powerCap->setRange(device.getGpuConstParams().power1_cap_min, device.getGpuConstParams().power1_cap_max);
                ui->spin_powerCap->setRange(device.getGpuConstParams().power1_cap_min, device.getGpuConstParams().power1_cap_max);
                ui->slider_powerCap->setValue(device.gpuData[ValueID::POWER_CAP_SELECTED].value);
            }

            if (ocProfiles.isEmpty())
                loadDefaultOcTables(features);

            else if ((features.isVDDCCurveAvailable && !ocProfiles.first().tables.keys().contains(OD_VDDC_CURVE))
                     || (!features.isVDDCCurveAvailable && ocProfiles.first().tables.keys().contains(OD_VDDC_CURVE))) {

                // if the profiles in config are incompatible with system (user changed card etc), clear and load default
                ocProfiles.clear();
                loadDefaultOcTables(features);
            }

            for (const auto &k : ocProfiles.keys())
                ui->combo_ocProfiles->addItem(k);

            createOcProfileListsAndGraph(ui->combo_ocProfiles->currentText());
            createOcProfilesMenu(true);
            ui->btn_ocProfileControl->menu()->actions()[findCurrentMenuIndex(ui->btn_ocProfileControl->menu(), "default")]->setChecked(true);

            ui->tw_overclock->setTabEnabled(1, true);
            ui->btn_applyStatesAndOc->setEnabled(true);

            if (features.isVDDCCurveAvailable) {
                ui->l_ocTableTitle->setText(tr("VDDC curve"));
                ui->btn_setOcRanges->setVisible(true);

                ui->list_memStates->setVisible(false);
                ui->l_memOcTableTitle->setVisible(false);
            } else {
                ui->btn_setOcRanges->setVisible(false);
            }

            if (ui->cb_restoreOcProfile->isChecked())
                setCurrentOcProfile(ui->l_currentOcProfile->text());
        }
    }
}

void radeon_profile::refreshGpuData() {
    device.getPowerLevel();
    device.getClocks();
    device.getTemperature();
    device.getGpuUsage();
    device.getFanSpeed();
    device.getPowerCapSelected();
    device.getPowerCapAverage();
}

void radeon_profile::addTreeWidgetItem(QTreeWidget * parent, const QString &leftColumn, const QString  &rightColumn) {
    parent->addTopLevelItem(new QTreeWidgetItem(QStringList() << leftColumn << rightColumn));
}

QString radeon_profile::createCurrentMinMaxString(const QString &current, const QString &min,  const QString &max) {
    return QString(current + " (min: " + min + " max: " + max + ")");
}

QString radeon_profile::createCurrentMinMaxString(const ValueID idCurrent, const ValueID idMin, const ValueID idMax) {
    return createCurrentMinMaxString(device.gpuData.value(idCurrent).strValue, device.gpuData.value(idMin).strValue, device.gpuData.value(idMax).strValue);
}

void radeon_profile::refreshUI() {
    // refresh top bar
    topbarManager.updateItems(device.gpuData);

    // GPU data list
    if (ui->tw_main->currentIndex() == 0) {
        for (int i = 0; i < ui->list_currentGPUData->topLevelItemCount(); ++i) {
            switch (keysInCurrentGpuList.value(i)) {
                case ValueID::TEMPERATURE_CURRENT:
                    ui->list_currentGPUData->topLevelItem(i)->setText(1, createCurrentMinMaxString(ValueID::TEMPERATURE_CURRENT, ValueID::TEMPERATURE_MIN, ValueID::TEMPERATURE_MAX));
                    continue;
                case ValueID::POWER_CAP_SELECTED:
                    ui->list_currentGPUData->topLevelItem(i)->setText(1, createCurrentMinMaxString(device.gpuData.value(ValueID::POWER_CAP_SELECTED).strValue,
                                                                                                   QString::number(device.getGpuConstParams().power1_cap_min),
                                                                                                   QString::number(device.getGpuConstParams().power1_cap_max)));
                    continue;

                // ignored values
                case ValueID::TEMPERATURE_BEFORE_CURRENT:
                case ValueID::TEMPERATURE_MIN:
                case ValueID::TEMPERATURE_MAX:
                    continue;

                default:
                    ui->list_currentGPUData->topLevelItem(i)->setText(1, device.gpuData.value(keysInCurrentGpuList.at(i)).strValue);
            }
        }
    }

    if (group_profileControlButtons.checkedButton() != nullptr) {
        switch (device.getDriverFeatures().currentPowerMethod) {
            case PowerMethod::DPM:
            case PowerMethod::PROFILE:
                if (device.currentPowerProfile != group_profileControlButtons.checkedButton()->text()) {
                    for (auto &mode :device.getDriverFeatures().powerProfiles) {
                        if (mode.name == device.currentPowerProfile) {
                            group_profileControlButtons.button(mode.id)->setChecked(true);
                            break;
                        }
                    }
                }
                break;
            case PowerMethod::PP_MODE:
                if (group_profileControlButtons.checkedId() != device.currentPowerProfile.toInt())
                    group_profileControlButtons.button(device.currentPowerProfile.toInt())->setChecked(true);
                break;
            default:
                break;
        }
    }

    ui->combo_pLevel->setCurrentText(device.currentPowerLevel);

    // do the math only when user looking at stats table
    if (ui->tw_systemInfo->currentIndex() == 3 && ui->tw_main->currentIndex() == 0)
        updateStatsTable();
}

void radeon_profile::createCurrentGpuDataListItems()
{
    ui->list_currentGPUData->clear();
    for (int i = 0; i < device.gpuData.keys().count(); ++i) {

        switch (device.gpuData.keys().at(i)) {

            // ignored values
            case ValueID::TEMPERATURE_BEFORE_CURRENT:
            case ValueID::TEMPERATURE_MIN:
            case ValueID::TEMPERATURE_MAX:
                continue;

            default:
                addTreeWidgetItem(ui->list_currentGPUData, globalStuff::getNameOfValueID(device.gpuData.keys().at(i)), "");
                keysInCurrentGpuList.append(device.gpuData.keys().at(i));
        }
    }
}

void radeon_profile::updateExecLogs() {
    for (int i = 0; i < execsRunning.count(); i++) {
        if (execsRunning.at(i)->getExecState() == QProcess::Running && execsRunning.at(i)->logEnabled) {
            QString logData = QDateTime::currentDateTime().toString(logDateFormat) +";" + device.gpuData.value(ValueID::POWER_LEVEL, RPValue()).strValue + ";" +
                    device.gpuData.value(ValueID::CLK_CORE).strValue + ";"+
                    device.gpuData.value(ValueID::CLK_MEM).strValue + ";"+
                    device.gpuData.value(ValueID::CLK_UVD).strValue + ";"+
                    device.gpuData.value(ValueID::DCLK_UVD).strValue + ";"+
                    device.gpuData.value(ValueID::VOLT_CORE).strValue + ";"+
                    device.gpuData.value(ValueID::VOLT_MEM).strValue + ";"+
                    device.gpuData.value(ValueID::TEMPERATURE_CURRENT).strValue;
            execsRunning.at(i)->appendToLog(logData);
        }
    }
}

void radeon_profile::enableUiControls(bool enable)
{
    const bool writable = enable && device.isInitialized() && device.getDriverFeatures().isChangeProfileAvailable;
    ui->tw_main->setTabEnabled(2, writable && (device.getDriverFeatures().isOcTableAvailable
        || device.getDriverFeatures().isPercentCoreOcAvailable || device.getDriverFeatures().isDpmCoreFreqTableAvailable));
    ui->tw_main->setTabEnabled(3, writable && device.getDriverFeatures().isFanControlAvailable);
    ui->widget_pmControls->setEnabled(writable);
}

void radeon_profile::mainTimerEvent() {

    // retry connection if lost and not root
    if (!rootMode && !dcomm.isConnected())
        dcomm.connectToDaemon();

    if (!refreshWhenHidden->isChecked() && this->isHidden()) {

        // even if in tray, keep the fan control active (if enabled)
        if (device.gpuData.contains(ValueID::FAN_SPEED_PERCENT) && device.getDriverFeatures().isChangeProfileAvailable && ui->btn_pwmProfile->isChecked()) {
            device.getTemperature();
            adjustFanSpeed();
        }
        return;
    }

    refreshGpuData();

    if (device.gpuData.contains(ValueID::FAN_SPEED_PERCENT) && device.getDriverFeatures().isChangeProfileAvailable && ui->btn_pwmProfile->isChecked())
        adjustFanSpeed();

    if (Q_LIKELY(ui->cb_graphs->isChecked()))
        refreshGraphs();

    if (ui->cb_eventsTracking->isChecked())
        checkEvents();

    // lets say coreClk is essential to get stats (it is disabled in ui anyway when features.clocksAvailable is false)
    if (ui->cb_stats->isChecked() && device.gpuData.contains(ValueID::CLK_CORE))
        doTheStats();

    // don't refresh ui dynamic stuff when min or hidden
    if (!isMinimized() && !isHidden())
        refreshUI();

    refreshTooltip();

    if (Q_UNLIKELY(execsRunning.count() > 0))
        updateExecLogs();
}

void radeon_profile::adjustFanSpeed() {
    if (!ui->btn_pwmProfile->isChecked() || !device.getDriverFeatures().isChangeProfileAvailable)
        return;
    const auto sensor = currentFanProfileHotspot ? ValueID::TEMPERATURE_HOTSPOT : ValueID::TEMPERATURE_CURRENT;
    const float temperature = device.gpuData.value(sensor).value;
    if (temperature < 0 || !std::isfinite(temperature) || currentFanProfile.isEmpty()) {
        on_btn_pwmAuto_clicked();
        statusBar()->showMessage(tr("Fan curve sensor unavailable. Restored automatic fan control."));
        return;
    }
    if (temperature == lastFanTemperature || (temperature < lastFanTemperature
            && lastFanTemperature - temperature < ui->spin_hysteresis->value()))
        return;

    lastFanTemperature = temperature;
    device.setPwmValue(fanSpeedAtTemperature(currentFanProfile, temperature));
}

void radeon_profile::restoreFanState() {

    // find fan state from before distconnect and restore it
    for (const auto &b : group_pwm.buttons()) {
        if (b->isChecked()) {
            b->click();
            return;
        }
    }
}

void radeon_profile::refreshGraphs() {
    plotManager.updateSeries(++counter_ticks, device.gpuData);
}

void radeon_profile::doTheStats() {
    // count ticks for stats //
    counter_statsTick++;

    // figure out pm level based on data provided
    QString pmLevelName = "Core: " + device.gpuData.value(ValueID::CLK_CORE).strValue + "  Mem: " + device.gpuData.value(ValueID::CLK_MEM).strValue;

    if (pmStats.contains(pmLevelName))
        pmStats[pmLevelName]++;
    else {
        pmStats.insert(pmLevelName, 1);
        ui->list_stats->addTopLevelItem(new QTreeWidgetItem(QStringList() << pmLevelName));
        ui->list_stats->header()->resizeSections(QHeaderView::ResizeToContents);
    }
}

void radeon_profile::updateStatsTable() {
    // do the math with percents
    for (QTreeWidgetItemIterator item(ui->list_stats); *item; item++)
        (*item)->setText(1,QString::number(pmStats.value((*item)->text(0)) * 100.0 / counter_statsTick) + "%");
}

void radeon_profile::refreshTooltip()
{
    QString tooltipData = radeon_profile::windowTitle() + "\n" + tr("Current profile: ")+ device.currentPowerProfile + "  " + device.currentPowerLevel +"\n";

    for (auto i = 0; i < ui->list_currentGPUData->topLevelItemCount(); i++)
        tooltipData += ui->list_currentGPUData->topLevelItem(i)->text(0) + ": " + device.gpuData.value(keysInCurrentGpuList.at(i)).strValue + '\n';

    icon_tray->setToolTip(tooltipData.trimmed());
}

bool radeon_profile::askConfirmation(const QString title, const QString question){
    return QMessageBox::Yes ==
            QMessageBox::question(this, title, question, QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
}

void radeon_profile::showWindow() {
    if (ui->cb_minimizeTray->isChecked() && ui->cb_startMinimized->isChecked())
        return;

    // disable changeEvent() for some time to properly configure window state at start
    QTimer::singleShot(200, this, SLOT(setEnabledChangeEvent()));

    if (ui->cb_startMinimized->isChecked())
        this->showMinimized();
    else
        this->showNormal();
}
