///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016 Edouard Griffiths, F4EXB                                   //
//                                                                               //
// This program is free software; you can redistribute it and/or modify          //
// it under the terms of the GNU General Public License as published by          //
// the Free Software Foundation as version 3 of the License, or                  //
//                                                                               //
// This program is distributed in the hope that it will be useful,               //
// but WITHOUT ANY WARRANTY; without even the implied warranty of                //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                  //
// GNU General Public License V3 for more details.                               //
//                                                                               //
// You should have received a copy of the GNU General Public License             //
// along with this program. If not, see <http://www.gnu.org/licenses/>.          //
///////////////////////////////////////////////////////////////////////////////////

#include <QDockWidget>
#include <QMainWindow>
#include <QFileDialog>
#include <QTime>
#include <QDebug>

#include "ssbmodgui.h"

#include "device/devicesinkapi.h"
#include "dsp/upchannelizer.h"
#include "dsp/spectrumvis.h"
#include "dsp/threadedbasebandsamplesource.h"
#include "ui_ssbmodgui.h"
#include "plugin/pluginapi.h"
#include "util/simpleserializer.h"
#include "util/db.h"
#include "gui/basicchannelsettingswidget.h"
#include "dsp/dspengine.h"
#include "mainwindow.h"

const QString SSBModGUI::m_channelID = "sdrangel.channeltx.modssb";

const int SSBModGUI::m_agcTimeConstant[] = {
        1,
        2,
        5,
       10,
       20,
       50,
      100,
      200,
      500,
      990};

SSBModGUI* SSBModGUI::create(PluginAPI* pluginAPI, DeviceSinkAPI *deviceAPI)
{
    SSBModGUI* gui = new SSBModGUI(pluginAPI, deviceAPI);
	return gui;
}

void SSBModGUI::destroy()
{
}

void SSBModGUI::setName(const QString& name)
{
	setObjectName(name);
}

QString SSBModGUI::getName() const
{
	return objectName();
}

qint64 SSBModGUI::getCenterFrequency() const {
	return m_channelMarker.getCenterFrequency();
}

void SSBModGUI::setCenterFrequency(qint64 centerFrequency)
{
	m_channelMarker.setCenterFrequency(centerFrequency);
	applySettings();
}

void SSBModGUI::resetToDefaults()
{
	blockApplySettings(true);

    ui->BW->setValue(30);
    ui->lowCut->setValue(3);
    ui->spanLog2->setValue(m_settings.m_spanLog2);
	ui->toneFrequency->setValue(100);
	ui->deltaFrequency->setValue(0);
	ui->audioBinaural->setChecked(false);
	ui->audioFlipChannels->setChecked(false);
	ui->dsb->setChecked(false);
	ui->audioMute->setChecked(false);

    ui->play->setEnabled(false);
    ui->play->setChecked(false);
    ui->tone->setChecked(false);
    ui->morseKeyer->setChecked(false);
    ui->mic->setChecked(false);

	blockApplySettings(false);
}

QByteArray SSBModGUI::serialize() const
{
	SimpleSerializer s(1);

	s.writeS32(1, m_channelMarker.getCenterFrequency());
	s.writeS32(2, ui->BW->value());
	s.writeS32(3, ui->toneFrequency->value());
    s.writeBlob(4, ui->spectrumGUI->serialize());
	s.writeU32(5, m_channelMarker.getColor().rgb());
	s.writeBlob(6, ui->cwKeyerGUI->serialize());
    s.writeS32(7, ui->lowCut->value());
    s.writeS32(8, ui->spanLog2->value());
    s.writeBool(9, ui->audioBinaural->isChecked());
    s.writeBool(10, ui->audioFlipChannels->isChecked());
    s.writeBool(11, ui->dsb->isChecked());
    s.writeBool(12, ui->agc->isChecked());
    s.writeS32(13, ui->agcTime->value());
    s.writeS32(14, ui->agcThreshold->value());
    s.writeS32(15, ui->agcThresholdGate->value());
    s.writeS32(16, ui->agcThresholdDelay->value());
    s.writeS32(17, ui->agcOrder->value());

	return s.final();
}

