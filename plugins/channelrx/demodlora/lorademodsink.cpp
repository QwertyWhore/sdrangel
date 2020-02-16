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

#include <QTime>
#include <QDebug>
#include <stdio.h>

#include "dsp/dsptypes.h"
#include "dsp/basebandsamplesink.h"
#include "dsp/fftengine.h"
#include "util/db.h"

#include "lorademodmsg.h"
#include "lorademodsink.h"

LoRaDemodSink::LoRaDemodSink() :
    m_decodeMsg(nullptr),
    m_decoderMsgQueue(nullptr),
    m_spectrumSink(nullptr),
    m_spectrumBuffer(nullptr),
    m_downChirps(nullptr),
    m_upChirps(nullptr),
    m_fftBuffer(nullptr),
    m_spectrumLine(nullptr)
{
    m_demodActive = false;
	m_bandwidth = LoRaDemodSettings::bandwidths[0];
	m_channelSampleRate = 96000;
	m_channelFrequencyOffset = 0;
	m_nco.setFreq(m_channelFrequencyOffset, m_channelSampleRate);
	m_interpolator.create(16, m_channelSampleRate, m_bandwidth / 1.9f);
	m_interpolatorDistance = (Real) m_channelSampleRate / (Real) m_bandwidth;
    m_sampleDistanceRemain = 0;

    m_state = LoRaStateReset;
	m_chirp = 0;
	m_chirp0 = 0;

    m_fft = FFTEngine::create();
    m_fftSFD = FFTEngine::create();

    initSF(m_settings.m_spreadFactor, m_settings.m_deBits);
}

LoRaDemodSink::~LoRaDemodSink()
{
    delete m_fft;
    delete m_fftSFD;
    delete[] m_downChirps;
    delete[] m_upChirps;
    delete[] m_spectrumBuffer;
    delete[] m_spectrumLine;
}

void LoRaDemodSink::initSF(unsigned int sf, unsigned int deBits)
{
    if (m_downChirps) {
        delete[] m_downChirps;
    }
    if (m_upChirps) {
        delete[] m_upChirps;
    }
    if (m_fftBuffer) {
        delete[] m_fftBuffer;
    }
    if (m_spectrumBuffer) {
        delete[] m_spectrumBuffer;
    }
    if (m_spectrumLine) {
        delete[] m_spectrumLine;
    }

    m_nbSymbols = 1 << sf;
    m_nbSymbolsEff = 1 << (sf - deBits);
    m_fftLength = m_nbSymbols;
    m_fft->configure(m_fftInterpolation*m_fftLength, false);
    m_fftSFD->configure(m_fftInterpolation*m_fftLength, false);
    m_state = LoRaStateReset;
    m_sfdSkip = m_fftLength / 4;
    m_fftWindow.create(FFTWindow::Function::Kaiser, m_fftLength);
    m_fftWindow.setKaiserAlpha(M_PI);
    m_downChirps = new Complex[2*m_nbSymbols]; // Each table is 2 chirps long to allow processing from arbitrary offsets.
    m_upChirps = new Complex[2*m_nbSymbols];
    m_fftBuffer = new Complex[m_fftInterpolation*m_fftLength];
    m_spectrumBuffer = new Complex[m_nbSymbols];
    m_spectrumLine = new Complex[m_nbSymbols];
    std::fill(m_spectrumLine, m_spectrumLine+m_nbSymbols, Complex(std::polar(1e-6*SDR_RX_SCALED, 0.0)));

    float halfAngle = M_PI;
    float phase = -halfAngle;
    double accumulator = 0;

    for (int i = 0; i < m_fftLength; i++)
    {
        accumulator = fmod(accumulator + phase, 2*M_PI);
        m_downChirps[i] = Complex(std::conj(std::polar(1.0, accumulator)));
        m_upChirps[i] = Complex(std::polar(1.0, accumulator));
        phase += (2*halfAngle) / m_nbSymbols;
    }

    // Duplicate table to allow processing from arbitrary offsets
    std::copy(m_downChirps, m_downChirps+m_fftLength, m_downChirps+m_fftLength);
    std::copy(m_upChirps, m_upChirps+m_fftLength, m_upChirps+m_fftLength);
}

