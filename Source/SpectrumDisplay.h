#pragma once
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"
#include "SilverTheme.h"

//==============================================================================
//  Display principal: analisador de espectro, banda arrastavel, threshold,
//  fader de threshold (direita), controle de frequencia (barra inferior) e zoom.
//==============================================================================
class SpectrumDisplay : public juce::Component
{
public:
    explicit SpectrumDisplay (SilverDesserProcessor& p)
        : proc (p),
          pFreq  (*p.apvts.getParameter (ids::freq)),
          pWidth (*p.apvts.getParameter (ids::width)),
          pThr   (*p.apvts.getParameter (ids::threshold)),
          aFreq  (pFreq,  [] (float) {}),
          aWidth (pWidth, [] (float) {}),
          aThr   (pThr,   [] (float) {}),
          fft (kFftOrder),
          window ((size_t) kFftSize, juce::dsp::WindowingFunction<float>::hann)
    {
        specIn.assign ((size_t) kBins, -120.0f);
        specBp.assign ((size_t) kBins, -120.0f);
        fftData.assign ((size_t) kFftSize * 2, 0.0f);
        setOpaque (true);
    }

    // ---- chamado pelo editor (timer ~30 Hz) ----
    void setLive (float grDb, float trackDb) { gr = grDb; track = trackDb; }

    void updateAnalysis()
    {
        const double fs = proc.currentSampleRate.load();
        binHz = (float) (fs / (double) kFftSize);
        analyse (false, specIn);
        analyse (true, specBp);

        // frequencia do pico dentro da banda
        const float fc = freqVal(), w = widthVal();
        const float fLo = fc * std::pow (2.0f, -w * 0.5f), fHi = fc * std::pow (2.0f, w * 0.5f);
        const int b0 = juce::jlimit (1, kBins - 2, (int) std::ceil (fLo / binHz));
        const int b1 = juce::jlimit (1, kBins - 2, (int) std::floor (fHi / binHz));
        float best = -200.0f; int bi = -1;
        for (int i = b0; i <= b1; ++i)
            if (specBp[(size_t) i] > best) { best = specBp[(size_t) i]; bi = i; }

        if (bi > 0 && best > -88.0f)
        {
            // interpolacao parabolica
            const float a = specBp[(size_t) bi - 1], b = specBp[(size_t) bi], c = specBp[(size_t) bi + 1];
            const float den = a - 2.0f * b + c;
            const float off = std::abs (den) > 1e-6f ? 0.5f * (a - c) / den : 0.0f;
            const float f = ((float) bi + juce::jlimit (-0.5f, 0.5f, off)) * binHz;
            peakFreq = peakFreq <= 0.0f ? f : peakFreq + (f - peakFreq) * 0.25f;
            peakDb = best;
        }
        else
        {
            peakFreq = 0.0f;
        }
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        const float fc = freqVal(), w = widthVal(), slope = slopeVal(), thr = thrVal();
        const float fLo = fc * std::pow (2.0f, -w * 0.5f), fHi = fc * std::pow (2.0f, w * 0.5f);

        // fundo
        g.fillAll (theme::bgBottom);
        auto plotFull = juce::Rectangle<float> (plotL, 2.0f, plotW, dBBottomY - 2.0f);
        g.setGradientFill (juce::ColourGradient (theme::dispTop, 0, plotFull.getY(), theme::dispBottom, 0, plotFull.getBottom(), false));
        g.fillRoundedRectangle (plotFull, 3.0f);
        g.setColour (theme::stripBg);
        g.fillRect (juce::Rectangle<float> (plotL, 2.0f, plotW, dB0Y - 2.0f));
        g.setColour (theme::panelBorder);
        g.drawRoundedRectangle (plotFull, 3.0f, 1.0f);

        drawGrid (g);

        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (plotFull.toNearestInt());

            // espectro de entrada
            juce::Path p;
            buildPath (specIn, p);
            juce::Path fill (p);
            fill.lineTo (plotR, dBBottomY);
            fill.lineTo (plotL, dBBottomY);
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (theme::spectrumFill.withAlpha (0.75f), 0, dB0Y,
                                                     theme::spectrumFill.withAlpha (0.35f), 0, dBBottomY, false));
            g.fillPath (fill);
            g.setColour (theme::spectrumLine.withAlpha (0.8f));
            g.strokePath (p, juce::PathStrokeType (1.0f));