bool SSBModGUI::deserialize(const QByteArray& data)
{
	SimpleDeserializer d(data);

	if(!d.isValid())
    {
		resetToDefaults();
		applySettings();
		return false;
	}

	if(d.getVersion() == 1)
    {
		QByteArray bytetmp;
		quint32 u32tmp;
		qint32 tmp;
		bool booltmp;

		blockApplySettings(true);
		m_channelMarker.blockSignals(true);

		d.readS32(1, &tmp, 0);
		m_channelMarker.setCenterFrequency(tmp);
		d.readS32(2, &tmp, 30);
		ui->BW->setValue(tmp);
		d.readS32(3, &tmp, 100);
		ui->toneFrequency->setValue(tmp);
        d.readBlob(4, &bytetmp);
        ui->spectrumGUI->deserialize(bytetmp);

        if(d.readU32(5, &u32tmp))
        {
			m_channelMarker.setColor(u32tmp);
        }

        d.readBlob(6, &bytetmp);
        ui->cwKeyerGUI->deserialize(bytetmp);

        d.readS32(7, &tmp, 3);
        ui->lowCut->setValue(tmp);
        d.readS32(8, &tmp, 3);
        ui->spanLog2->setValue(tmp);
        setNewRate(tmp);
        d.readBool(9, &booltmp);
        ui->audioBinaural->setChecked(booltmp);
        d.readBool(10, &booltmp);
        ui->audioFlipChannels->setChecked(booltmp);
        d.readBool(11, &booltmp);
        ui->dsb->setChecked(booltmp);
        d.readBool(12, &booltmp, false);
        ui->agc->setChecked(booltmp);
        d.readS32(13, &tmp, 7);
        ui->agcTime->setValue(tmp > 9 ? 9 : tmp);
        d.readS32(14, &tmp, -40);
        ui->agcThreshold->setValue(tmp);
        d.readS32(15, &tmp, 4);
        ui->agcThresholdGate->setValue(tmp);
        d.readS32(16, &tmp, 5);
        ui->agcThresholdDelay->setValue(tmp);
        d.readS32(17, &tmp, 20);
        ui->agcOrder->setValue(tmp);

        displaySettings();

        blockApplySettings(false);
		m_channelMarker.blockSignals(false);

	    applySettings();

		return true;
	}
    else
    {
		resetToDefaults();
		applySettings();
		return false;
	}
}

bool SSBModGUI::handleMessage(const Message& message)
{
    if (SSBMod::MsgReportFileSourceStreamData::match(message))
    {
        m_recordSampleRate = ((SSBMod::MsgReportFileSourceStreamData&)message).getSampleRate();
        m_recordLength = ((SSBMod::MsgReportFileSourceStreamData&)message).getRecordLength();
        m_samplesCount = 0;
        updateWithStreamData();
        return true;
    }
    else if (SSBMod::MsgReportFileSourceStreamTiming::match(message))
    {
        m_samplesCount = ((SSBMod::MsgReportFileSourceStreamTiming&)message).getSamplesCount();
        updateWithStreamTime();
        return true;
    }
    else
    {
        return false;
    }
}

void SSBModGUI::channelMarkerUpdate()
{
    m_settings.m_rgbColor = m_channelMarker.getColor().rgb();
    m_settings.m_udpAddress = m_channelMarker.getUDPAddress();
    m_settings.m_udpPort = m_channelMarker.getUDPReceivePort();
    displaySettings();
    applySettings();
}

void SSBModGUI::handleSourceMessages()
{
    Message* message;

    while ((message = getInputMessageQueue()->pop()) != 0)
    {
        if (handleMessage(*message))
        {
            delete message;
        }
    }
}

void SSBModGUI::on_deltaFrequency_changed(qint64 value)
{
    m_channelMarker.setCenterFrequency(value);
    m_settings.m_inputFrequencyOffset = m_channelMarker.getCenterFrequency();
    applySettings();
}