void LoRaDemodSink::feed(const SampleVector::const_iterator& begin, const SampleVector::const_iterator& end)
{
	int newangle;
	Complex ci;

	for (SampleVector::const_iterator it = begin; it < end; ++it)
	{
		Complex c(it->real() / SDR_RX_SCALEF, it->imag() / SDR_RX_SCALEF);
		c *= m_nco.nextIQ();

		if (m_interpolator.decimate(&m_sampleDistanceRemain, c, &ci))
		{
            processSample(ci);
			m_sampleDistanceRemain += m_interpolatorDistance;
		}
	}
}

void LoRaDemodSink::processSample(const Complex& ci)
{
    if (m_state == LoRaStateReset) // start over
    {
        m_demodActive = false;
        reset();
        m_state = LoRaStateDetectPreamble;
    }
    else if (m_state == LoRaStateDetectPreamble) // look for preamble
    {
        m_fft->in()[m_fftCounter++] = ci * m_downChirps[m_chirp]; // de-chirp the up ramp

        if (m_fftCounter == m_fftLength)
        {
            m_fftWindow.apply(m_fft->in());
            std::fill(m_fft->in()+m_fftLength, m_fft->in()+m_fftInterpolation*m_fftLength, Complex{0.0, 0.0});
            m_fft->transform();
            m_fftCounter = 0;
            double magsq;

            unsigned int imax = argmax(
                m_fft->out(),
                m_fftInterpolation,
                m_fftLength,
                magsq,
                m_spectrumBuffer,
                m_fftInterpolation
            ) / m_fftInterpolation;

            if (m_magsqQueue.size() > m_requiredPreambleChirps + 1) {
                m_magsqQueue.pop();
            }

            m_magsqQueue.push(magsq);
            m_argMaxHistory[m_argMaxHistoryCounter++] = imax;

            if (m_argMaxHistoryCounter == m_requiredPreambleChirps)
            {
                m_argMaxHistoryCounter = 0;
                bool preambleFound = true;

                for (int i = 1; i < m_requiredPreambleChirps; i++)
                {
                    if (m_argMaxHistory[0] != m_argMaxHistory[i])
                    {
                        preambleFound = false;
                        break;
                    }
                }

                if ((preambleFound) && (magsq > 1e-9))
                {
                    if (m_spectrumSink) {
                        m_spectrumSink->feed(m_spectrumBuffer, m_nbSymbols);
                    }

                    qDebug("LoRaDemodSink::processSample: preamble found: %u|%f", m_argMaxHistory[0], magsq);
                    m_chirp = m_argMaxHistory[0];
                    m_fftCounter = m_chirp;
                    m_chirp0 = 0;
                    m_chirpCount = 0;
                    m_state = LoRaStatePreambleResyc;
                }
                else
                {
                    m_magsqOffAvg(m_magsqQueue.front());
                }
            }
        }
    }
    else if (m_state == LoRaStatePreambleResyc)
    {
        m_fftCounter++;

        if (m_fftCounter == m_fftLength)
        {
            if (m_spectrumSink) {
                m_spectrumSink->feed(m_spectrumLine, m_nbSymbols);
            }

            m_fftCounter = 0;
            m_demodActive = true;
            m_state = LoRaStatePreamble;
        }
    }
    else if (m_state == LoRaStatePreamble) // preamble found look for SFD start
    {
        m_fft->in()[m_fftCounter] = ci * m_downChirps[m_chirp];  // de-chirp the up ramp
        m_fftSFD->in()[m_fftCounter] = ci * m_upChirps[m_chirp]; // de-chiro the down ramp
        m_fftCounter++;

        if (m_fftCounter == m_fftLength)
        {
            std::copy(m_fftSFD->in(), m_fftSFD->in() + m_fftLength, m_fftBuffer); // save for later

            m_fftWindow.apply(m_fft->in());
            std::fill(m_fft->in()+m_fftLength, m_fft->in()+m_fftInterpolation*m_fftLength, Complex{0.0, 0.0});
            m_fft->transform();

            m_fftWindow.apply(m_fftSFD->in());
            std::fill(m_fftSFD->in()+m_fftLength, m_fftSFD->in()+m_fftInterpolation*m_fftLength, Complex{0.0, 0.0});
            m_fftSFD->transform();

            m_fftCounter = 0;
            double magsq, magsqSFD;

            unsigned int imaxSFD = argmax(
                m_fftSFD->out(),
                m_fftInterpolation,
                m_fftLength,
                magsqSFD,
                nullptr,
                m_fftInterpolation
            ) / m_fftInterpolation;

            unsigned int imax = argmax(
                m_fft->out(),
                m_fftInterpolation,
                m_fftLength,
                magsq,
                m_spectrumBuffer,
                m_fftInterpolation
            ) / m_fftInterpolation;

            m_preambleHistory[m_chirpCount] = imax;
            m_chirpCount++;

            if (magsq <  magsqSFD) // preamble drop
            {
                if (m_chirpCount < 3) // too early
                {
                    m_state = LoRaStateReset;
                }
                else
                {
                    m_syncWord = round(m_preambleHistory[m_chirpCount-2] / 8.0);
                    m_syncWord += 16 * round(m_preambleHistory[m_chirpCount-3] / 8.0);
                    qDebug("LoRaDemodSink::processSample: SFD found:  up: %4u|%11.6f - down: %4u|%11.6f sync: %x", imax, magsq, imaxSFD, magsqSFD, m_syncWord);

                    int sadj = 0;
                    int nadj = 0;
                    int zadj;
                    int sfdSkip = m_sfdSkip;

                    for (int i = 0; i < m_chirpCount-3; i++)
                    {
                        sadj += m_preambleHistory[i] > m_nbSymbols/2 ? m_preambleHistory[i] - m_nbSymbols : m_preambleHistory[i];
                        nadj++;
                    }

                    zadj = nadj == 0 ? 0 : sadj / nadj;
                    zadj = zadj < -(sfdSkip/2) ? -(sfdSkip/2) : zadj > sfdSkip/2 ? sfdSkip/2 : zadj;
                    qDebug("LoRaDemodSink::processSample: zero adjust: %d (%d)", zadj, nadj);

                    m_sfdSkipCounter = 0;
                    m_fftCounter = m_fftLength - m_sfdSkip + zadj;
                    m_chirp += zadj;
                    //std::copy(m_fftBuffer+m_sfdSkip, m_fftBuffer+(m_fftLength-m_sfdSkip), m_fftBuffer); // prepare sliding fft
                    m_state = LoRaStateSkipSFD; //LoRaStateSlideSFD;
                }
            }
            else if (m_chirpCount > m_maxSFDSearchChirps) // SFD missed start over
            {
                m_state = LoRaStateReset;
            }
            else
            {
                if (m_spectrumSink) {
                    m_spectrumSink->feed(m_spectrumBuffer, m_nbSymbols);
                }

                qDebug("LoRaDemodSink::processSample: SFD search: up: %4u|%11.6f - down: %4u|%11.6f", imax, magsq, imaxSFD, magsqSFD);
                m_magsqOnAvg(magsq);
            }
        }
    }
    else if (m_state == LoRaStateSkipSFD) // Just skip SFD
    {
        m_fftCounter++;

        if (m_fftCounter == m_fftLength)
        {
            m_fftCounter = m_fftLength - m_sfdSkip;
            m_sfdSkipCounter++;

            if (m_sfdSkipCounter == m_sfdFourths) // 1.25 SFD chips left
            {
                qDebug("LoRaDemodSink::processSample: SFD skipped");
                m_chirp = m_chirp0;
                m_fftCounter = 0;
                m_chirpCount = 0;
                int correction = 0;
                m_magsqMax = 0.0;
                m_decodeMsg = LoRaDemodMsg::MsgDecodeSymbols::create();
                m_decodeMsg->setSyncWord(m_syncWord);
                m_state = LoRaStateReadPayload;
            }
        }
    }
    else if (m_state == LoRaStateSlideSFD) // perform sliding FFTs over the rest of the SFD period
    {
        m_fftBuffer[m_fftCounter] = ci * m_upChirps[m_chirp]; // de-chirp the down ramp
        m_fftCounter++;

        if (m_fftCounter == m_fftLength)
        {
            std::copy(m_fftBuffer, m_fftBuffer + m_fftLength, m_fftSFD->in());
            std::fill(m_fftSFD->in()+m_fftLength, m_fftSFD->in()+m_fftInterpolation*m_fftLength, Complex{0.0, 0.0});
            m_fftSFD->transform();
            std::copy(m_fftBuffer+m_sfdSkip, m_fftBuffer+(m_fftLength-m_sfdSkip), m_fftBuffer); // prepare next sliding fft
            m_fftCounter = m_fftLength - m_sfdSkip;
            m_sfdSkipCounter++;

            double magsqSFD;

            unsigned int imaxSFD = argmax(
                m_fftSFD->out(),
                m_fftInterpolation,
                m_fftLength,
                magsqSFD,
                m_spectrumBuffer,
                m_fftInterpolation
            ) / m_fftInterpolation;

            if (m_spectrumSink) {
                m_spectrumSink->feed(m_spectrumBuffer, m_nbSymbols);
            }

            qDebug("LoRaDemodSink::processSample: SFD slide %u %4u|%11.6f", m_sfdSkipCounter, imaxSFD, magsqSFD);

            if (m_sfdSkipCounter == m_sfdFourths) // 1.25 SFD chips length
            {
                qDebug("LoRaDemodSink::processSample: SFD done");
                m_chirp = m_chirp0;
                m_fftCounter = 0;
                m_chirpCount = 0;
                int correction = 0;
                m_magsqMax = 0.0;
                m_decodeMsg = LoRaDemodMsg::MsgDecodeSymbols::create();
                m_decodeMsg->setSyncWord(m_syncWord);
                m_state = LoRaStateReadPayload; //LoRaStateReadPayload;
            }
        }
    }
    else if (m_state == LoRaStateReadPayload)
    {
        m_fft->in()[m_fftCounter] = ci * m_downChirps[m_chirp]; // de-chirp the up ramp
        m_fftCounter++;

        if (m_fftCounter == m_fftLength)
        {
            m_fftWindow.apply(m_fft->in());
            std::fill(m_fft->in()+m_fftLength, m_fft->in()+m_fftInterpolation*m_fftLength, Complex{0.0, 0.0});
            m_fft->transform();
            m_fftCounter = 0;
            double magsq;

            unsigned int symbol = evalSymbol(
                argmax(
                    m_fft->out(),
                    m_fftInterpolation,
                    m_fftLength,
                    magsq,
                    m_spectrumBuffer,
                    m_fftInterpolation
                )
            ) % m_nbSymbolsEff;

            if (m_spectrumSink) {
                m_spectrumSink->feed(m_spectrumBuffer, m_nbSymbols);
            }

            if (magsq > m_magsqMax) {
                m_magsqMax = magsq;
            }

            m_decodeMsg->pushBackSymbol(symbol);

            if ((m_chirpCount == 0)
            ||  (m_settings.m_eomSquelchTenths == 121) // max - disable squelch
            || ((m_settings.m_eomSquelchTenths*magsq)/10.0 > m_magsqMax))
            {
                qDebug("LoRaDemodSink::processSample: symbol %02u: %4u|%11.6f", m_chirpCount, symbol, magsq);
                m_magsqOnAvg(magsq);
                m_chirpCount++;

                if (m_chirpCount > m_settings.m_nbSymbolsMax)
                {
                    qDebug("LoRaDemodSink::processSample: message length exceeded");
                    m_state = LoRaStateReset;
                    m_decodeMsg->setSignalDb(CalcDb::dbPower(m_magsqOnAvg.asDouble() / (1<<m_settings.m_spreadFactor)));
                    m_decodeMsg->setNoiseDb(CalcDb::dbPower(m_magsqOffAvg.asDouble() / (1<<m_settings.m_spreadFactor)));

                    if (m_decoderMsgQueue && m_settings.m_decodeActive) {
                        m_decoderMsgQueue->push(m_decodeMsg);
                    } else {
                        delete m_decodeMsg;
                    }
                }
            }
            else
            {
                qDebug("LoRaDemodSink::processSample: end of message");
                m_state = LoRaStateReset;
                m_decodeMsg->popSymbol(); // last symbol is garbage
                m_decodeMsg->setSignalDb(CalcDb::dbPower(m_magsqOnAvg.asDouble() / (1<<m_settings.m_spreadFactor)));
                m_decodeMsg->setNoiseDb(CalcDb::dbPower(m_magsqOffAvg.asDouble() / (1<<m_settings.m_spreadFactor)));

                if (m_decoderMsgQueue && m_settings.m_decodeActive) {
                    m_decoderMsgQueue->push(m_decodeMsg);
                } else {
                    delete m_decodeMsg;
                }
            }
        }
    }
    else
    {
        m_state = LoRaStateReset;
    }

    m_chirp++;

    if (m_chirp >= m_chirp0 + m_nbSymbols) {
        m_chirp = m_chirp0;
    }
}