            // sidechain (banda)
            juce::Path pb;
            buildPath (specBp, pb);
            pb.lineTo (plotR, dBBottomY);
            pb.lineTo (plotL, dBBottomY);
            pb.closeSubPath();
            g.setColour (theme::sideFill.withAlpha (0.55f));
            g.fillPath (pb);

            const float xLo = freqToX (fLo), xHi = freqToX (fHi), xc = freqToX (fc);

            // curva do filtro (mostra largura e slope)
            {
                juce::Path curve;
                const double norm = bandNorm (fLo, fHi, slope);
                for (int x = (int) plotL; x <= (int) plotR; ++x)
                {
                    const double f = xToFreq ((float) x);
                    const double a = bandResp (f, fLo, fHi, slope) * norm;
                    const float db = (float) juce::jmax (-100.0, 20.0 * std::log10 (a + 1.0e-6));
                    const float y = dbToY (db);
                    if (x == (int) plotL) curve.startNewSubPath ((float) x, y); else curve.lineTo ((float) x, y);
                }
                g.setColour (theme::bandLine.withAlpha (0.55f));
                g.strokePath (curve, juce::PathStrokeType (1.2f));
            }

            // reducao de ganho: caixa pendurada no threshold efetivo + linha vermelha
            const float eff = autoOn() ? track + thr : thr;
            const float yEff = dbToY (eff);
            if (gr > 0.05f && fx())
            {
                const float pxPerDb = (dBBottomY - dB0Y) / 95.0f;
                auto box = juce::Rectangle<float> (xLo, yEff, xHi - xLo, gr * pxPerDb);
                g.setColour (theme::reduction.withAlpha (0.28f));
                g.fillRect (box);
                g.setColour (theme::reduction);
                g.drawRect (box, 1.0f);

                const float yGr = dbToY (-gr);
                g.drawLine (xLo, yGr, xHi, yGr, 1.5f);
                g.setFont (theme::font (13.0f, true));
                g.drawText ("-" + juce::String (gr, 1) + " dB", juce::Rectangle<float> (xLo - 74.0f, yGr - 9.0f, 70.0f, 18.0f),
                            juce::Justification::centredRight, false);
            }

            // threshold: linha efetiva tracejada (auto) e linha do ajuste
            const float yThr = dbToY (thr);
            if (autoOn() && std::abs (yEff - yThr) > 1.5f)
            {
                const float dashes[] = { 5.0f, 4.0f };
                g.setColour (theme::thresholdEff);
                g.drawDashedLine (juce::Line<float> (plotL, yEff, plotR, yEff), dashes, 2, 1.2f);
            }
            g.setColour (theme::threshold);
            g.drawLine (plotL, yThr, plotR, yThr, 1.6f);
            g.setFont (theme::font (13.0f, true));
            g.drawText ("THRESHOLD", juce::Rectangle<float> (plotR - 150.0f, yThr - 18.0f, 146.0f, 16.0f), juce::Justification::centredRight, false);
            g.drawText (pThr.getCurrentValueAsText(), juce::Rectangle<float> (plotR - 150.0f, yThr + 1.0f, 146.0f, 16.0f), juce::Justification::centredRight, false);

            // bordas da banda
            g.setColour (theme::bandLine);
            g.drawLine (xLo, 3.0f, xLo, dBBottomY, 1.4f);
            g.drawLine (xHi, 3.0f, xHi, dBBottomY, 1.4f);

