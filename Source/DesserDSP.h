#pragma once
// =============================================================================
//  SilverDesser - nucleo de DSP (sem dependencia do JUCE)
//
//  Estrutura (inspirada no Oxford SuprEsser):
//   * Par complementar bandpass / band-reject em FIR de fase linear
//     (convolucao particionada por FFT, com crossfade ao mudar o filtro).
//   * Trigger (BAND/WIDE) e Audio (BAND/WIDE) independentes.
//   * Threshold com Level Tracking automatico (janela de 24 dB, resposta assimetrica).
//   * Ratio em graus (45 = 1:1, 0 = limitador, negativo = "cut"), soft knee, make-up.
//   * Reaction Envelope: attack / hold / release.
//   * Look-ahead fixo (o audio principal e atrasado em relacao ao detector).
// =============================================================================

#include <vector>
#include <complex>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <atomic>
#include <array>

namespace silver
{

constexpr double kPi = 3.14159265358979323846;

inline float  dbToGain (float db)  { return std::pow (10.0f, db * 0.05f); }
inline double dbToGainD (double db) { return std::pow (10.0, db * 0.05); }

//==============================================================================
//  FFT radix-2 (in-place). Sem escala: quem chama a inversa divide por N.
//==============================================================================
template <typename T>
class FFTT
{
public:
    using C = std::complex<T>;

    void prepare (int n)
    {
        size = n;
        rev.assign ((size_t) n, 0);
        tw.assign ((size_t) std::max (1, n / 2), C());

        int bits = 0;
        while ((1 << bits) < n) ++bits;

        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }

        for (int k = 0; k < n / 2; ++k)
        {
            const double a = -2.0 * kPi * (double) k / (double) n;
            tw[(size_t) k] = C ((T) std::cos (a), (T) std::sin (a));
        }
    }

    void transform (C* d, bool inverse) const
    {
        for (int i = 0; i < size; ++i)
        {
            const int j = rev[(size_t) i];
            if (i < j) std::swap (d[i], d[j]);
        }

        for (int len = 2; len <= size; len <<= 1)
        {
            const int half = len >> 1;
            const int step = size / len;

            for (int i = 0; i < size; i += len)
            {
                for (int j = 0; j < half; ++j)
                {
                    const C w = tw[(size_t) (j * step)];
                    const T wr = w.real();
                    const T wi = inverse ? -w.imag() : w.imag();

                    const C a = d[i + j];
                    const C b = d[i + j + half];
                    const T vr = b.real() * wr - b.imag() * wi;
                    const T vi = b.real() * wi + b.imag() * wr;

                    d[i + j]        = C (a.real() + vr, a.imag() + vi);
                    d[i + j + half] = C (a.real() - vr, a.imag() - vi);
                }
            }
        }
    }

    int getSize() const { return size; }

private:
    int size = 0;
    std::vector<int> rev;
    std::vector<C> tw;
};

//==============================================================================
//  Linha de atraso circular (potencia de 2)
//==============================================================================
class DelayLine
{
public:
    void prepare (int maxDelay)
    {
        int cap = 1;
        while (cap < maxDelay + 2) cap <<= 1;
        buf.assign ((size_t) cap, 0.0f);
        mask = cap - 1;
        pos = 0;
    }

    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }

    inline void push (float x) { pos = (pos + 1) & mask; buf[(size_t) pos] = x; }

    // get (0) = ultima amostra inserida
    inline float get (int d) const { return buf[(size_t) ((pos - d) & mask)]; }

private:
    std::vector<float> buf;
    int mask = 0, pos = 0;
};