void LoRaDemodSink::reset()
{
    m_chirp = 0;
    m_chirp0 = 0;
    m_fftCounter = 0;
    m_argMaxHistoryCounter = 0;
    m_sfdSkipCounter = 0;
}

unsigned int LoRaDemodSink::argmax(
    const Complex *fftBins,
    unsigned int fftMult,
    unsigned int fftLength,
    double& magsqMax,
    Complex *specBuffer,
    unsigned int specDecim)
{
    magsqMax = 0.0;
    unsigned int imax;
    double magSum = 0.0;

    for (unsigned int i = 0; i < fftMult*fftLength; i++)
    {
        double magsq = std::norm(fftBins[i]);

        if (magsq > magsqMax)
        {
            imax = i;
            magsqMax = magsq;
        }

        if (specBuffer)
        {
            magSum += magsq;

            if (i % specDecim == specDecim - 1)
            {
                specBuffer[i/specDecim] = Complex(std::polar(magSum, 0.0));
                magSum = 0.0;
            }
        }
    }

    return imax;
}

void LoRaDemodSink::decimateSpectrum(Complex *in, Complex *out, unsigned int size, unsigned int decimation)
{
    for (unsigned int i = 0; i < size; i++)
    {
        if (i % decimation == 0) {
            out[i/decimation] = in[i];
        }
    }
}