void SSBModGUI::on_dsb_toggled(bool checked)
{
    m_settings.m_dsb = checked;

    if (checked)
    {
        if (ui->BW->value() < 0) {
            ui->BW->setValue(-ui->BW->value());
        }

        m_channelMarker.setSidebands(ChannelMarker::dsb);

        QString bwStr = QString::number(ui->BW->value()/10.0, 'f', 1);
        ui->BWText->setText(tr("%1%2k").arg(QChar(0xB1, 0x00)).arg(bwStr));
        ui->lowCut->setValue(0);
        ui->lowCut->setEnabled(false);

        m_settings.m_bandwidth = ui->BW->value() * 100.0;
        m_settings.m_lowCutoff = 0;

        applySettings();
    }
    else
    {
        if (ui->BW->value() < 0) {
            m_channelMarker.setSidebands(ChannelMarker::lsb);
        } else {
            m_channelMarker.setSidebands(ChannelMarker::usb);
        }

        QString bwStr = QString::number(ui->BW->value()/10.0, 'f', 1);
        ui->BWText->setText(tr("%1k").arg(bwStr));
        ui->lowCut->setEnabled(true);
        m_settings.m_bandwidth = ui->BW->value() * 100.0;

        on_lowCut_valueChanged(m_channelMarker.getLowCutoff()/100);
    }

    setNewRate(m_settings.m_spanLog2);
}

void SSBModGUI::on_audioBinaural_toggled(bool checked)
{
    m_settings.m_audioBinaural = checked;
	applySettings();
}

void SSBModGUI::on_audioFlipChannels_toggled(bool checked)
{
    m_settings.m_audioFlipChannels = checked;
	applySettings();
}

void SSBModGUI::on_spanLog2_valueChanged(int value)
{
    if (setNewRate(value))
    {
        m_settings.m_spanLog2 = value;
        applySettings();
    }

}

void SSBModGUI::on_BW_valueChanged(int value)
{
    QString s = QString::number(value/10.0, 'f', 1);
    m_channelMarker.setBandwidth(value * 200);

    if (ui->dsb->isChecked())
    {
        ui->BWText->setText(tr("%1%2k").arg(QChar(0xB1, 0x00)).arg(s));
    }
    else
    {
        ui->BWText->setText(tr("%1k").arg(s));
    }

    m_settings.m_bandwidth = value * 100;
    on_lowCut_valueChanged(m_channelMarker.getLowCutoff()/100);
	setNewRate(m_settings.m_spanLog2);
}

void SSBModGUI::on_lowCut_valueChanged(int value)
{
    int lowCutoff = getEffectiveLowCutoff(value * 100);
    m_channelMarker.setLowCutoff(lowCutoff);
    QString s = QString::number(lowCutoff/1000.0, 'f', 1);
    ui->lowCutText->setText(tr("%1k").arg(s));
    ui->lowCut->setValue(lowCutoff/100);
    m_settings.m_lowCutoff = ui->lowCut->value() * 100.0;
    applySettings();
}

int SSBModGUI::getEffectiveLowCutoff(int lowCutoff)
{
    int ssbBW = m_channelMarker.getBandwidth() / 2;
    int effectiveLowCutoff = lowCutoff;
    const int guard = 100;

    if (ssbBW < 0)
    {
        if (effectiveLowCutoff < ssbBW + guard)
        {
            effectiveLowCutoff = ssbBW + guard;
        }
        if (effectiveLowCutoff > 0)
        {
            effectiveLowCutoff = 0;
        }
    }
    else
    {
        if (effectiveLowCutoff > ssbBW - guard)
        {
            effectiveLowCutoff = ssbBW - guard;
        }
        if (effectiveLowCutoff < 0)
        {
            effectiveLowCutoff = 0;
        }
    }

    return effectiveLowCutoff;
}

void SSBModGUI::on_toneFrequency_valueChanged(int value)
{
    ui->toneFrequencyText->setText(QString("%1k").arg(value / 100.0, 0, 'f', 2));
    m_settings.m_toneFrequency = value * 10.0;
    applySettings();
}

void SSBModGUI::on_volume_valueChanged(int value)
{
    ui->volumeText->setText(QString("%1").arg(value / 10.0, 0, 'f', 1));
    m_settings.m_volumeFactor = value / 10.0;
    applySettings();
}

void SSBModGUI::on_audioMute_toggled(bool checked)
{
    m_settings.m_audioMute = checked;
	applySettings();
}

void SSBModGUI::on_playLoop_toggled(bool checked)
{
    m_settings.m_playLoop = checked;
    applySettings();
}