//==============================================================================
//  Separador de banda FIR (fase linear): produz o bandpass; o band-reject e
//  obtido por (entrada atrasada - bandpass), logo a soma e perfeita.
//==============================================================================
class FirBandSplitter
{
public:
    void prepare (double sampleRate, int channels)
    {
        fs = sampleRate;
        numCh = std::max (1, std::min (2, channels));

        // tamanho do kernel (acompanha a taxa de amostragem)
        N = 2048;
        if (fs > 60000.0)  N = 4096;
        if (fs > 120000.0) N = 8192;

        B = 128;
        P = N / B;
        F = 2 * B;

        fftF.prepare (F);
        fftN.prepare (N);

        kA.assign ((size_t) P * (size_t) F, std::complex<float>());
        kB = kA;
        kP = kA;

        for (auto& c : ch)
        {
            c.inBlock.assign ((size_t) F, 0.0f);
            c.outBlock.assign ((size_t) B, 0.0f);
            c.fdl.assign ((size_t) P * (size_t) F, std::complex<float>());
            c.fdlPos = 0;
        }

        buf.assign ((size_t) F, std::complex<float>());
        acc.assign ((size_t) F, std::complex<float>());
        accB.assign ((size_t) F, std::complex<float>());
        designSpec.assign ((size_t) N, std::complex<double>());
        designH.assign ((size_t) N, 0.0);

        pos = 0;
        fading = false;
        pendingValid = false;
        fadeBlock = 0;

        // kernel inicial: passa-tudo (impulso centrado) ate o primeiro setBand
        currentFc = -1.0;
        setBand (7000.0, 1.4, 72.0, true);
    }

    void reset()
    {
        for (auto& c : ch)
        {
            std::fill (c.inBlock.begin(), c.inBlock.end(), 0.0f);
            std::fill (c.outBlock.begin(), c.outBlock.end(), 0.0f);
            std::fill (c.fdl.begin(), c.fdl.end(), std::complex<float>());
            c.fdlPos = 0;
        }
        pos = 0;
    }

    int getLatency() const { return N / 2 + B; }
    int getKernelSize() const { return N; }

    // Projeta o filtro. force = aplica direto (sem crossfade).
    void setBand (double fc, double widthOct, double slopeDb, bool force = false)
    {
        currentFc = fc; currentW = widthOct; currentS = slopeDb;
        designBandKernel (fs, fc, widthOct, slopeDb, N, designH, designSpec, fftN);

        auto& target = force ? kA : (fading ? kP : kB);
        partitionKernel (designH, target);

        if (force)
        {
            fading = false;
            pendingValid = false;
        }
        else if (fading)
        {
            pendingValid = true;
        }
        else
        {
            fading = true;
            fadeBlock = 0;
        }
    }

    // Processa um quadro (uma amostra por canal). in/out podem ser o mesmo array.
    inline void processFrame (const float* in, float* out)
    {
        for (int c = 0; c < numCh; ++c)
        {
            ch[c].inBlock[(size_t) (B + pos)] = in[c];
            out[c] = ch[c].outBlock[(size_t) pos];
        }

        if (++pos == B)
        {
            pos = 0;

            const float w0 = fading ? (float) fadeBlock / (float) kFadeBlocks : 0.0f;
            const float w1 = fading ? (float) (fadeBlock + 1) / (float) kFadeBlocks : 0.0f;

            for (int c = 0; c < numCh; ++c)
                runBlock (ch[c], w0, w1);

            if (fading && ++fadeBlock >= kFadeBlocks)
            {
                std::swap (kA, kB);
                fading = false;

                if (pendingValid)
                {
                    std::swap (kB, kP);
                    pendingValid = false;
                    fading = true;
                    fadeBlock = 0;
                }
            }
        }
    }