int LoRaDemodSink::toSigned(int u, int intSize)
{
    if (u > intSize/2) {
        return u - intSize;
    } else {
        return u;
    }
}

unsigned int LoRaDemodSink::evalSymbol(unsigned int rawSymbol)
{
    unsigned int spread = m_fftInterpolation * (1<<m_settings.m_deBits);

    if (spread < 2 ) {
        return rawSymbol;
    } else {
        return (rawSymbol + spread/2 - 1) / spread; // middle point goes to symbol below (smear to the right)
    }
}

void LoRaDemodSink::applyChannelSettings(int channelSampleRate, int bandwidth, int channelFrequencyOffset, bool force)
{
    qDebug() << "LoRaDemodSink::applyChannelSettings:"
            << " channelSampleRate: " << channelSampleRate
            << " channelFrequencyOffset: " << channelFrequencyOffset
            << " bandwidth: " << bandwidth;

    if ((channelFrequencyOffset != m_channelFrequencyOffset) ||
        (channelSampleRate != m_channelSampleRate) || force)
    {
        m_nco.setFreq(-channelFrequencyOffset, channelSampleRate);
    }

    if ((channelSampleRate != m_channelSampleRate) ||
        (bandwidth != m_bandwidth) || force)
    {
        m_interpolator.create(16, channelSampleRate, bandwidth / 1.25f);
        m_interpolatorDistance = (Real) channelSampleRate / (Real) bandwidth;
        m_sampleDistanceRemain = 0;
        qDebug() << "LoRaDemodSink::applyChannelSettings: m_interpolator.create:"
            << " m_interpolatorDistance: " << m_interpolatorDistance;
    }

    m_channelSampleRate = channelSampleRate;
    m_bandwidth = bandwidth;
    m_channelFrequencyOffset = channelFrequencyOffset;
}

void LoRaDemodSink::applySettings(const LoRaDemodSettings& settings, bool force)
{
    qDebug() << "LoRaDemodSink::applySettings:"
            << " m_inputFrequencyOffset: " << settings.m_inputFrequencyOffset
            << " m_bandwidthIndex: " << settings.m_bandwidthIndex
            << " m_spreadFactor: " << settings.m_spreadFactor
            << " m_rgbColor: " << settings.m_rgbColor
            << " m_title: " << settings.m_title
            << " force: " << force;

    if ((settings.m_spreadFactor != m_settings.m_spreadFactor)
     || (settings.m_deBits != m_settings.m_deBits) || force) {
        initSF(settings.m_spreadFactor, settings.m_deBits);
    }

    m_settings = settings;
}
