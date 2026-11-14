#include "PluginEditor.h"

using namespace juce;

namespace
{
    RangedAudioParameter& P (AudioProcessorValueTreeState& a, const char* id) { return *a.getParameter (id); }
}

//==============================================================================
void SilverDesserEditor::BarButton::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setGradientFill (ColourGradient (theme::headerTop, 0, 0, theme::headerBottom, 0, r.getHeight(), false));
    g.fillRect (r);
    g.setColour (theme::panelBorder);
    g.drawLine (0, 0, r.getWidth(), 0, 1.0f);

    g.setColour (theme::navy);
    g.setFont (theme::font (14.0f, true));
    const float cx = r.getCentreX(), cy = r.getCentreY();
    g.drawText ("ADVANCED", Rectangle<float> (cx - 150.0f, 0.0f, 120.0f, r.getHeight()), Justification::centredRight, false);
    g.drawText ("CONTROLS", Rectangle<float> (cx + 30.0f, 0.0f, 120.0f, r.getHeight()), Justification::centredLeft, false);

    Path tri;
    if (advanced) tri.addTriangle (cx - 9.0f, cy + 4.0f, cx + 9.0f, cy + 4.0f, cx, cy - 5.0f);   // aberto: seta para cima
    else          tri.addTriangle (cx - 9.0f, cy - 4.0f, cx + 9.0f, cy - 4.0f, cx, cy + 5.0f);
    g.setColour (theme::navyDim);
    g.fillPath (tri);
}