    //==========================================================================
    // Projeto do kernel: bandpass = passa-altas * passa-baixas (curvas sigmoides
    // em dB/oitava, -6 dB nas bordas), normalizado para pico = 1.
    //==========================================================================
    static void designBandKernel (double fs, double fc, double widthOct, double slopeDb, int N,
                                  std::vector<double>& h,
                                  std::vector<std::complex<double>>& spec,
                                  FFTT<double>& fft)
    {
        const double fLo = fc * std::pow (2.0, -widthOct * 0.5);
        const double fHi = fc * std::pow (2.0,  widthOct * 0.5);
        const bool lfCut = fLo <= 20.0;       // borda inferior em 20 Hz => LF-Cut (vai ate DC)
        const bool hfCut = fHi >= 20000.0;    // borda superior em 20 kHz => HF-Cut

        const int half = N / 2;
        // Two passes avoid heap allocation when automating the filter.
        double peak = 0.0;

        auto sig = [] (double x)
        {
            x = std::max (-12.0, std::min (12.0, x));
            return 1.0 / (1.0 + std::pow (10.0, -x));
        };

        for (int k = 0; k <= half; ++k)
        {
            const double f = (double) k * fs / (double) N;
            double hp = 1.0, lp = 1.0;

            if (! lfCut)
                hp = (f <= 0.0) ? 0.0 : sig (slopeDb * std::log2 (f / fLo) / 20.0);

            if (! hfCut && f > 0.0)
                lp = sig (-slopeDb * std::log2 (f / fHi) / 20.0);

            peak = std::max (peak, hp * lp);
        }

        const double norm = (peak > 1.0e-4) ? std::min (8.0, 1.0 / peak) : 1.0;

        for (int k = 0; k <= half; ++k)
        {
            const double f = (double) k * fs / (double) N;
            const double hp = lfCut ? 1.0 : ((f <= 0.0) ? 0.0 : sig (slopeDb * std::log2 (f / fLo) / 20.0));
            const double lp = (hfCut || f <= 0.0) ? 1.0 : sig (-slopeDb * std::log2 (f / fHi) / 20.0);
            const double a = hp * lp * norm;
            spec[(size_t) k] = { a, 0.0 };
            if (k > 0 && k < half)
                spec[(size_t) (N - k)] = { a, 0.0 };
        }

        fft.transform (spec.data(), true);

        for (int n = 0; n < N; ++n)
        {
            const int idx = (n - half + N) % N;
            const double v = spec[(size_t) idx].real() / (double) N;
            const double w = 0.42 - 0.5 * std::cos (2.0 * kPi * n / N)
                                  + 0.08 * std::cos (4.0 * kPi * n / N);
            h[(size_t) n] = v * w;
        }
    }

private:
    struct Chan
    {
        std::vector<float> inBlock, outBlock;
        std::vector<std::complex<float>> fdl;
        int fdlPos = 0;
    };

    static constexpr int kFadeBlocks = 4;

    void partitionKernel (const std::vector<double>& h, std::vector<std::complex<float>>& dst)
    {
        for (int p = 0; p < P; ++p)
        {
            for (int i = 0; i < F; ++i)
                buf[(size_t) i] = (i < B) ? std::complex<float> ((float) h[(size_t) (p * B + i)], 0.0f)
                                          : std::complex<float>();

            fftF.transform (buf.data(), false);
            std::copy (buf.begin(), buf.end(), dst.begin() + (std::ptrdiff_t) p * F);
        }
    }

    void accumulate (const Chan& c, const std::vector<std::complex<float>>& k, std::vector<std::complex<float>>& dst) const
    {
        std::fill (dst.begin(), dst.end(), std::complex<float>());

        for (int p = 0; p < P; ++p)
        {
            const int slot = (c.fdlPos - p + P) % P;
            const std::complex<float>* x = c.fdl.data() + (size_t) slot * (size_t) F;
            const std::complex<float>* kk = k.data() + (size_t) p * (size_t) F;

            for (int i = 0; i < F; ++i)
            {
                const float xr = x[i].real(), xi = x[i].imag();
                const float kr = kk[i].real(), ki = kk[i].imag();
                dst[(size_t) i] += std::complex<float> (xr * kr - xi * ki, xr * ki + xi * kr);
            }
        }
    }