            // pico de frequencia detectado
            if (peakFreq > 0.0f)
            {
                const float xp = freqToX (peakFreq);
                g.setColour (theme::reduction);
                g.drawLine (xp, dB0Y, xp, dBBottomY, 1.6f);
                auto tag = juce::Rectangle<float> (xp - 30.0f, dBBottomY - 24.0f, 60.0f, 17.0f);
                if (tag.getRight() > plotR) tag.setX (plotR - 60.0f);
                g.setColour (juce::Colour (0xffffffff).withAlpha (0.85f));
                g.fillRoundedRectangle (tag, 3.0f);
                g.setColour (theme::reduction);
                g.setFont (theme::font (12.5f, true));
                g.drawText (juce::String (juce::roundToInt (peakFreq)) + " Hz", tag, juce::Justification::centred, false);
            }

            // marcadores no topo
            drawEdgeTag (g, xLo, fLo);
            drawEdgeTag (g, xHi, fHi);
            drawCenterMarker (g, xc);
        }

        drawAxes (g);
        drawFreqSlider (g, fc);
        drawThresholdFader (g, thr);
        drawMagnifier (g);
    }

    //==========================================================================
    void mouseMove (const juce::MouseEvent& e) override
    {
        switch (hitTest (e.position))
        {
            case Hit::EdgeLo: case Hit::EdgeHi:        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
            case Hit::Threshold: case Hit::Fader:      setMouseCursor (juce::MouseCursor::UpDownResizeCursor); break;
            case Hit::Band: case Hit::Slider:          setMouseCursor (juce::MouseCursor::DraggingHandCursor); break;
            case Hit::Magnifier:                       setMouseCursor (juce::MouseCursor::PointingHandCursor); break;
            default:                                   setMouseCursor (juce::MouseCursor::NormalCursor); break;
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        drag = hitTest (e.position);

        if (drag == Hit::Magnifier)
        {
            toggleZoom();
            drag = Hit::None;
            return;
        }
        if (drag == Hit::None) return;

        if (drag == Hit::Plot) drag = Hit::Band;   // clique fora da banda: leva a banda ate la

        if (drag == Hit::Band)
        {
            const float xc = freqToX (freqVal());
            const float xLo = freqToX (freqVal() * std::pow (2.0f, -widthVal() * 0.5f));
            const float xHi = freqToX (freqVal() * std::pow (2.0f,  widthVal() * 0.5f));
            grabOffset = (e.position.x >= xLo && e.position.x <= xHi) ? e.position.x - xc : 0.0f;
        }

        if (e.mods.isAltDown() || e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            resetFor (drag);
            drag = Hit::None;
            return;
        }

        begin (drag);
        applyDrag (e.position);
    }

    void mouseDrag (const juce::MouseEvent& e) override { if (drag != Hit::None) applyDrag (e.position); }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag != Hit::None) end (drag);
        drag = Hit::None;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto h = hitTest (e.position);
        if (h == Hit::Threshold || h == Hit::Fader) resetFor (Hit::Threshold);
        else if (h == Hit::Slider) resetFor (Hit::Slider);
        else if (h == Hit::EdgeLo || h == Hit::EdgeHi) resetFor (Hit::EdgeLo);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.position.x >= plotL && e.position.x <= plotR)
        {
            const float nw = juce::jlimit (0.2f, 10.0f, widthVal() * std::exp (w.deltaY * 0.8f));
            aWidth.setValueAsCompleteGesture (nw);
        }
    }

