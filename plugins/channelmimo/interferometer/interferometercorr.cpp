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

#include <algorithm>

#include "dsp/fftengine.h"
#include "interferometercorr.h"

Sample sAdd(const Sample& a, const Sample& b) { //!< Sample addition
    return Sample{a.real() + b.real(), a.imag() + b.imag()};
}

Sample sMulConj(const Sample& a, const Sample& b) { //!< Sample multiply with conjugate
    return Sample{a.real()*b.real() + a.imag()*b.imag(), a.imag()*b.real() - a.real()*b.imag()};
}

Sample cf2s(const std::complex<float>& a) { //!< Complex float to Sample
    Sample s;
    s.setReal(a.real()*SDR_RX_SCALEF);
    s.setImag(a.imag()*SDR_RX_SCALEF);
    return s;
}

InterferometerCorrelator::InterferometerCorrelator(int fftSize) :
    m_corrType(InterferometerSettings::CorrelationAdd),
    m_fftSize(fftSize)
{
    for (int i = 0; i < 2; i++)
    {
        m_fft[i] = FFTEngine::create();
        m_fft[i]->configure(2*fftSize, false); // internally twice the data FFT size
    }

    m_invFFT = FFTEngine::create();
    m_invFFT->configure(2*fftSize, true);

    m_dataj = new std::complex<float>[2*fftSize]; // receives actual FFT result hence twice the data FFT size
    m_scorr.resize(2*fftSize);
    m_tcorr.resize(2*fftSize);
}

InterferometerCorrelator::~InterferometerCorrelator()
{
    for (int i = 0; i < 2; i++)
    {
        delete[] m_fft[i];
    }

    delete[] m_dataj;
}

void InterferometerCorrelator::performCorr(const SampleVector& data0, const SampleVector& data1)
{
    switch (m_corrType)
    {
        case InterferometerSettings::CorrelationAdd:
            performOpCorr(data0, data1, sAdd);
            break;
        case InterferometerSettings::CorrelationMultiply:
            performOpCorr(data0, data1, sMulConj);
            break;
        case InterferometerSettings::CorrelationCorrelation:
            performFFTCorr(data0, data1);
            break;
        default:
            break;
    }
}

void InterferometerCorrelator::performOpCorr(const SampleVector& data0, const SampleVector& data1, Sample sampleOp(const Sample& a, const Sample& b))
{
    unsigned int size = std::min(data0.size(), data1.size());
    adjustTCorrSize(size);

    std::transform(
        data0.begin(),
        data0.begin() + size,
        data1.begin(),
        m_tcorr.begin(),
        sampleOp
    );

    m_processed = size;
    m_remaining = 0;
}

void InterferometerCorrelator::performFFTCorr(const SampleVector& data0, const SampleVector& data1)
{
    unsigned int size = std::min(data0.size(), data1.size());
    SampleVector::const_iterator begin0 = data0.begin();
    SampleVector::const_iterator begin1 = data1.begin();
    adjustSCorrSize(size);
    adjustTCorrSize(size);

    while (size > m_fftSize)
    {
        // FFT[0]
        std::transform(
            begin0,
            begin0 + m_fftSize,
            m_fft[0]->in(),
            [](const Sample& s) -> std::complex<float> {
                return std::complex<float>{s.real() / SDR_RX_SCALEF, s.imag() / SDR_RX_SCALEF};
            }
        );
        std::fill(m_fft[0]->in() + m_fftSize, m_fft[0]->in() + 2*m_fftSize, std::complex<float>{0, 0});
        m_fft[0]->transform();

        // FFT[1]
        std::transform(
            begin1,
            begin1 + m_fftSize,
            m_fft[1]->in(),
            [](const Sample& s) -> std::complex<float> {
                return std::complex<float>{s.real() / SDR_RX_SCALEF, s.imag() / SDR_RX_SCALEF};
            }
        );
        std::fill(m_fft[1]->in() + m_fftSize, m_fft[1]->in() + 2*m_fftSize, std::complex<float>{0, 0});
        m_fft[1]->transform();

        // conjugate FFT[1]
        std::transform(
            m_fft[1]->out(),
            m_fft[1]->out()+2*m_fftSize,
            m_dataj,
            [](const std::complex<float>& c) -> std::complex<float> {
                return std::conj(c);
            }
        );

        // product of FFT[1]* with FFT[0] and store in inverse FFT input
        std::transform(
            m_fft[0]->out(),
            m_fft[0]->out()+2*m_fftSize,
            m_dataj,
            m_invFFT->in(),
            [](std::complex<float>& a, const std::complex<float>& b) -> std::complex<float> {
                return a*b;
            }
        );

        // copy product to correlation spectrum
        std::transform(
            m_invFFT->in(),
            m_invFFT->in() + 2*m_fftSize,
            m_scorr.begin(),
            cf2s
        );

        // do the inverse FFT to get time correlation
        m_invFFT->transform();
        std::transform(
            m_invFFT->out(),
            m_invFFT->out() + 2*m_fftSize,
            m_tcorr.begin(),
            cf2s
        );

        // TODO: do something with the result
        size -= m_fftSize;
        begin0 += m_fftSize;
        begin1 += m_fftSize;
    }

    // update the samples counters
    m_processed = begin0 - data0.begin();
    m_remaining = size - m_fftSize;
}

void InterferometerCorrelator::adjustSCorrSize(int size)
{
    if (size > m_scorrSize)
    {
        m_scorr.resize(size);
        m_scorrSize = size;
    }
}

void InterferometerCorrelator::adjustTCorrSize(int size)
{
    if (size > m_tcorrSize)
    {
        m_tcorr.resize(size);
        m_tcorrSize = size;
    }
}
