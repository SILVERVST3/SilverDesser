#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "SilverTheme.h"

//==============================================================================
//  Caixa de valor: arrasta (vertical), duplo-clique para digitar, Alt/Cmd-clique = padrao
//==============================================================================
class ValueBox : public juce::Component
{
public:
    ValueBox (juce::RangedAudioParameter& p, float dragPixels = 240.0f, bool allowTyping = true)
        : param (p), pixels (dragPixels), typing (allowTyping),
          attach (p, [this] (float) { repaint(); })
    {
        attach.sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        theme::drawInset (g, r);
        g.setColour (theme::navy);
        g.setFont (theme::font (juce::jmin (16.0f, r.getHeight() * 0.68f), true));
        g.drawText (param.getCurrentValueAsText(), r, juce::Justification::centred, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isAltDown() || e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            attach.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
            return;
        }
        startNorm = param.getValue();
        attach.beginGesture();
        dragging = true;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging) return;
        const float fine = e.mods.isShiftDown() ? 0.1f : 1.0f;
        const float n = juce::jlimit (0.0f, 1.0f, startNorm - (float) e.getDistanceFromDragStartY() / pixels * fine);
        attach.setValueAsPartOfGesture (param.convertFrom0to1 (n));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging) attach.endGesture();
        dragging = false;
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (! typing) return;

        editor = std::make_unique<juce::TextEditor>();
        editor->setBounds (getLocalBounds());
        editor->setFont (theme::font (15.0f, true));
        editor->setJustification (juce::Justification::centred);
        editor->setColour (juce::TextEditor::backgroundColourId, theme::boxBg);
        editor->setColour (juce::TextEditor::textColourId, theme::navy);
        editor->setColour (juce::TextEditor::outlineColourId, theme::bandLine);
        editor->setColour (juce::TextEditor::focusedOutlineColourId, theme::bandLine);
        editor->setText (param.getCurrentValueAsText(), false);
        editor->onReturnKey = [this] { commit(); };
        editor->onEscapeKey = [this] { closeEditorAsync(); };
        editor->onFocusLost = [this] { closeEditorAsync(); };
        addAndMakeVisible (*editor);
        editor->grabKeyboardFocus();
        editor->selectAll();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const float step = (e.mods.isShiftDown() ? 0.002f : 0.01f) * (w.deltaY > 0 ? 1.0f : -1.0f);
        const float n = juce::jlimit (0.0f, 1.0f, param.getValue() + step);
        attach.setValueAsCompleteGesture (param.convertFrom0to1 (n));
    }

private:
    void commit()
    {
        if (editor == nullptr) return;
        const float n = param.getValueForText (editor->getText());
        attach.setValueAsCompleteGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n)));
        closeEditorAsync();
    }

    void closeEditorAsync()
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ValueBox> (this)]
        {
            if (safe != nullptr) { safe->editor.reset(); safe->repaint(); }
        });
    }

    juce::RangedAudioParameter& param;
    float pixels;
    bool typing;
    juce::ParameterAttachment attach;
    float startNorm = 0.0f;
    bool dragging = false;
    std::unique_ptr<juce::TextEditor> editor;
};

//==============================================================================
//  Botao (texto ou icone). Estado "on" pintado de verde ou azul-prata.
//==============================================================================
class PushBtn : public juce::Component
{
public:
    enum class Icon { None, Mix, Inside, Outside };

    PushBtn() = default;

    juce::String text;
    Icon icon = Icon::None;
    bool on = false;
    bool greenWhenOn = true;
    std::function<void()> onClick;

    void setOn (bool v) { if (on != v) { on = v; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.5f);

        juce::Colour top, bot;
        if (on && greenWhenOn)       { top = theme::btnOn.brighter (0.35f); bot = theme::btnOn.darker (0.15f); }
        else if (on)                 { top = juce::Colour (0xff9fb6dd);     bot = juce::Colour (0xff6f8bbd); }
        else                         { top = juce::Colour (0xfff3f5f8);     bot = juce::Colour (0xffb7bfcc); }

        if (down) { std::swap (top, bot); }

        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillRoundedRectangle (r.translated (0.0f, 1.5f), 4.0f);
        g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), bot, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (theme::boxBorder);
        g.drawRoundedRectangle (r, 4.0f, 1.2f);

        g.setColour (theme::navy);
        if (icon == Icon::None)
        {
            g.setFont (theme::font (juce::jmin (16.0f, r.getHeight() * 0.62f), true));
            g.drawText (text, r, juce::Justification::centred, false);
        }
        else
        {
            drawIcon (g, r.reduced (r.getWidth() * 0.18f, r.getHeight() * 0.2f));
        }
    }

    void mouseDown (const juce::MouseEvent&) override { down = true; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        down = false; repaint();
        if (contains (e.getPosition()) && onClick) onClick();
    }