void SSBModGUI::on_play_toggled(bool checked)
{
    ui->tone->setEnabled(!checked); // release other source inputs
    ui->morseKeyer->setEnabled(!checked);
    ui->mic->setEnabled(!checked);
    m_modAFInput = checked ? SSBMod::SSBModInputFile : SSBMod::SSBModInputNone;
    SSBMod::MsgConfigureAFInput* message = SSBMod::MsgConfigureAFInput::create(m_modAFInput);
    m_ssbMod->getInputMessageQueue()->push(message);
    ui->navTimeSlider->setEnabled(!checked);
    m_enableNavTime = !checked;
}

void SSBModGUI::on_tone_toggled(bool checked)
{
    ui->play->setEnabled(!checked); // release other source inputs
    ui->morseKeyer->setEnabled(!checked);
    ui->mic->setEnabled(!checked);
    m_modAFInput = checked ? SSBMod::SSBModInputTone : SSBMod::SSBModInputNone;
    SSBMod::MsgConfigureAFInput* message = SSBMod::MsgConfigureAFInput::create(m_modAFInput);
    m_ssbMod->getInputMessageQueue()->push(message);
}

void SSBModGUI::on_morseKeyer_toggled(bool checked)
{
    ui->play->setEnabled(!checked); // release other source inputs
    ui->tone->setEnabled(!checked); // release other source inputs
    ui->mic->setEnabled(!checked);
    m_modAFInput = checked ? SSBMod::SSBModInputCWTone : SSBMod::SSBModInputNone;
    SSBMod::MsgConfigureAFInput* message = SSBMod::MsgConfigureAFInput::create(m_modAFInput);
    m_ssbMod->getInputMessageQueue()->push(message);
}

void SSBModGUI::on_mic_toggled(bool checked)
{
    ui->play->setEnabled(!checked); // release other source inputs
    ui->morseKeyer->setEnabled(!checked);
    ui->tone->setEnabled(!checked); // release other source inputs
    m_modAFInput = checked ? SSBMod::SSBModInputAudio : SSBMod::SSBModInputNone;
    SSBMod::MsgConfigureAFInput* message = SSBMod::MsgConfigureAFInput::create(m_modAFInput);
    m_ssbMod->getInputMessageQueue()->push(message);
}

void SSBModGUI::on_agc_toggled(bool checked)
{
    m_settings.m_agc = checked;
    applySettings();
}

void SSBModGUI::on_agcOrder_valueChanged(int value){
    QString s = QString::number(value / 100.0, 'f', 2);
    ui->agcOrderText->setText(s);
    m_settings.m_agcOrder = value / 100.0;
    applySettings();
}

void SSBModGUI::on_agcTime_valueChanged(int value){
    QString s = QString::number(m_agcTimeConstant[value], 'f', 0);
    ui->agcTimeText->setText(s);
    m_settings.m_agcTime = m_agcTimeConstant[value] * 48;
    applySettings();
}

void SSBModGUI::on_agcThreshold_valueChanged(int value)
{
    displayAGCPowerThreshold(value);
    m_settings.m_agcThreshold = value; // dB
    applySettings();
}

void SSBModGUI::on_agcThresholdGate_valueChanged(int value)
{
    QString s = QString::number(value, 'f', 0);
    ui->agcThresholdGateText->setText(s);
    m_settings.m_agcThresholdGate = value * 48;
    applySettings();
}

void SSBModGUI::on_agcThresholdDelay_valueChanged(int value)
{
    QString s = QString::number(value * 10, 'f', 0);
    ui->agcThresholdDelayText->setText(s);
    m_settings.m_agcThresholdDelay = value * 480;
    applySettings();
}

void SSBModGUI::on_navTimeSlider_valueChanged(int value)
{
    if (m_enableNavTime && ((value >= 0) && (value <= 100)))
    {
        int t_sec = (m_recordLength * value) / 100;
        QTime t(0, 0, 0, 0);
        t = t.addSecs(t_sec);

        SSBMod::MsgConfigureFileSourceSeek* message = SSBMod::MsgConfigureFileSourceSeek::create(value);
        m_ssbMod->getInputMessageQueue()->push(message);
    }
}

void SSBModGUI::on_showFileDialog_clicked(bool checked __attribute__((unused)))
{
    QString fileName = QFileDialog::getOpenFileName(this,
        tr("Open raw audio file"), ".", tr("Raw audio Files (*.raw)"));

    if (fileName != "")
    {
        m_fileName = fileName;
        ui->recordFileText->setText(m_fileName);
        ui->play->setEnabled(true);
        configureFileName();
    }
}