    void runBlock (Chan& c, float w0, float w1)
    {
        for (int i = 0; i < F; ++i)
            buf[(size_t) i] = std::complex<float> (c.inBlock[(size_t) i], 0.0f);

        fftF.transform (buf.data(), false);
        std::copy (buf.begin(), buf.end(), c.fdl.begin() + (std::ptrdiff_t) c.fdlPos * F);

        accumulate (c, kA, acc);
        fftF.transform (acc.data(), true);
        const float scale = 1.0f / (float) F;

        if (fading)
        {
            accumulate (c, kB, accB);
            fftF.transform (accB.data(), true);

            for (int i = 0; i < B; ++i)
            {
                const float w = w0 + (w1 - w0) * ((float) (i + 1) / (float) B);
                const float a = acc[(size_t) (B + i)].real() * scale;
                const float b = accB[(size_t) (B + i)].real() * scale;
                c.outBlock[(size_t) i] = a + (b - a) * w;
            }
        }
        else
        {
            for (int i = 0; i < B; ++i)
                c.outBlock[(size_t) i] = acc[(size_t) (B + i)].real() * scale;
        }

        // desloca a janela de entrada
        std::memmove (c.inBlock.data(), c.inBlock.data() + B, sizeof (float) * (size_t) B);
        c.fdlPos = (c.fdlPos + 1) % P;
    }

    double fs = 48000.0;
    int numCh = 2, N = 2048, B = 128, P = 16, F = 256;
    int pos = 0;
    bool fading = false, pendingValid = false;
    int fadeBlock = 0;
    double currentFc = -1.0, currentW = 0.0, currentS = 0.0;

    FFTT<float> fftF;
    FFTT<double> fftN;
    Chan ch[2];
    std::vector<std::complex<float>> kA, kB, kP, buf, acc, accB;
    std::vector<std::complex<double>> designSpec;
    std::vector<double> designH;
};

//==============================================================================
//  Parametros do de-esser
//==============================================================================
struct DesserParams
{
    float inTrimDb   = 0.0f;
    float outTrimDb  = 0.0f;

    float freq       = 7000.0f;   // Hz
    float width      = 1.4f;      // oitavas
    float slopeDb    = 72.0f;     // dB/oitava

    int   listen     = 0;         // 0 = MIX, 1 = INSIDE, 2 = OUTSIDE

    float thresholdDb = -18.0f;   // 0 .. -84
    bool  autoTrack   = true;
    float dampingS    = 1.0f;

    float ratioDeg    = 26.565f;  // 45 = 1:1, 0 = limitador, <0 = cut
    float makeupDb    = 0.0f;
    float wet         = 1.0f;     // 0..1
    float kneeDb      = 0.0f;

    float attackMs    = 5.2f;
    float holdS       = 0.01f;
    float releaseS    = 0.127f;

    bool  trigWide    = false;    // false = BAND, true = WIDE
    bool  audioWide   = false;
    bool  effectIn    = true;
};

//==============================================================================
//  Nucleo do de-esser
//==============================================================================
class DesserCore
{
public:
    static constexpr int kScopeSize = 16384;

    void prepare (double sampleRate, int channels)
    {
        fs = sampleRate;
        numCh = std::max (1, std::min (2, channels));
        splitter.prepare (fs, numCh);

        lookahead = (int) std::lround (0.002 * fs);
        latency = splitter.getLatency() + lookahead;
        dryDelayTap = splitter.getLatency();

        for (int c = 0; c < 2; ++c)
        {
            dry[c].prepare (latency + 8);
            bpLine[c].prepare (lookahead + 8);
        }

        for (auto& v : scopeIn) v.store (0.0f);
        for (auto& v : scopeBp) v.store (0.0f);
        scopePos.store (0);

        smoothA = 1.0 - std::exp (-1.0 / (0.005 * fs));
        reset();
        setParams (params, true);
    }

    void reset()
    {
        splitter.reset();
        for (int c = 0; c < 2; ++c) { dry[c].reset(); bpLine[c].reset(); pDet[c] = 0.0; pTrk[c] = 0.0; }
        grS = 0.0; holdCount = 0;
        trackDb = -60.0; trackerInit = false; belowCount = 0;
        wetS = params.wet; makeupS = dbToGainD (params.makeupDb);
        inG = dbToGain (params.inTrimDb);
        outG = dbToGain (params.outTrimDb);
        eMix = params.effectIn ? 1.0f : 0.0f;
        updateListenTargets();
        rW = rWt; bW = bWt; wideMix = wideMixT;
        lastGrDb = 0.0f; lastThreshEff = -18.0f;
    }

