#pragma once
// =============================================================================
//  Paleta e helpers de desenho "SILVER".
//  Para trocar o visual, basta mexer nas cores abaixo.
// =============================================================================
#include <juce_gui_basics/juce_gui_basics.h>

namespace theme
{
    using juce::Colour;

    // janela / paineis
    inline const Colour bgTop        { 0xfff2f5fa };
    inline const Colour bgBottom     { 0xffd9e1ec };
    inline const Colour panelTop     { 0xfffafcff };
    inline const Colour panelBottom  { 0xffe5ebf3 };
    inline const Colour panelBorder  { 0xffafbdcf };
    inline const Colour headerTop    { 0xfff8faff };
    inline const Colour headerBottom { 0xffdee6f0 };

    // textos (azul-marinho)
    inline const Colour navy         { 0xff1b3049 };
    inline const Colour navyDim      { 0xff526780 };
    inline const Colour white        { 0xffffffff };

    // caixas de valor
    inline const Colour boxBg        { 0xffffffff };
    inline const Colour boxBorder    { 0xff9cacc2 };

    // display
    inline const Colour dispTop      { 0xfff8fbff };
    inline const Colour dispBottom   { 0xffeaf0f8 };
    inline const Colour stripBg      { 0xffe2e7ee };
    inline const Colour gridLine     { 0xffb4bdcb };
    inline const Colour gridText     { 0xff2c4068 };
    inline const Colour spectrumFill { 0xff7b9fc5 };
    inline const Colour spectrumLine { 0xff436e9e };
    inline const Colour sideFill     { 0xffbb517c };   // sidechain / banda
    inline const Colour bandLine     { 0xff008d9c };   // bordas da banda e threshold
    inline const Colour threshold    { 0xff008d9c };
    inline const Colour thresholdEff { 0xff1b3049 };
    inline const Colour marker       { 0xffe07b00 };   // setas laranja
    inline const Colour reduction    { 0xffd6204b };

    // medidores / botoes
    inline const Colour meterGreen   { 0xff35b05a };
    inline const Colour meterYellow  { 0xffe6b800 };
    inline const Colour meterRed     { 0xffe0443a };
    inline const Colour meterOff     { 0xffa9b4ca };
    inline const Colour grBar        { 0xfff2c200 };
    inline const Colour btnOn        { 0xff52d273 };

    inline juce::Font font (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    }

    inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title = {})
    {
        g.setGradientFill (juce::ColourGradient (panelTop, r.getX(), r.getY(), panelBottom, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 9.0f);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.drawRoundedRectangle (r.reduced (1.0f), 8.0f, 1.0f);
        g.setColour (panelBorder);
        g.drawRoundedRectangle (r, 9.0f, 1.0f);

        if (title.isNotEmpty())
        {
            g.setColour (navy);
            g.setFont (font (17.0f, true));
            g.drawText (title, r.withHeight (28.0f).translated (0, 2.0f), juce::Justification::centred, false);
        }
    }

    inline void drawInset (juce::Graphics& g, juce::Rectangle<float> r, float radius = 3.0f)
    {
        g.setColour (boxBg);
        g.fillRoundedRectangle (r, radius);
        g.setColour (boxBorder);
        g.drawRoundedRectangle (r, radius, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.drawLine (r.getX() + 2, r.getY() + 1.5f, r.getRight() - 2, r.getY() + 1.5f, 1.0f);
    }

    inline void drawLabel (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float size = 14.0f,
                           juce::Justification j = juce::Justification::centred)
    {
        g.setColour (navy);
        g.setFont (font (size, true));
        if (t.contains ("\n"))
            g.drawFittedText (t, r.toNearestInt(), j, 2, 1.0f);
        else
            g.drawText (t, r, j, false);
    }

    // puxador metalico (fader)
    inline void drawMetalHandle (juce::Graphics& g, juce::Rectangle<float> r, bool horizontalGrip = true)
    {
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (r.translated (1.5f, 2.0f), 4.0f);

        juce::ColourGradient grad (juce::Colour (0xfff7f9fc), r.getX(), r.getY(),
                                   juce::Colour (0xff98a2b3), r.getX(), r.getBottom(), false);
        grad.addColour (0.5, juce::Colour (0xffd5dbe5));
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colour (0xff5d687c));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);

        // sulcos
        const float cx = r.getCentreX(), cy = r.getCentreY();
        for (int pass = 0; pass < 2; ++pass)
        {
            g.setColour (pass == 0 ? juce::Colour (0xff6f7a8f).withAlpha (0.8f) : juce::Colours::white.withAlpha (0.7f));
            const float off = pass == 0 ? 0.0f : 1.2f;
            for (int i = -1; i <= 1; ++i)
            {
                if (horizontalGrip) g.drawLine (r.getX() + 5, cy + i * 5.0f + off, r.getRight() - 5, cy + i * 5.0f + off, 1.2f);
                else                g.drawLine (cx + i * 5.0f + off, r.getY() + 5, cx + i * 5.0f + off, r.getBottom() - 5, 1.2f);
            }
        }
    }
}