void SSBModGUI::configureFileName()
{
    qDebug() << "FileSourceGui::configureFileName: " << m_fileName.toStdString().c_str();
    SSBMod::MsgConfigureFileSourceName* message = SSBMod::MsgConfigureFileSourceName::create(m_fileName);
    m_ssbMod->getInputMessageQueue()->push(message);
}

void SSBModGUI::onWidgetRolled(QWidget* widget __attribute__((unused)), bool rollDown __attribute__((unused)))
{
}

void SSBModGUI::onMenuDoubleClicked()
{
	if (!m_basicSettingsShown)
	{
		m_basicSettingsShown = true;
		BasicChannelSettingsWidget* bcsw = new BasicChannelSettingsWidget(&m_channelMarker, this);
		bcsw->show();

		if (bcsw->getHasChanged())
		{
		    channelMarkerUpdate();
		}
	}
}

SSBModGUI::SSBModGUI(PluginAPI* pluginAPI, DeviceSinkAPI *deviceAPI, QWidget* parent) :
	RollupWidget(parent),
	ui(new Ui::SSBModGUI),
	m_pluginAPI(pluginAPI),
	m_deviceAPI(deviceAPI),
	m_channelMarker(this),
	m_basicSettingsShown(false),
	m_doApplySettings(true),
	m_rate(6000),
	m_channelPowerDbAvg(20,0),
    m_recordLength(0),
    m_recordSampleRate(48000),
    m_samplesCount(0),
    m_tickCount(0),
    m_enableNavTime(false),
    m_modAFInput(SSBMod::SSBModInputNone)
{
	ui->setupUi(this);
	setAttribute(Qt::WA_DeleteOnClose, true);
	connect(this, SIGNAL(widgetRolled(QWidget*,bool)), this, SLOT(onWidgetRolled(QWidget*,bool)));
	connect(this, SIGNAL(menuDoubleClickEvent()), this, SLOT(onMenuDoubleClicked()));

	m_spectrumVis = new SpectrumVis(ui->glSpectrum);
	m_ssbMod = new SSBMod(m_spectrumVis);
	m_ssbMod->setMessageQueueToGUI(getInputMessageQueue());
	m_channelizer = new UpChannelizer(m_ssbMod);
	m_threadedChannelizer = new ThreadedBasebandSampleSource(m_channelizer, this);
	//m_pluginAPI->addThreadedSink(m_threadedChannelizer);
    m_deviceAPI->addThreadedSource(m_threadedChannelizer);

    resetToDefaults();

	ui->glSpectrum->setCenterFrequency(m_rate/2);
	ui->glSpectrum->setSampleRate(m_rate);
	ui->glSpectrum->setDisplayWaterfall(true);
	ui->glSpectrum->setDisplayMaxHold(true);
	ui->glSpectrum->setSsbSpectrum(true);
	ui->glSpectrum->connectTimer(m_pluginAPI->getMainWindow()->getMasterTimer());

	connect(&m_pluginAPI->getMainWindow()->getMasterTimer(), SIGNAL(timeout()), this, SLOT(tick()));

    ui->deltaFrequencyLabel->setText(QString("%1f").arg(QChar(0x94, 0x03)));
    ui->deltaFrequency->setColorMapper(ColorMapper(ColorMapper::GrayGold));
    ui->deltaFrequency->setValueRange(false, 7, -9999999, 9999999);

	//m_channelMarker = new ChannelMarker(this);
	m_channelMarker.setColor(Qt::green);
	m_channelMarker.setBandwidth(m_rate);
	m_channelMarker.setSidebands(ChannelMarker::usb);
	m_channelMarker.setCenterFrequency(0);
	m_channelMarker.setVisible(true);

	m_deviceAPI->registerChannelInstance(m_channelID, this);
    m_deviceAPI->addChannelMarker(&m_channelMarker);
    m_deviceAPI->addRollupWidget(this);

    ui->cwKeyerGUI->setBuddies(m_ssbMod->getInputMessageQueue(), m_ssbMod->getCWKeyer());
    ui->spectrumGUI->setBuddies(m_spectrumVis->getInputMessageQueue(), m_spectrumVis, ui->glSpectrum);

    displaySettings();
	applySettings();
	setNewRate(m_settings.m_spanLog2);

	connect(getInputMessageQueue(), SIGNAL(messageEnqueued()), this, SLOT(handleSourceMessages()));
	connect(m_ssbMod, SIGNAL(levelChanged(qreal, qreal, int)), ui->volumeMeter, SLOT(levelChanged(qreal, qreal, int)));
}