    int getLatencySamples() const { return latency; }

    void setParams (const DesserParams& p, bool force = false)
    {
        params = p;

        // coeficientes
        // constante de tempo do detector: curta p/ agudos (sibilancia), mais longa p/ banda grave;
        // no trigger WIDE o sinal e de banda larga, entao usa uma janela fixa de 5 ms
        const double detTau = p.trigWide ? 0.005
                                         : std::max (0.0003, std::min (0.015, 1.5 / std::max (20.0, (double) p.freq)));
        detA = 1.0 - std::exp (-1.0 / (detTau * fs));
        trkA = 1.0 - std::exp (-1.0 / (0.010 * fs));

        attA = 1.0 - std::exp (-2.2 / std::max (1.0, (double) p.attackMs * 0.001 * fs));
        relA = 1.0 - std::exp (-2.2 / std::max (1.0, (double) p.releaseS * fs));
        holdSamples = (int) std::lround ((double) p.holdS * fs);

        slopeS = std::tan ((double) p.ratioDeg * kPi / 180.0);
        makeupLin = dbToGainD ((double) p.makeupDb);

        updateListenTargets();
        inGt = dbToGain (p.inTrimDb);
        outGt = dbToGain (p.outTrimDb);
        eMixT = p.effectIn ? 1.0f : 0.0f;

        const bool bandChanged = std::abs (p.freq - lastFreq) > 1e-3f * lastFreq
                              || std::abs (p.width - lastWidth) > 1e-4f
                              || std::abs (p.slopeDb - lastSlope) > 1e-3f;

        if (force)
        {
            splitter.setBand (p.freq, p.width, p.slopeDb, true);
            lastFreq = p.freq; lastWidth = p.width; lastSlope = p.slopeDb;
            bandDirty = false;
        }
        else if (bandChanged)
        {
            bandDirty = true;
        }

        if (force)
        {
            wetS = p.wet; makeupS = makeupLin;
            inG = inGt; outG = outGt; eMix = eMixT;
            rW = rWt; bW = bWt; wideMix = wideMixT;
        }
    }