//==============================================================================
SilverDesserEditor::SilverDesserEditor (SilverDesserProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      display (p),
      ratioFader (P (p.apvts, ids::ratio)),
      inTrim   (P (p.apvts, ids::inTrim)),
      outTrim  (P (p.apvts, ids::outTrim)),
      width    (P (p.apvts, ids::width)),
      slope    (P (p.apvts, ids::slope), 120.0f),
      freq     (P (p.apvts, ids::freq)),
      makeup   (P (p.apvts, ids::makeup)),
      wet      (P (p.apvts, ids::wet)),
      ratioBox (P (p.apvts, ids::ratio)),
      knee     (P (p.apvts, ids::knee), 60.0f, false),
      attack   (P (p.apvts, ids::attack)),
      hold     (P (p.apvts, ids::hold)),
      release  (P (p.apvts, ids::release)),
      damping  (P (p.apvts, ids::damping)),
      effectIn  (P (p.apvts, ids::effectIn),  "IN",   "IN",   true),
      autoTrack (P (p.apvts, ids::autoTrack), "IN",   "IN",   true),
      trigWide  (P (p.apvts, ids::trigWide),  "WIDE", "BAND", false),
      audioWide (P (p.apvts, ids::audioWide), "WIDE", "BAND", false),
      listenAttach (P (p.apvts, ids::listen), [this] (float v)
      {
          const int i = roundToInt (v);
          listenMix.setOn (i == 0); listenInside.setOn (i == 1); listenOutside.setOn (i == 2);
      })
{
    canvas.onPaint = [this] (Graphics& g) { paintCanvas (g); };
    addAndMakeVisible (canvas);

    for (Component* c : std::initializer_list<Component*> {
             &display, &inMeter, &outMeter, &inScale, &outScale, &grMeter, &ratioFader,
             &inTrim, &outTrim, &width, &slope, &freq, &makeup, &wet, &ratioBox, &knee,
             &attack, &hold, &release, &damping,
             &effectIn, &autoTrack, &trigWide, &audioWide,
             &listenMix, &listenInside, &listenOutside, &abA, &abB, &advBar })
        canvas.addAndMakeVisible (c);

    freq.setTooltip ("Centre frequency of the selected band. Double-click to type.");
    width.setTooltip ("Bandwidth in octaves.");
    slope.setTooltip ("Filter slope in dB per octave; this is not a Q control.");
    wet.setTooltip ("Blend compression with the uncompressed signal.");
    autoTrack.setTooltip ("Follow programme level automatically; threshold becomes relative.");
    effectIn.setTooltip ("Enable processing with a short crossfade; latency is retained.");

    // Listen (radio)
    listenMix.icon = PushBtn::Icon::Mix;
    listenInside.icon = PushBtn::Icon::Inside;
    listenOutside.icon = PushBtn::Icon::Outside;
    listenMix.onClick     = [this] { listenAttach.setValueAsCompleteGesture (0.0f); };
    listenInside.onClick  = [this] { listenAttach.setValueAsCompleteGesture (1.0f); };
    listenOutside.onClick = [this] { listenAttach.setValueAsCompleteGesture (2.0f); };
    listenAttach.sendInitialUpdate();

    // A/B
    abA.text = "A"; abB.text = "B";
    abA.greenWhenOn = abB.greenWhenOn = false;
    abA.onClick = [this] { proc.switchAB (0); updateAB(); };
    abB.onClick = [this] { proc.switchAB (1); updateAB(); };
    updateAB();

    // Advanced / Easy
    advBar.onClick = [this] { setAdvanced (! advanced, true); };

    advanced = proc.advancedVisible;
    advBar.advanced = advanced;

    setResizable (true, true);
    const int w0 = jlimit (700, 1800, proc.editorWidth);
    getConstrainer()->setFixedAspectRatio ((double) baseW / (double) baseH());
    setResizeLimits (700, (int) std::lround (700.0 * baseH() / baseW), 1800, (int) std::lround (1800.0 * baseH() / baseW));
    setSize (w0, (int) std::lround ((double) w0 * baseH() / baseW));

    layoutChildren();
    startTimerHz (30);
}

SilverDesserEditor::~SilverDesserEditor()
{
    stopTimer();
}

void SilverDesserEditor::updateAB()
{
    abA.setOn (proc.getActiveSlot() == 0);
    abB.setOn (proc.getActiveSlot() == 1);
}

void SilverDesserEditor::setAdvanced (bool a, bool resizeWindow)
{
    advanced = a;
    proc.advancedVisible = a;
    advBar.advanced = a;
    layoutChildren();

    if (resizeWindow)
    {
        const int w = getWidth();
        getConstrainer()->setFixedAspectRatio ((double) baseW / (double) baseH());
        setResizeLimits (700, (int) std::lround (700.0 * baseH() / baseW), 1800, (int) std::lround (1800.0 * baseH() / baseW));
        setSize (w, (int) std::lround ((double) w * baseH() / baseW));
    }
    canvas.repaint();
}

//==============================================================================
void SilverDesserEditor::paint (Graphics& g)
{
    g.fillAll (theme::bgBottom);
}

void SilverDesserEditor::resized()
{
    const float s = (float) getWidth() / (float) baseW;
    canvas.setTransform (AffineTransform::scale (s));
    canvas.setBounds (0, 0, baseW, baseH());
    proc.editorWidth = getWidth();
}

void SilverDesserEditor::layoutChildren()
{
    // ---- coordenadas na grade base (1256 x 712) ----
    display.setBounds (218, 36, 706, 561);

    inMeter.setBounds   (30, 82, 28, 377);
    inScale.setBounds   (64, 74, 34, 393);
    outMeter.setBounds  (1170, 82, 28, 377);
    outScale.setBounds  (1204, 74, 40, 393);

    inTrim.setBounds    (9, 563, 86, 26);
    outTrim.setBounds   (1157, 495, 86, 26);

    // Listen
    listenMix.setBounds     (130, 100, 58, 36);
    listenInside.setBounds  (130, 182, 58, 38);
    listenOutside.setBounds (130, 260, 58, 38);

    // Filters
    width.setBounds  (115, 401, 86, 24);
    slope.setBounds  (115, 469, 86, 24);
    freq.setBounds   (115, 537, 86, 24);

    // Dynamics
    grMeter.setBounds    (952, 109, 66, 343);
    ratioFader.setBounds (1048, 109, 100, 343);
    makeup.setBounds     (944, 495, 82, 24);
    wet.setBounds        (944, 563, 82, 24);
    ratioBox.setBounds   (1052, 495, 86, 24);
    knee.setBounds       (1052, 563, 86, 24);

    effectIn.setBounds   (1171, 548, 60, 40);

    // cabecalho
    abA.setBounds (1256 - 90, 5, 36, 22);
    abB.setBounds (1256 - 48, 5, 36, 22);

    // barra Advanced
    advBar.setBounds (4, 600, 1248, 26);

    // painel inferior
    trigWide.setBounds  (140, 660, 58, 36);
    audioWide.setBounds (231, 660, 58, 36);
    attack.setBounds    (474, 660, 88, 28);
    hold.setBounds      (603, 660, 88, 28);
    release.setBounds   (732, 660, 88, 28);
    autoTrack.setBounds (1012, 660, 58, 36);
    damping.setBounds   (1105, 660, 90, 28);

    for (Component* c : std::initializer_list<Component*> { &trigWide, &audioWide, &attack, &hold, &release, &autoTrack, &damping })
        c->setVisible (advanced);
}

//==============================================================================
void SilverDesserEditor::paintCanvas (Graphics& g)
{
    using theme::drawLabel;
    using theme::drawPanel;

    // fundo
    g.setGradientFill (ColourGradient (theme::bgTop, 0, 0, theme::bgBottom, 0, (float) baseH(), false));
    g.fillAll();

    // cabecalho
    {
        Rectangle<float> r (0.0f, 0.0f, (float) baseW, 32.0f);
        g.setGradientFill (ColourGradient (theme::headerTop, 0, 0, theme::headerBottom, 0, 32.0f, false));
        g.fillRect (r);
        g.setColour (theme::panelBorder);
        g.drawLine (0, 32.0f, (float) baseW, 32.0f, 1.0f);

        g.setColour (theme::navy);
        g.setFont (theme::font (24.0f, true));
        g.drawText ("SILVER", Rectangle<float> (16.0f, 0.0f, 200.0f, 32.0f), Justification::centredLeft, false);

        Font t = theme::font (22.0f, true);
        t.setExtraKerningFactor (0.14f);
        g.setFont (t);
        g.drawText ("SILVER DEESSER", Rectangle<float> (300.0f, 0.0f, 656.0f, 32.0f), Justification::centred, false);

        g.setColour (theme::navyDim);
        g.setFont (theme::font (13.0f, true));
        g.drawText ("VST3", Rectangle<float> (1060.0f, 0.0f, 80.0f, 32.0f), Justification::centredRight, false);
    }

    // paineis
    drawPanel (g, { 4.0f, 36.0f, 96.0f, 561.0f },    "INPUT");
    drawPanel (g, { 104.0f, 36.0f, 108.0f, 278.0f }, "LISTEN");
    drawPanel (g, { 104.0f, 318.0f, 108.0f, 279.0f }, "FILTERS");
    drawPanel (g, { 930.0f, 36.0f, 217.0f, 561.0f }, "DYNAMICS");
    drawPanel (g, { 1152.0f, 36.0f, 98.0f, 561.0f }, "OUTPUT");

    // rotulos
    drawLabel (g, "TRIM",   { 4.0f, 539.0f, 96.0f, 20.0f }, 14.0f);
    drawLabel (g, "TRIM",   { 1152.0f, 472.0f, 98.0f, 20.0f }, 14.0f);
    drawLabel (g, "EFFECT", { 1152.0f, 524.0f, 98.0f, 20.0f }, 14.0f);

    drawLabel (g, "MIX",     { 104.0f, 76.0f, 108.0f, 20.0f }, 14.0f);
    drawLabel (g, "INSIDE",  { 104.0f, 158.0f, 108.0f, 20.0f }, 14.0f);
    drawLabel (g, "OUTSIDE", { 104.0f, 238.0f, 108.0f, 20.0f }, 14.0f);

    drawLabel (g, "WIDTH",     { 104.0f, 377.0f, 108.0f, 20.0f }, 14.0f);
    drawLabel (g, "SLOPE",   { 104.0f, 445.0f, 108.0f, 20.0f }, 14.0f);
    drawLabel (g, "FREQUENCY", { 104.0f, 513.0f, 108.0f, 20.0f }, 14.0f);

    drawLabel (g, "REDUCTION", { 940.0f, 76.0f, 100.0f, 20.0f }, 13.0f);
    drawLabel (g, "RATIO",     { 1034.0f, 76.0f, 90.0f, 20.0f }, 13.0f);
    drawLabel (g, "MAKE-UP",   { 942.0f, 472.0f, 86.0f, 20.0f }, 13.0f);
    drawLabel (g, "WET/DRY",   { 942.0f, 540.0f, 86.0f, 20.0f }, 13.0f);
    drawLabel (g, "SOFT-KNEE", { 1052.0f, 540.0f, 86.0f, 20.0f }, 13.0f);

    // painel Advanced
    if (advanced)
    {
        drawPanel (g, { 4.0f, 630.0f, 1246.0f, 80.0f });

        g.setColour (theme::panelBorder.withAlpha (0.6f));
        g.drawLine (306.0f, 640.0f, 306.0f, 700.0f, 1.0f);
        g.drawLine (840.0f, 640.0f, 840.0f, 700.0f, 1.0f);

        drawLabel (g, "EFFECT\nMODE:",         { 20.0f, 650.0f, 110.0f, 40.0f }, 15.0f, Justification::centredRight);
        drawLabel (g, "TRIGGER",               { 130.0f, 640.0f, 78.0f, 18.0f }, 13.0f);
        drawLabel (g, "AUDIO",                 { 221.0f, 640.0f, 78.0f, 18.0f }, 13.0f);

        drawLabel (g, "REACTION\nENVELOPE:",   { 322.0f, 650.0f, 140.0f, 40.0f }, 15.0f, Justification::centredRight);
        drawLabel (g, "ATTACK",                { 474.0f, 640.0f, 88.0f, 18.0f }, 13.0f);
        drawLabel (g, "HOLD",                  { 603.0f, 640.0f, 88.0f, 18.0f }, 13.0f);
        drawLabel (g, "RELEASE",               { 732.0f, 640.0f, 88.0f, 18.0f }, 13.0f);

        drawLabel (g, "LEVEL\nTRACKING:",      { 850.0f, 650.0f, 150.0f, 40.0f }, 15.0f, Justification::centredRight);
        drawLabel (g, "AUTO",                  { 1009.0f, 640.0f, 64.0f, 18.0f }, 13.0f);
        drawLabel (g, "DAMPING",               { 1105.0f, 640.0f, 90.0f, 18.0f }, 13.0f);
    }
}

//==============================================================================
void SilverDesserEditor::timerCallback()
{
    const float dt = 1.0f / 30.0f;

    // entrada / saida
    const float inDb  = Decibels::gainToDecibels (proc.meterInPeak.exchange (0.0f), -100.0f);
    const float outDb = Decibels::gainToDecibels (proc.meterOutPeak.exchange (0.0f), -100.0f);

    inLevel  = jmax (inDb,  inLevel  - 30.0f * dt);
    outLevel = jmax (outDb, outLevel - 30.0f * dt);

    if (outDb >= outHold) { outHold = outDb; outHoldT = 2.0f; }
    else if ((outHoldT -= dt) <= 0.0f) outHold = jmax (-100.0f, outHold - 40.0f * dt);

    inMeter.setLevels  (inLevel,  -100.0f);
    outMeter.setLevels (outLevel, outHold);

    // reducao
    const float grNow = proc.meterGrDb.exchange (0.0f);
    grLevel = jmax (grNow, grLevel - 36.0f * dt);
    if (grNow >= grHold) { grHold = grNow; grHoldT = 2.0f; }
    else if ((grHoldT -= dt) <= 0.0f) grHold = jmax (0.0f, grHold - 20.0f * dt);

    grMeter.setValues (grLevel, grHold);

    display.setLive (grLevel, proc.trackLevelDb.load());
    display.updateAnalysis();
    display.repaint();
}