SSBModGUI::~SSBModGUI()
{
    m_deviceAPI->removeChannelInstance(this);
	m_deviceAPI->removeThreadedSource(m_threadedChannelizer);
	delete m_threadedChannelizer;
	delete m_channelizer;
	delete m_ssbMod;
	delete m_spectrumVis;
	//delete m_channelMarker;
	delete ui;
}

bool SSBModGUI::setNewRate(int spanLog2)
{
	if ((spanLog2 < 1) || (spanLog2 > 5))
	{
		return false;
	}

	m_settings.m_spanLog2 = spanLog2;
	m_rate = 48000 / (1<<spanLog2);

	if (ui->BW->value() < -m_rate/100)
	{
		ui->BW->setValue(-m_rate/100);
		m_channelMarker.setBandwidth(-m_rate*2);
	}
	else if (ui->BW->value() > m_rate/100)
	{
		ui->BW->setValue(m_rate/100);
		m_channelMarker.setBandwidth(m_rate*2);
	}

	if (ui->lowCut->value() < -m_rate/100)
	{
		ui->lowCut->setValue(-m_rate/100);
		m_channelMarker.setLowCutoff(-m_rate);
	}
	else if (ui->lowCut->value() > m_rate/100)
	{
		ui->lowCut->setValue(m_rate/100);
		m_channelMarker.setLowCutoff(m_rate);
	}

	QString s = QString::number(m_rate/1000.0, 'f', 1);

	if (ui->dsb->isChecked())
	{
        ui->BW->setMinimum(0);
        ui->BW->setMaximum(m_rate/100);
        ui->lowCut->setMinimum(0);
        ui->lowCut->setMaximum(m_rate/100);

        m_channelMarker.setSidebands(ChannelMarker::dsb);

        ui->spanText->setText(tr("%1%2k").arg(QChar(0xB1, 0x00)).arg(s));
        ui->glSpectrum->setCenterFrequency(0);
        ui->glSpectrum->setSampleRate(2*m_rate);
        ui->glSpectrum->setSsbSpectrum(false);
        ui->glSpectrum->setLsbDisplay(false);
	}
	else
	{
        ui->BW->setMinimum(-m_rate/100);
        ui->BW->setMaximum(m_rate/100);
        ui->lowCut->setMinimum(-m_rate/100);
        ui->lowCut->setMaximum(m_rate/100);

        if (ui->BW->value() < 0)
        {
            m_channelMarker.setSidebands(ChannelMarker::lsb);
            ui->glSpectrum->setLsbDisplay(true);
        }
        else
        {
            m_channelMarker.setSidebands(ChannelMarker::usb);
            ui->glSpectrum->setLsbDisplay(false);
        }

        ui->spanText->setText(tr("%1k").arg(s));
        ui->glSpectrum->setCenterFrequency(m_rate/2);
        ui->glSpectrum->setSampleRate(m_rate);
        ui->glSpectrum->setSsbSpectrum(true);
	}

	return true;
}

void SSBModGUI::blockApplySettings(bool block)
{
    m_doApplySettings = !block;
}