    // Processa em-lugar. numChannels <= 2.
    void process (float* const* data, int numChannels, int numSamples)
    {
        const int nc = std::min (numChannels, numCh);

        if (nc <= 0 || numSamples <= 0) return;
        samplesSinceDesign = std::min (samplesSinceDesign, 128) + std::min (numSamples, 128);
        if (bandDirty && samplesSinceDesign >= 128)
        {
            splitter.setBand (params.freq, params.width, params.slopeDb);
            lastFreq = params.freq; lastWidth = params.width; lastSlope = params.slopeDb;
            bandDirty = false;
            samplesSinceDesign = 0;
        }

        float peakIn = 0.0f, peakOut = 0.0f, grMax = 0.0f;
        const double thr = (double) params.thresholdDb;
        const double knee = (double) params.kneeDb;
        const bool autoT = params.autoTrack;
        const bool trigWide = params.trigWide;

        const int D = dryDelayTap;
        const int L = lookahead;

        float x[2] = { 0, 0 }, bp[2] = { 0, 0 };

        for (int i = 0; i < numSamples; ++i)
        {
            wetS += (params.wet - wetS) * (float) smoothA;
            makeupS += (makeupLin - makeupS) * smoothA;
            inG   += (inGt  - inG)  * (float) smoothA;
            outG  += (outGt - outG) * (float) smoothA;
            eMix  += (eMixT - eMix) * (float) smoothA;
            rW    += (rWt   - rW)   * (float) smoothA;
            bW    += (bWt   - bW)   * (float) smoothA;
            wideMix += (wideMixT - wideMix) * (float) smoothA;

            for (int c = 0; c < nc; ++c)
            {
                x[c] = data[c][i] * inG;
                peakIn = std::max (peakIn, std::abs (x[c]));
            }
            for (int c = nc; c < 2; ++c) x[c] = x[0];

            splitter.processFrame (x, bp);

            for (int c = 0; c < nc; ++c)
            {
                dry[c].push (x[c]);
                bpLine[c].push (bp[c]);
            }

            // ---------------- deteccao ----------------
            double mDet = 0.0, sTrk = 0.0;
            for (int c = 0; c < nc; ++c)
            {
                const float al  = dry[c].get (D);
                const float rj  = al - bp[c];
                const float trig = trigWide ? al : bp[c];
                const float trk  = trigWide ? al : rj;

                pDet[c] += ((double) trig * trig - pDet[c]) * detA;
                pTrk[c] += ((double) trk * trk - pTrk[c]) * trkA;

                mDet = std::max (mDet, pDet[c]);
                sTrk += pTrk[c];
            }
            sTrk /= (double) nc;

            const double lDet = 10.0 * std::log10 (mDet + 1.0e-12);
            const double lTrk = 10.0 * std::log10 (sTrk + 1.0e-12);

            if (autoT) updateTracker (lTrk);
            const double ref = autoT ? trackDb : 0.0;

            // ---------------- computador de ganho ----------------
            const double over = (lDet - ref) - thr;
            double gr = 0.0;
            const double oneMinusS = 1.0 - slopeS;

            if (knee <= 0.0)
                gr = over > 0.0 ? oneMinusS * over : 0.0;
            else if (2.0 * over < -knee)
                gr = 0.0;
            else if (2.0 * std::abs (over) <= knee)
                gr = oneMinusS * (over + knee * 0.5) * (over + knee * 0.5) / (2.0 * knee);
            else
                gr = oneMinusS * over;

            gr = std::max (0.0, std::min (60.0, gr));

            // ---------------- envelope de reacao ----------------
            if (gr > grS)
            {
                grS += (gr - grS) * attA;
                holdCount = holdSamples;
            }
            else if (holdCount > 0)
            {
                --holdCount;
            }
            else
            {
                grS += (gr - grS) * relA;
            }

            const float gLin = (float) (std::pow (10.0, -grS * 0.05) * makeupS);
            const float gEff = 1.0f + wetS * (gLin - 1.0f);
            grMax = std::max (grMax, (float) grS);

            // ---------------- montagem da saida ----------------
            float mono = 0.0f, monoBp = 0.0f;
            for (int c = 0; c < nc; ++c)
            {
                const float dr = dry[c].get (D + L);
                const float bd = bpLine[c].get (L);
                const float rj = dr - bd;

                const float bandProc = bd * gEff;
                const float bandOut = rW * rj + bW * bandProc;
                const float wideOut = dr * gEff;

                const float proc = bandOut + wideMix * (wideOut - bandOut);
                float y = dr + eMix * (proc - dr);
                y *= outG;

                data[c][i] = y;
                peakOut = std::max (peakOut, std::abs (y));

                mono += x[c];
                monoBp += bp[c];
            }

            const int sp = scopePos.load (std::memory_order_relaxed);
            scopeIn[(size_t) sp].store (mono / (float) nc, std::memory_order_relaxed);
            scopeBp[(size_t) sp].store (monoBp / (float) nc, std::memory_order_relaxed);
            scopePos.store ((sp + 1) & (kScopeSize - 1), std::memory_order_release);
        }

        lastPeakIn  = peakIn;
        lastPeakOut = peakOut;
        lastGrDb    = params.effectIn ? grMax : 0.0f;
        lastThreshEff = (float) (trackDb + thr);
    }

    // leitura para a interface (via atomics no processor)
    float getPeakIn() const        { return lastPeakIn; }
    float getPeakOut() const       { return lastPeakOut; }
    float getGrDb() const          { return lastGrDb; }
    float getTrackDb() const       { return (float) (params.autoTrack ? trackDb : 0.0); }