private:
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> r)
    {
        juce::Path p;
        const float cx = r.getCentreX(), cy = r.getCentreY();

        if (icon == Icon::Mix)
        {
            const float s = r.getHeight() * 0.28f;
            for (float dx : { -r.getWidth() * 0.24f, r.getWidth() * 0.24f })
            {
                p.startNewSubPath (cx + dx - s, cy); p.lineTo (cx + dx + s, cy);
                p.startNewSubPath (cx + dx, cy - s); p.lineTo (cx + dx, cy + s);
            }
        }
        else
        {
            const float w = r.getWidth(), h = r.getHeight() * 0.55f;
            const float x0 = r.getX(), x3 = r.getRight();
            const float x1 = x0 + w * 0.3f, x2 = x0 + w * 0.7f;
            const float yHi = cy - h * 0.5f, yLo = cy + h * 0.5f;
            if (icon == Icon::Inside)
            {
                p.startNewSubPath (x0, yLo); p.lineTo (x1, yLo); p.lineTo (x1, yHi);
                p.lineTo (x2, yHi); p.lineTo (x2, yLo); p.lineTo (x3, yLo);
            }
            else
            {
                p.startNewSubPath (x0, yHi); p.lineTo (x1, yHi); p.lineTo (x1, yLo);
                p.lineTo (x2, yLo); p.lineTo (x2, yHi); p.lineTo (x3, yHi);
            }
        }
        g.strokePath (p, juce::PathStrokeType (2.4f));
    }

    bool down = false;
};

// Botao ligado a um parametro booleano
class ParamButton : public PushBtn
{
public:
    ParamButton (juce::RangedAudioParameter& p, juce::String onText, juce::String offText, bool green)
        : attach (p, [this] (float v) { setOn (v > 0.5f); refreshText(); }),
          onT (std::move (onText)), offT (std::move (offText))
    {
        greenWhenOn = green;
        attach.sendInitialUpdate();
        onClick = [this] { attach.setValueAsCompleteGesture (on ? 0.0f : 1.0f); };
    }

private:
    void refreshText() { text = on ? onT : offT; repaint(); }
    juce::ParameterAttachment attach;
    juce::String onT, offT;
};

//==============================================================================
//  Medidor de nivel de entrada / saida (segmentos)
//==============================================================================
class MeterBar : public juce::Component
{
public:
    explicit MeterBar (bool showPeakHold) : peakHold (showPeakHold) {}

    void setLevels (float levelDb, float holdDb)
    {
        if (std::abs (levelDb - level) > 0.05f || std::abs (holdDb - hold) > 0.05f)
        {
            level = levelDb; hold = holdDb; repaint();
        }
    }

    static constexpr int numSegs = 29;

    static float segLowerDb (int j) { return j < 18 ? -(float) (j + 1) : -18.0f - 2.0f * (float) (j - 17); }

    // y (0 = topo) para um valor em dB
    static float dbToFrac (float db)
    {
        db = juce::jlimit (-40.0f, 0.0f, db);
        const float d = -db;
        return d <= 18.0f ? d / (float) numSegs : (18.0f + (d - 18.0f) * 0.5f) / (float) numSegs;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const float h = r.getHeight() / (float) numSegs;

        for (int j = 0; j < numSegs; ++j)
        {
            const float lower = segLowerDb (j);
            const bool lit = level >= lower;
            juce::Colour c = theme::meterOff;
            if (lit) c = lower >= -3.0f ? theme::meterRed : (lower >= -12.0f ? theme::meterYellow : theme::meterGreen);

            auto seg = juce::Rectangle<float> (r.getX(), r.getY() + (float) j * h, r.getWidth(), h).reduced (1.5f, 1.0f);
            g.setColour (c);
            g.fillRect (seg);
            g.setColour (juce::Colours::black.withAlpha (0.15f));
            g.drawRect (seg, 0.8f);
        }

        if (peakHold && hold > -40.0f)
        {
            const float y = r.getY() + dbToFrac (hold) * r.getHeight();
            g.setColour (theme::navy);
            g.fillRect (r.getX(), y - 1.0f, r.getWidth(), 2.0f);
        }
    }

private:
    bool peakHold;
    float level = -100.0f, hold = -100.0f;
};

// escala 0 .. -40 ao lado do medidor
class MeterScale : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::navy);
        g.setFont (theme::font (13.0f, true));
        static const int marks[] = { 0, -2, -4, -6, -8, -10, -12, -16, -20, -24, -28, -32, -40 };
        const float meterH = (float) getHeight() - 16.0f;   // 8 px de folga em cima e embaixo
        for (int m : marks)
        {
            const float y = 8.0f + MeterBar::dbToFrac ((float) m) * meterH;
            g.drawText (juce::String (m), 0, (int) y - 8, getWidth(), 16, juce::Justification::centredLeft, false);
        }
    }
};