void SSBModGUI::applySettings()
{
	if (m_doApplySettings)
	{
		setTitleColor(m_channelMarker.getColor());

		m_channelizer->configure(m_channelizer->getInputMessageQueue(),
			48000,
			m_channelMarker.getCenterFrequency());

		ui->deltaFrequency->setValue(m_channelMarker.getCenterFrequency());

        m_ssbMod->configure(m_ssbMod->getInputMessageQueue(),
            m_settings.m_bandwidth,
            m_settings.m_lowCutoff,
            m_settings.m_toneFrequency,
            m_settings.m_volumeFactor,
            m_settings.m_spanLog2,
            m_settings.m_audioBinaural,
            m_settings.m_audioFlipChannels,
            m_settings.m_dsb,
            m_settings.m_audioMute,
            m_settings.m_playLoop,
            m_settings.m_agc,
            m_settings.m_agcOrder,
            m_settings.m_agcTime,
            m_settings.m_agcThreshold,
            m_settings.m_agcThresholdGate,
            m_settings.m_agcThresholdDelay);

//		m_ssbMod->configure(m_ssbMod->getInputMessageQueue(),
//			ui->BW->value() * 100.0f,
//			ui->lowCut->value() * 100.0f,
//			ui->toneFrequency->value() * 10.0f,
//            ui->volume->value() / 10.0f,
//			m_spanLog2,
//			ui->audioBinaural->isChecked(),
//			ui->audioFlipChannels->isChecked(),
//			ui->dsb->isChecked(),
//			ui->audioMute->isChecked(),
//			ui->playLoop->isChecked(),
//			ui->agc->isChecked(),
//			ui->agcOrder->value() / 100.0,
//			m_agcTimeConstant[ui->agcTime->value()],
//            ui->agcThreshold->value(),
//            ui->agcThresholdGate->value(),
//            ui->agcThresholdDelay->value() * 10);
	}
}

void SSBModGUI::displaySettings()
{
    QString s = QString::number(m_agcTimeConstant[ui->agcTime->value()], 'f', 0);
    ui->agcTimeText->setText(s);
    displayAGCPowerThreshold(ui->agcThreshold->value());
    s = QString::number(ui->agcThresholdGate->value(), 'f', 0);
    ui->agcThresholdGateText->setText(s);
    s = QString::number(ui->agcThresholdDelay->value() * 10, 'f', 0);
    ui->agcThresholdDelayText->setText(s);
    s = QString::number(ui->agcOrder->value() / 100.0, 'f', 2);
    ui->agcOrderText->setText(s);
}

void SSBModGUI::displayAGCPowerThreshold(int value)
{
    if (value == -99)
    {
        ui->agcThresholdText->setText("---");
    }
    else
    {
        QString s = QString::number(value, 'f', 0);
        ui->agcThresholdText->setText(s);
    }
}

void SSBModGUI::leaveEvent(QEvent*)
{
	blockApplySettings(true);
	m_channelMarker.setHighlighted(false);
	blockApplySettings(false);
}

void SSBModGUI::enterEvent(QEvent*)
{
	blockApplySettings(true);
	m_channelMarker.setHighlighted(true);
	blockApplySettings(false);
}

void SSBModGUI::tick()
{
    double powDb = CalcDb::dbPower(m_ssbMod->getMagSq());
	m_channelPowerDbAvg.feed(powDb);
	ui->channelPower->setText(tr("%1 dB").arg(m_channelPowerDbAvg.average(), 0, 'f', 1));

    if (((++m_tickCount & 0xf) == 0) && (m_modAFInput == SSBMod::SSBModInputFile))
    {
        SSBMod::MsgConfigureFileSourceStreamTiming* message = SSBMod::MsgConfigureFileSourceStreamTiming::create();
        m_ssbMod->getInputMessageQueue()->push(message);
    }
}

void SSBModGUI::updateWithStreamData()
{
    QTime recordLength(0, 0, 0, 0);
    recordLength = recordLength.addSecs(m_recordLength);
    QString s_time = recordLength.toString("hh:mm:ss");
    ui->recordLengthText->setText(s_time);
    updateWithStreamTime();
}

void SSBModGUI::updateWithStreamTime()
{
    int t_sec = 0;
    int t_msec = 0;

    if (m_recordSampleRate > 0)
    {
        t_msec = ((m_samplesCount * 1000) / m_recordSampleRate) % 1000;
        t_sec = m_samplesCount / m_recordSampleRate;
    }

    QTime t(0, 0, 0, 0);
    t = t.addSecs(t_sec);
    t = t.addMSecs(t_msec);
    QString s_timems = t.toString("hh:mm:ss.zzz");
    QString s_time = t.toString("hh:mm:ss");
    ui->relTimeText->setText(s_timems);

    if (!m_enableNavTime)
    {
        float posRatio = (float) t_sec / (float) m_recordLength;
        ui->navTimeSlider->setValue((int) (posRatio * 100.0));
    }
}