    // buffers de osciloscopio para o analisador espectral
    int copyScope (bool bandpass, float* dst, int num) const
    {
        num = std::max (0, std::min (num, kScopeSize));
        const auto& src = bandpass ? scopeBp : scopeIn;
        int p = scopePos.load (std::memory_order_acquire);
        int start = (p - num + kScopeSize * 2) & (kScopeSize - 1);
        for (int i = 0; i < num; ++i)
            dst[i] = src[(size_t) ((start + i) & (kScopeSize - 1))].load (std::memory_order_relaxed);
        return num;
    }

private:
    void updateListenTargets()
    {
        const auto& p = params;
        switch (p.listen)
        {
            case 1:  rWt = 0.0f; bWt = 1.0f; break; // INSIDE
            case 2:  rWt = 1.0f; bWt = 0.0f; break; // OUTSIDE
            default: rWt = 1.0f; bWt = 1.0f; break; // MIX
        }
        wideMixT = p.audioWide ? 1.0f : 0.0f;
    }

    // Level tracking: janela de 24 dB, sobe rapido / desce devagar
    void updateTracker (double lTrk)
    {
        const double damp = std::max (0.1, (double) params.dampingS);

        if (lTrk < -85.0) return;                 // silencio digital: ignora

        if (! trackerInit)
        {
            trackDb = lTrk;
            trackerInit = true;
            belowCount = 0;
            return;
        }

        const double aSlow = 1.0 - std::exp (-1.0 / (damp * fs));
        const double aFast = 1.0 - std::exp (-1.0 / (std::max (0.005, damp * 0.1) * fs));

        if (lTrk > trackDb)
        {
            trackDb += (lTrk - trackDb) * aFast;
            belowCount = 0;
        }
        else if (lTrk > trackDb - 24.0)
        {
            trackDb += (lTrk - trackDb) * aSlow;
            belowCount = 0;
        }
        else
        {
            // abaixo da janela: pausa/silencio, nao puxa o threshold...
            // ...a menos que dure muito (mudanca real de nivel)
            const long timeout = (long) ((2.0 + 6.0 * damp) * fs);
            if (++belowCount > timeout)
                trackDb += (lTrk - trackDb) * aSlow;
        }

        trackDb = std::max (-90.0, std::min (6.0, trackDb));
    }

    double fs = 48000.0;
    int numCh = 2;
    int lookahead = 96, latency = 0, dryDelayTap = 0;

    FirBandSplitter splitter;
    DelayLine dry[2], bpLine[2];
    DesserParams params;

    double detA = 0.1, trkA = 0.001, attA = 0.01, relA = 0.001, smoothA = 0.01;
    int holdSamples = 0, holdCount = 0;
    double slopeS = 0.5, makeupLin = 1.0;
    double pDet[2] = { 0, 0 }, pTrk[2] = { 0, 0 };
    double grS = 0.0;
    double trackDb = -60.0;
    bool trackerInit = false;
    long belowCount = 0;

    float inG = 1.0f, inGt = 1.0f, outG = 1.0f, outGt = 1.0f, eMix = 1.0f, eMixT = 1.0f;
    float rW = 1.0f, rWt = 1.0f, bW = 1.0f, bWt = 1.0f, wideMix = 0.0f, wideMixT = 0.0f;

    float lastFreq = -1.0f, lastWidth = -1.0f, lastSlope = -1.0f;
    bool bandDirty = false;
    int samplesSinceDesign = 1000;

    float lastPeakIn = 0.0f, lastPeakOut = 0.0f, lastGrDb = 0.0f, lastThreshEff = -18.0f;

    static_assert (std::atomic<float>::is_always_lock_free, "Scope requires lock-free floats");
    std::array<std::atomic<float>, kScopeSize> scopeIn {}, scopeBp {};
    float wetS = 1.0f;
    double makeupS = 1.0;
    std::atomic<int> scopePos { 0 };
};

} // namespace silver