private:
    enum class Hit { None, Plot, Band, EdgeLo, EdgeHi, Threshold, Fader, Slider, Magnifier };

    static constexpr int kFftOrder = 12;
    static constexpr int kFftSize = 1 << kFftOrder;
    static constexpr int kBins = kFftSize / 2 + 1;

    // geometria local
    static constexpr float plotL = 38.0f, plotW = 596.0f, plotR = plotL + plotW;
    static constexpr float dB0Y = 39.0f, dBBottomY = 488.0f;
    static constexpr float sliderY = 524.0f, sliderH = 24.0f;
    static constexpr float faderX = 688.0f;

    //---- valores de parametros
    float freqVal()  const { return pFreq.convertFrom0to1 (pFreq.getValue()); }
    float widthVal() const { return pWidth.convertFrom0to1 (pWidth.getValue()); }
    float thrVal()   const { return pThr.convertFrom0to1 (pThr.getValue()); }
    float slopeVal() const { return proc.apvts.getRawParameterValue (ids::slope)->load(); }
    bool  autoOn()   const { return proc.apvts.getRawParameterValue (ids::autoTrack)->load() > 0.5f; }
    bool  fx()       const { return proc.apvts.getRawParameterValue (ids::effectIn)->load() > 0.5f; }

    //---- mapeamentos
    float dbToY (float db) const { return dB0Y + (-db) / 95.0f * (dBBottomY - dB0Y); }
    float yToDb (float y) const  { return -(y - dB0Y) / (dBBottomY - dB0Y) * 95.0f; }

    float freqToX (float f) const
    {
        const float t = std::log (juce::jmax (1.0f, f) / visLo) / std::log (visHi / visLo);
        return plotL + t * plotW;
    }
    float xToFreq (float x) const
    {
        return visLo * std::pow (visHi / visLo, (x - plotL) / plotW);
    }
    // barra inferior: sempre 20 Hz .. 20 kHz
    float sliderFreqToX (float f) const { return plotL + std::log (f / 20.0f) / std::log (1000.0f) * plotW; }
    float sliderXToFreq (float x) const { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - plotL) / plotW)); }

    //---- resposta do filtro (mesma formula do DSP)
    static double sig (double x) { x = juce::jlimit (-12.0, 12.0, x); return 1.0 / (1.0 + std::pow (10.0, -x)); }

    static double bandResp (double f, double fLo, double fHi, double s)
    {
        double hp = 1.0, lp = 1.0;
        if (fLo > 20.0)    hp = sig (s * std::log2 (f / fLo) / 20.0);
        if (fHi < 20000.0) lp = sig (-s * std::log2 (f / fHi) / 20.0);
        return hp * lp;
    }

    static double bandNorm (double fLo, double fHi, double s)
    {
        double peak = 0.0;
        for (int i = 0; i <= 240; ++i)
            peak = juce::jmax (peak, bandResp (20.0 * std::pow (1000.0, i / 240.0), fLo, fHi, s));
        return peak > 1.0e-4 ? juce::jmin (8.0, 1.0 / peak) : 1.0;
    }

    //---- analise
    void analyse (bool bandpass, std::vector<float>& sm)
    {
        proc.copyScope (bandpass, fftData.data(), kFftSize);
        window.multiplyWithWindowingTable (fftData.data(), (size_t) kFftSize);
        std::fill (fftData.begin() + kFftSize, fftData.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        const float norm = 4.0f / (float) kFftSize;
        for (int k = 0; k < kBins; ++k)
        {
            const float db = juce::jmax (-120.0f, 20.0f * std::log10 (fftData[(size_t) k] * norm + 1.0e-9f));
            float& s = sm[(size_t) k];
            s += (db - s) * (db > s ? 0.75f : 0.18f);
        }
    }

    void buildPath (const std::vector<float>& sm, juce::Path& path) const
    {
        const int x0 = (int) plotL, x1 = (int) plotR;
        bool started = false;

        for (int x = x0; x <= x1; ++x)
        {
            const float ba = xToFreq ((float) x - 0.5f) / binHz;
            const float bb = xToFreq ((float) x + 0.5f) / binHz;
            float db;

            if (bb - ba < 1.0f)
            {
                const float b = 0.5f * (ba + bb);
                int i = juce::jlimit (0, kBins - 2, (int) std::floor (b));
                const float fr = juce::jlimit (0.0f, 1.0f, b - (float) i);
                db = sm[(size_t) i] * (1.0f - fr) + sm[(size_t) i + 1] * fr;
            }
            else
            {
                const int i0 = juce::jlimit (0, kBins - 1, (int) std::ceil (ba));
                const int i1 = juce::jlimit (0, kBins - 1, (int) std::floor (bb));
                db = -200.0f;
                for (int i = i0; i <= i1; ++i) db = juce::jmax (db, sm[(size_t) i]);
            }

            const float y = juce::jlimit (dB0Y, dBBottomY, dbToY (db));
            if (! started) { path.startNewSubPath ((float) x, y); started = true; }
            else           path.lineTo ((float) x, y);
        }
    }

    //---- desenho
    void drawGrid (juce::Graphics& g) const
    {
        g.setColour (theme::gridLine.withAlpha (0.55f));
        for (int db = 0; db >= -95; db -= 5)
        {
            const float y = dbToY ((float) db);
            g.drawLine (plotL, y, plotR, y, (db % 10 == 0) ? 1.0f : 0.5f);
        }
        static const float fr[] = { 40, 60, 100, 200, 400, 600, 1000, 2000, 3000, 4000, 6000, 8000, 10000, 20000 };
        for (float f : fr)
        {
            if (f < visLo * 0.999f || f > visHi * 1.001f) continue;
            const float x = freqToX (f);
            g.drawLine (x, dB0Y, x, dBBottomY, 0.6f);
        }
    }

    void drawAxes (juce::Graphics& g) const
    {
        g.setColour (theme::gridText);
        g.setFont (theme::font (12.5f, true));

        for (int db = 0; db >= -95; db -= 5)
        {
            const float y = dbToY ((float) db);
            g.drawText (juce::String (db), juce::Rectangle<float> (0.0f, y - 8.0f, plotL - 4.0f, 16.0f), juce::Justification::centredRight, false);
            g.drawText (juce::String (db), juce::Rectangle<float> (plotR + 4.0f, y - 8.0f, 30.0f, 16.0f), juce::Justification::centredLeft, false);
        }

        static const struct { float f; const char* t; } lab[] = {
            { 40, "40" }, { 60, "60" }, { 100, "100" }, { 200, "200" }, { 400, "400" }, { 600, "600" }, { 1000, "1K" },
            { 2000, "2K" }, { 3000, "3K" }, { 4000, "4K" }, { 6000, "6K" }, { 8000, "8K" }, { 10000, "10K" }, { 20000, "20K" } };
        for (auto& l : lab)
        {
            if (l.f < visLo * 0.999f || l.f > visHi * 1.001f) continue;
            g.drawText (l.t, juce::Rectangle<float> (freqToX (l.f) - 20.0f, dBBottomY + 4.0f, 40.0f, 16.0f), juce::Justification::centred, false);
        }
    }

    static juce::String fmtFreq (float f)
    {
        return f >= 1000.0f ? juce::String (f / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
    }

    void drawEdgeTag (juce::Graphics& g, float x, float f) const
    {
        const float xc = juce::jlimit (plotL + 28.0f, plotR - 28.0f, x);
        auto tag = juce::Rectangle<float> (xc - 30.0f, 4.0f, 60.0f, 15.0f);
        g.setColour (juce::Colour (0xffffe9a6));
        g.fillRoundedRectangle (tag, 3.0f);
        g.setColour (theme::marker);
        g.drawRoundedRectangle (tag, 3.0f, 1.0f);
        g.setColour (theme::navy);
        g.setFont (theme::font (12.0f, true));
        g.drawText (fmtFreq (f), tag, juce::Justification::centred, false);

        // setas laranja
        juce::Path arrows;
        const float y = 28.0f;
        arrows.addTriangle (x - 4.0f, y, x - 11.0f, y - 4.5f, x - 11.0f, y + 4.5f);
        arrows.addTriangle (x + 4.0f, y, x + 11.0f, y - 4.5f, x + 11.0f, y + 4.5f);
        g.setColour (theme::marker);
        g.fillPath (arrows);
    }

    void drawCenterMarker (juce::Graphics& g, float x) const
    {
        juce::Path p;
        const float y = 28.0f;
        p.addTriangle (x - 7.0f, y - 6.0f, x + 7.0f, y - 6.0f, x, y + 1.0f);
        p.addTriangle (x - 7.0f, y + 8.0f, x + 7.0f, y + 8.0f, x, y + 1.0f);
        g.setColour (theme::marker);
        g.fillPath (p);
    }

    void drawFreqSlider (juce::Graphics& g, float fc) const
    {
        auto track = juce::Rectangle<float> (plotL, sliderY + 6.0f, plotW, 12.0f);
        g.setColour (juce::Colour (0xff8995aa));
        g.fillRoundedRectangle (track, 5.0f);
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.drawRoundedRectangle (track, 5.0f, 1.0f);

        const float x = sliderFreqToX (juce::jlimit (20.0f, 20000.0f, fc));
        theme::drawMetalHandle (g, juce::Rectangle<float> (x - 24.0f, sliderY, 48.0f, sliderH), false);
    }

    void drawThresholdFader (juce::Graphics& g, float thr) const
    {
        const float top = dbToY (0.0f), bot = dbToY (-84.0f);
        g.setColour (juce::Colour (0xff8995aa));
        g.fillRoundedRectangle (faderX - 3.0f, top, 6.0f, bot - top, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.drawRoundedRectangle (faderX - 3.0f, top, 6.0f, bot - top, 3.0f, 1.0f);

        const float y = dbToY (thr);
        theme::drawMetalHandle (g, juce::Rectangle<float> (faderX - 17.0f, y - 30.0f, 34.0f, 60.0f), true);
    }

    juce::Rectangle<float> magnifierRect() const { return { 668.0f, sliderY - 1.0f, 28.0f, 26.0f }; }

    void drawMagnifier (juce::Graphics& g) const
    {
        auto r = magnifierRect();
        g.setGradientFill (juce::ColourGradient (zoomOn ? juce::Colour (0xff9fb6dd) : juce::Colour (0xfff3f5f8), r.getX(), r.getY(),
                                                 zoomOn ? juce::Colour (0xff6f8bbd) : juce::Colour (0xffb7bfcc), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (theme::boxBorder);
        g.drawRoundedRectangle (r, 4.0f, 1.2f);
        g.setColour (theme::navy);
        const auto c = r.getCentre().translated (-2.0f, -2.0f);
        g.drawEllipse (c.x - 6.0f, c.y - 6.0f, 12.0f, 12.0f, 2.0f);
        g.drawLine (c.x + 4.5f, c.y + 4.5f, c.x + 10.0f, c.y + 10.0f, 2.6f);
    }

    //---- interacao
    Hit hitTest (juce::Point<float> p) const
    {
        if (magnifierRect().contains (p)) return Hit::Magnifier;
        if (p.y >= sliderY - 6.0f && p.y <= sliderY + sliderH + 6.0f && p.x >= plotL - 20.0f && p.x <= plotR + 20.0f) return Hit::Slider;
        if (p.x >= plotR + 34.0f && p.y <= dBBottomY + 10.0f) return Hit::Fader;

        if (p.x >= plotL && p.x <= plotR && p.y >= 0.0f && p.y <= dBBottomY + 4.0f)
        {
            const float fc = freqVal(), w = widthVal();
            const float xLo = freqToX (fc * std::pow (2.0f, -w * 0.5f));
            const float xHi = freqToX (fc * std::pow (2.0f,  w * 0.5f));
            const float yThr = dbToY (thrVal());

            if (std::abs (p.y - yThr) <= 6.0f && p.y > dB0Y) return Hit::Threshold;
            const float dLo = std::abs (p.x - xLo), dHi = std::abs (p.x - xHi);
            if (juce::jmin (dLo, dHi) <= 7.0f) return dLo <= dHi ? Hit::EdgeLo : Hit::EdgeHi;
            if (p.x > xLo && p.x < xHi) return Hit::Band;
            return Hit::Plot;
        }
        return Hit::None;
    }

    void begin (Hit h)
    {
        switch (h)
        {
            case Hit::Band: case Hit::Slider:        aFreq.beginGesture();  break;
            case Hit::EdgeLo: case Hit::EdgeHi:      aWidth.beginGesture(); break;
            case Hit::Threshold: case Hit::Fader:    aThr.beginGesture();   break;
            default: break;
        }
    }

    void end (Hit h)
    {
        switch (h)
        {
            case Hit::Band: case Hit::Slider:        aFreq.endGesture();  break;
            case Hit::EdgeLo: case Hit::EdgeHi:      aWidth.endGesture(); break;
            case Hit::Threshold: case Hit::Fader:    aThr.endGesture();   break;
            default: break;
        }
    }

    void applyDrag (juce::Point<float> p)
    {
        switch (drag)
        {
            case Hit::Band:
                aFreq.setValueAsPartOfGesture (juce::jlimit (20.0f, 20000.0f, xToFreq (p.x - grabOffset)));
                break;
            case Hit::Slider:
                aFreq.setValueAsPartOfGesture (juce::jlimit (20.0f, 20000.0f, sliderXToFreq (p.x)));
                break;
            case Hit::EdgeLo:
            {
                const float f = juce::jmax (1.0f, xToFreq (p.x));
                aWidth.setValueAsPartOfGesture (juce::jlimit (0.2f, 10.0f, 2.0f * std::log2 (freqVal() / f)));
                break;
            }
            case Hit::EdgeHi:
            {
                const float f = juce::jmax (1.0f, xToFreq (p.x));
                aWidth.setValueAsPartOfGesture (juce::jlimit (0.2f, 10.0f, 2.0f * std::log2 (f / freqVal())));
                break;
            }
            case Hit::Threshold:
            case Hit::Fader:
                aThr.setValueAsPartOfGesture (juce::jlimit (-84.0f, 0.0f, yToDb (p.y)));
                break;
            default: break;
        }
    }

    void resetFor (Hit h)
    {
        switch (h)
        {
            case Hit::Slider: case Hit::Band: aFreq.setValueAsCompleteGesture  (pFreq.convertFrom0to1 (pFreq.getDefaultValue())); break;
            case Hit::EdgeLo: case Hit::EdgeHi: aWidth.setValueAsCompleteGesture (pWidth.convertFrom0to1 (pWidth.getDefaultValue())); break;
            case Hit::Threshold: case Hit::Fader: aThr.setValueAsCompleteGesture (pThr.convertFrom0to1 (pThr.getDefaultValue())); break;
            default: break;
        }
    }

    void toggleZoom()
    {
        zoomOn = ! zoomOn;
        if (zoomOn)
        {
            visLo = juce::jmax (20.0f, freqVal() / 4.0f);
            visHi = juce::jmin (20000.0f, freqVal() * 4.0f);
            if (visHi / visLo < 3.0f) { visLo = 20.0f; visHi = 20000.0f; zoomOn = false; }
        }
        else { visLo = 20.0f; visHi = 20000.0f; }
        repaint();
    }

    SilverDesserProcessor& proc;
    juce::RangedAudioParameter &pFreq, &pWidth, &pThr;
    juce::ParameterAttachment aFreq, aWidth, aThr;

    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::vector<float> specIn, specBp, fftData;
    float binHz = 11.7f;
    float peakFreq = 0.0f, peakDb = -120.0f;

    float gr = 0.0f, track = 0.0f;
    float visLo = 20.0f, visHi = 20000.0f;
    bool zoomOn = false;

    Hit drag = Hit::None;
    float grabOffset = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumDisplay)
};