//==============================================================================
//  Medidor de reducao (Reduction)
//==============================================================================
class GRMeter : public juce::Component
{
public:
    void setValues (float grDb, float holdDb)
    {
        if (std::abs (grDb - gr) > 0.02f || std::abs (holdDb - hold) > 0.02f) { gr = grDb; hold = holdDb; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        theme::drawInset (g, r.reduced (0.5f), 2.0f);

        const float top = 4.0f, h = r.getHeight() - 8.0f;
        auto yOf = [&] (float db) { return top + juce::jlimit (0.0f, 24.0f, db) / 24.0f * h; };

        // barra
        g.setColour (theme::grBar);
        g.fillRect (juce::Rectangle<float> (6.0f, top, 16.0f, yOf (gr) - top));
        g.setColour (theme::navy.withAlpha (0.6f));
        g.drawRect (juce::Rectangle<float> (6.0f, top, 16.0f, h), 1.0f);

        // pico
        if (hold > 0.1f)
        {
            g.setColour (theme::reduction);
            g.fillRect (4.0f, yOf (hold) - 1.0f, 20.0f, 2.0f);
        }

        // escala
        g.setColour (theme::navy);
        g.setFont (theme::font (13.0f, true));
        for (int d = 3; d <= 24; d += 3)
        {
            const float y = yOf ((float) d);
            g.drawLine (24.0f, y, 29.0f, y, 1.0f);
            g.drawText (juce::String (d), 31, (int) y - 8, r.getWidth() - 32, 16, juce::Justification::centredLeft, false);
        }
    }

private:
    float gr = 0.0f, hold = 0.0f;
};

//==============================================================================
//  Fader vertical de Ratio (graus): 45 (topo, 1:1) ... -22.5 (base, "cut")
//==============================================================================
class RatioFader : public juce::Component
{
public:
    explicit RatioFader (juce::RangedAudioParameter& p)
        : param (p), attach (p, [this] (float) { repaint(); })
    {
        attach.sendInitialUpdate();
    }

    static constexpr float kMax = 45.0f, kMin = -22.5f;

    void paint (juce::Graphics& g) override
    {
        const float cx = trackX();
        const float top = trackTop(), bot = trackBottom();

        // trilho
        g.setColour (juce::Colour (0xff7d889c));
        g.fillRoundedRectangle (cx - 3.0f, top - 4.0f, 6.0f, bot - top + 8.0f, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (cx - 3.0f, top - 4.0f, 6.0f, bot - top + 8.0f, 3.0f, 1.0f);

        // rotulos
        g.setColour (theme::navy);
        g.setFont (theme::font (12.5f, true));
        const float lx = cx + 24.0f;
        auto yFor = [&] (float deg) { return top + (kMax - deg) / (kMax - kMin) * (bot - top); };
        g.drawText ("LIGHT",  (int) lx, (int) yFor (34.0f) - 14, 60, 14, juce::Justification::centredLeft, false);
        g.drawText ("COMP",   (int) lx, (int) yFor (34.0f),      60, 14, juce::Justification::centredLeft, false);
        g.drawText ("HEAVY",  (int) lx, (int) yFor (9.0f) - 14,  60, 14, juce::Justification::centredLeft, false);
        g.drawText ("COMP",   (int) lx, (int) yFor (9.0f),       60, 14, juce::Justification::centredLeft, false);
        g.drawText ("CUT",    (int) lx, (int) (bot - 14),        60, 14, juce::Justification::centredLeft, false);

        // marca do 0 graus (limitador)
        const float y0 = yFor (0.0f);
        g.setColour (theme::navyDim);
        g.drawLine (lx - 2.0f, y0, lx + 40.0f, y0, 1.0f);

        // puxador
        const float y = yFor (currentDeg());
        theme::drawMetalHandle (g, juce::Rectangle<float> (cx - 17.0f, y - 20.0f, 34.0f, 40.0f));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isAltDown() || e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            attach.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
            return;
        }
        attach.beginGesture();
        dragging = true;
        setFromY ((float) e.y);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (dragging) setFromY ((float) e.y); }
    void mouseUp (const juce::MouseEvent&) override { if (dragging) attach.endGesture(); dragging = false; }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        attach.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
    }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        attach.setValueAsCompleteGesture (juce::jlimit (kMin, kMax, currentDeg() + (w.deltaY > 0 ? 0.5f : -0.5f)));
    }

private:
    float trackX() const { return 22.0f; }
    float trackTop() const { return 22.0f; }
    float trackBottom() const { return (float) getHeight() - 22.0f; }
    float currentDeg() const { return param.convertFrom0to1 (param.getValue()); }

    void setFromY (float y)
    {
        const float f = juce::jlimit (0.0f, 1.0f, (y - trackTop()) / (trackBottom() - trackTop()));
        attach.setValueAsPartOfGesture (juce::jlimit (kMin, kMax, kMax - f * (kMax - kMin)));
    }

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attach;
    bool dragging = false;
};
