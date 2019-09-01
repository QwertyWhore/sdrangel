///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2019 Edouard Griffiths, F4EXB                                   //
//                                                                               //
// This program is free software; you can redistribute it and/or modify          //
// it under the terms of the GNU General Public License as published by          //
// the Free Software Foundation as version 3 of the License, or                  //
// (at your option) any later version.                                           //
//                                                                               //
// This program is distributed in the hope that it will be useful,               //
// but WITHOUT ANY WARRANTY; without even the implied warranty of                //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                  //
// GNU General Public License V3 for more details.                               //
//                                                                               //
// You should have received a copy of the GNU General Public License             //
// along with this program. If not, see <http://www.gnu.org/licenses/>.          //
///////////////////////////////////////////////////////////////////////////////////

#ifndef INCLUDE_INTERFEROMETERSINK_H
#define INCLUDE_INTERFEROMETERSINK_H

#include <QObject>

#include "dsp/mimosamplesink.h"
#include "dsp/samplesinkvector.h"
#include "interferometerstreamsink.h"
#include "interferometercorr.h"

class DownChannelizer;
class BasebandSampleSink;

class InterferometerSink : public QObject
{
    Q_OBJECT
public:
    class MsgConfigureChannelizer : public Message {
        MESSAGE_CLASS_DECLARATION

    public:
        int getLog2Decim() const { return m_log2Decim; }
        int getFilterChainHash() const { return m_filterChainHash; }

        static MsgConfigureChannelizer* create(unsigned int log2Decim, unsigned int filterChainHash) {
            return new MsgConfigureChannelizer(log2Decim, filterChainHash);
        }

    private:
        unsigned int m_log2Decim;
        unsigned int m_filterChainHash;

        MsgConfigureChannelizer(unsigned int log2Decim, unsigned int filterChainHash) :
            Message(),
            m_log2Decim(log2Decim),
            m_filterChainHash(filterChainHash)
        { }
    };

    InterferometerSink();
    ~InterferometerSink();
    MessageQueue *getInputMessageQueue() { return &m_inputMessageQueue; } //!< Get the queue for asynchronous inbound communication

    void setSpectrumSink(BasebandSampleSink *spectrumSink) { m_spectrumSink = spectrumSink; }
    void setScopeSink(BasebandSampleSink *scopeSink) { m_scopeSink = scopeSink; }

	void feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end, unsigned int streamIndex);

private:
    void run();
    bool handleMessage(const Message& cmd);

    InterferometerCorrelator m_correlator;
    SampleSinkVector m_sinkBuffers[2];
    InterferometerStreamSink m_sinks[2];
    DownChannelizer *m_channelizers[2];
    BasebandSampleSink *m_spectrumSink;
    BasebandSampleSink *m_scopeSink;
	MessageQueue m_inputMessageQueue; //!< Queue for asynchronous inbound communication

private slots:
	void handleSinkBuffer(unsigned int sinkIndex); //!< Handle data when samples have to be processed
    void handleInputMessages();
};

#endif // INCLUDE_INTERFEROMETERSINK_H
