#include "PluginEditor.h"

using namespace juce;
namespace col = si::col;
using si::Weight;

namespace
{
constexpr int W = 920, H = 650;

float pv (si::APVTS& s, const char* id) { return s.getRawParameterValue (id)->load(); }

void dragOut (SIRiserProcessor& proc, Component* from)
{
    const auto f = proc.writeRenderToFile (true);
    if (f.existsAsFile())
        DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, from);
}

void drawChipText (Graphics& g, Rectangle<float> r, const String& t, Colour c)
{
    g.setColour (Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
    g.setColour (c);
    g.setFont (si::font (9.5f, Weight::SemiBold, 0.1f));
    g.drawText (t, r, Justification::centred);
}

//==============================================================================
/** Rendered riser: glowing swell up to the DROP beam, dry tail after it. Drop files in, drag the render out. */
class WaveView : public Component, public FileDragAndDropTarget, private Timer
{
public:
    explicit WaveView (SIRiserProcessor& p) : proc (p) { startTimerHz (60); }

    void refreshPeaks()
    {
        res = proc.getResult();
        peaks.clear();
        if (res != nullptr)
        {
            const int cols = jmax (1, getWidth() / 2);
            const auto& a = res->audio;
            const int n = a.getNumSamples();
            peaks.resize ((size_t) cols);
            for (int c = 0; c < cols; ++c)
            {
                const int s0 = (int) ((int64) n * c / cols), s1 = jmax (s0 + 1, (int) ((int64) n * (c + 1) / cols));
                float mx = 0.0f;
                for (int ch = 0; ch < 2; ++ch) mx = jmax (mx, a.getMagnitude (ch, s0, s1 - s0));
                peaks[(size_t) c] = mx;
            }
        }
        repaint();
    }

    void resized() override { refreshPeaks(); }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colours::black.withAlpha (0.38f));
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (Colours::white.withAlpha (dragHover ? 0.0f : 0.06f));
        g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);

        using St = SIRiserProcessor::Status;
        const auto st = proc.status.load();
        const float midY = r.getCentreY() + 4.0f;

        if (res != nullptr && ! peaks.empty())
            drawWave (g, r, midY);
        else
            drawEmpty (g, r, st);

        if (dragHover)
        {
            Path border;
            border.addRoundedRectangle (r.reduced (2.0f), 13.0f);
            Path dashed;
            const float d[] = { 6.0f, 5.0f };
            PathStrokeType (1.6f).createDashedStroke (dashed, border, d, 2);
            g.setColour (col::accent);
            g.fillPath (dashed);
        }

        // status line
        String msg;
        Colour mc = col::label;
        if (st == St::Recording) { msg = "REC  " + String (proc.recSeconds.load(), 1) + " s"; mc = col::rec; }
        else if (st == St::Armed) { msg = "WAITING FOR AUDIO"; mc = col::rec; }
        else if (st == St::Rendering) { msg = "RENDERING"; mc = col::accent; }
        else if (st == St::Failed) { msg = "NO AUDIO IN SOURCE"; mc = col::warn; }
        if (msg.isNotEmpty())
        {
            auto chip = Rectangle<float> (r.getX() + 12.0f, r.getY() + 12.0f, 150.0f, 24.0f);
            drawChipText (g, chip, {}, mc);
            const float pulse = 0.5f + 0.5f * std::sin (phase * 6.0f);
            g.setColour (mc.withAlpha (st == St::Recording || st == St::Armed ? 0.4f + 0.6f * pulse : 1.0f));
            g.fillEllipse (chip.getX() + 11.0f, chip.getCentreY() - 3.5f, 7.0f, 7.0f);
            g.setColour (mc);
            g.setFont (si::font (9.5f, Weight::SemiBold, 0.12f));
            g.drawText (msg, chip.withTrimmedLeft (26.0f), Justification::centredLeft);
        }

        // live input meter while capturing
        if (st == St::Armed || st == St::Recording)
        {
            const float lvl = jlimit (0.0f, 1.0f, (Decibels::gainToDecibels (proc.inLevel.load(), -60.0f) + 60.0f) / 60.0f);
            auto m = Rectangle<float> (r.getRight() - 16.0f, r.getY() + 14.0f, 4.0f, r.getHeight() - 28.0f);
            g.setColour (Colours::white.withAlpha (0.06f));
            g.fillRoundedRectangle (m, 2.0f);
            g.setColour (col::rec);
            g.fillRoundedRectangle (m.withTrimmedTop (m.getHeight() * (1.0f - lvl)), 2.0f);
        }
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (! dragging && res != nullptr && e.getDistanceFromDragStart() > 6)
        {
            dragging = true;
            dragOut (proc, this);
        }
    }
    void mouseUp (const MouseEvent&) override { dragging = false; }
    void mouseEnter (const MouseEvent&) override { setMouseCursor (res != nullptr ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor); }

    bool isInterestedInFileDrag (const StringArray& files) override
    {
        for (auto& f : files)
            if (File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg") && ! f.contains ("SI Riser Renders")) return true;
        return false;
    }
    void fileDragEnter (const StringArray&, int, int) override { dragHover = true; repaint(); }
    void fileDragExit (const StringArray&) override { dragHover = false; repaint(); }
    void filesDropped (const StringArray& files, int, int) override
    {
        dragHover = false;
        for (auto& f : files)
            if (proc.loadFile (File (f))) break;
        repaint();
    }

private:
    void drawEmpty (Graphics& g, Rectangle<float> r, SIRiserProcessor::Status st)
    {
        if (st == SIRiserProcessor::Status::Armed || st == SIRiserProcessor::Status::Recording) return;
        auto c = r.getCentre();
        const float pulse = 0.5f + 0.5f * std::sin (phase * 2.0f);
        g.setColour (col::accent.withAlpha (0.06f + 0.05f * pulse));
        g.fillEllipse (Rectangle<float> (64.0f, 64.0f).withCentre (c.translated (0, -16)));
        si::drawIcon (g, si::Icon::Wave, Rectangle<float> (34.0f, 34.0f).withCentre (c.translated (0, -16)), col::accent);
        g.setColour (col::text);
        g.setFont (si::font (13.0f, Weight::Medium));
        g.drawText ("Capture your clip or drop an audio file", r.withTop (c.y + 22.0f).withHeight (20.0f), Justification::centredTop);
        g.setColour (col::hint);
        g.setFont (si::font (10.5f));
        g.drawText ("WAV  AIFF  FLAC  MP3", r.withTop (c.y + 44.0f).withHeight (16.0f), Justification::centredTop);
    }

    void drawWave (Graphics& g, Rectangle<float> r, float midY)
    {
        const int n = res->audio.getNumSamples();
        auto area = r.reduced (14.0f, 0.0f);
        const float xs = area.getWidth() / (float) peaks.size();
        const float dropX = area.getX() + area.getWidth() * (float) res->riserSamples / (float) n;
        const float h = r.getHeight() * 0.36f;

        // beat grid + bar numbers
        const double spb = res->sampleRate * 60.0 / res->bpm;
        const int beats = (int) std::round (res->lengthBeats);
        g.setFont (si::font (8.5f, Weight::SemiBold));
        for (int b = 0; b < beats; ++b)
        {
            const float x = area.getX() + area.getWidth() * (float) (b * spb / n);
            const bool bar = b % 4 == 0;
            g.setColour (Colours::white.withAlpha (bar ? 0.10f : 0.04f));
            g.fillRect (x, r.getY() + 14.0f, 1.0f, r.getHeight() - 28.0f);
            if (bar && beats > 4)
            {
                g.setColour (col::hint);
                g.drawText (String (b / 4 + 1), Rectangle<float> (x + 4.0f, r.getBottom() - 20.0f, 20.0f, 10.0f), Justification::left);
            }
        }
        g.setColour (Colours::white.withAlpha (0.08f));
        g.fillRect (area.getX(), midY, area.getWidth(), 1.0f);

        auto build = [&] (int c0, int c1)
        {
            Path p;
            if (c1 <= c0) return p;
            p.startNewSubPath (area.getX() + c0 * xs, midY);
            for (int c = c0; c < c1; ++c) p.lineTo (area.getX() + c * xs, midY - h * jmin (1.0f, peaks[(size_t) c]));
            for (int c = c1 - 1; c >= c0; --c) p.lineTo (area.getX() + c * xs, midY + h * jmin (1.0f, peaks[(size_t) c]));
            p.closeSubPath();
            return p;
        };
        const int dropC = jlimit (0, (int) peaks.size(), (int) ((dropX - area.getX()) / xs));
        const auto riser = build (0, dropC), dry = build (dropC, (int) peaks.size());

        ColourGradient fill (col::accent2.withAlpha (0.10f), area.getX(), 0, col::accent, dropX, 0, false);
        fill.addColour (0.75, col::accent.withAlpha (0.85f));
        g.setGradientFill (fill);
        g.fillPath (riser);
        auto glow = fill;
        glow.multiplyOpacity (0.25f);
        g.setGradientFill (glow);
        g.strokePath (riser, PathStrokeType (5.0f));
        g.setColour (Colours::white.withAlpha (0.16f));
        g.fillPath (dry);

        // curve overlay (riser gain shape)
        {
            const float shapeExp = std::pow (2.0f, -2.0f * pv (proc.apvts, "shape") * 0.01f);
            Path cv;
            for (int i = 0; i <= 64; ++i)
            {
                const float t = (float) i / 64.0f;
                const auto pt = Point<float> (area.getX() + (dropX - area.getX()) * t, midY - h * 1.08f * std::pow (t, shapeExp));
                i == 0 ? cv.startNewSubPath (pt) : cv.lineTo (pt);
            }
            Path dashed;
            const float d[] = { 3.0f, 4.0f };
            PathStrokeType (1.2f).createDashedStroke (dashed, cv, d, 2);
            g.setColour (Colours::white.withAlpha (0.35f));
            g.fillPath (dashed);
        }

        // drop beam
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.0f), dropX - 18.0f, 0, col::accentHi.withAlpha (0.25f), dropX, 0, false));
        g.fillRect (dropX - 18.0f, r.getY() + 8.0f, 18.0f, r.getHeight() - 16.0f);
        g.setColour (col::text);
        g.fillRect (dropX - 0.75f, r.getY() + 8.0f, 1.5f, r.getHeight() - 16.0f);
        auto tag = Rectangle<float> (jlimit (r.getX() + 6.0f, r.getRight() - 54.0f, dropX - 24.0f), r.getY() + 10.0f, 48.0f, 18.0f);
        g.setGradientFill (si::accentGradient (tag.getTopLeft(), tag.getBottomRight()));
        g.fillRoundedRectangle (tag, 9.0f);
        g.setColour (col::onAccent);
        g.setFont (si::font (9.0f, Weight::SemiBold, 0.14f));
        g.drawText ("DROP", tag, Justification::centred);

        // playhead
        if (proc.previewPlaying.load())
        {
            const float x = area.getX() + area.getWidth() * (float) proc.previewPos.load() / (float) n;
            g.setColour (col::accentHi.withAlpha (0.25f));
            g.fillRect (x - 4.0f, r.getY() + 6.0f, 8.0f, r.getHeight() - 12.0f);
            g.setColour (col::accentHi);
            g.fillRect (x - 1.0f, r.getY() + 6.0f, 2.0f, r.getHeight() - 12.0f);
        }

        // info chips (top-right)
        static const char* lenNames[] = { "1 BEAT", "2 BEATS", "1 BAR", "2 BARS", "4 BARS", "8 BARS" };
        const String info = String (lenNames[jlimit (0, 5, (int) pv (proc.apvts, "length"))]) + String (CharPointer_UTF8 ("  \xc2\xb7  ")) + String (res->bpm, 1) + String (CharPointer_UTF8 (" BPM  \xc2\xb7  "))
                            + String (res->riserSamples / res->sampleRate, 2) + " S";
        const float iw = GlyphArrangement::getStringWidth (si::font (9.5f, Weight::SemiBold, 0.1f), info) + 26.0f;
        drawChipText (g, Rectangle<float> (r.getRight() - 12.0f - iw, r.getBottom() - 34.0f, iw, 24.0f), info, col::text);
    }

    void timerCallback() override
    {
        phase += 1.0f / 60.0f;
        using St = SIRiserProcessor::Status;
        const auto st = proc.status.load();
        if (proc.previewPlaying.load() || st == St::Armed || st == St::Recording || st == St::Rendering || res == nullptr || st != lastSt)
            repaint();
        lastSt = st;
    }

    SIRiserProcessor& proc;
    std::shared_ptr<si::RiserResult> res;
    std::vector<float> peaks;
    bool dragging = false, dragHover = false;
    float phase = 0.0f;
    SIRiserProcessor::Status lastSt = SIRiserProcessor::Status::Empty;
};

//==============================================================================
/** Big gradient "drag to DAW" handle. */
class DragPill : public Component, public SettableTooltipClient
{
public:
    explicit DragPill (SIRiserProcessor& p) : proc (p) { setMouseCursor (MouseCursor::DraggingHandCursor); }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool ok = proc.getResult() != nullptr;
        const float rad = r.getHeight() * 0.5f;
        if (ok)
        {
            g.setGradientFill (si::accentGradient (r.getTopLeft(), r.getBottomRight()));
            g.fillRoundedRectangle (r, rad);
            g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.4f), 0, r.getY(), Colours::white.withAlpha (0.0f), 0, r.getCentreY(), false));
            g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
        }
        else
        {
            g.setColour (Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (r, rad);
            g.setColour (Colours::white.withAlpha (0.08f));
            g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
        }
        const auto c = ok ? col::onAccent : col::hint;
        si::drawIcon (g, si::Icon::Grip, Rectangle<float> (r.getX() + 14.0f, r.getCentreY() - 8.0f, 16.0f, 16.0f), c);
        si::drawIcon (g, si::Icon::Export, Rectangle<float> (r.getRight() - 32.0f, r.getCentreY() - 8.0f, 16.0f, 16.0f), c);
        g.setColour (c);
        g.setFont (si::font (11.0f, Weight::SemiBold, 0.1f));
        g.drawText ("DRAG TO DAW", r, Justification::centred);
    }
    void mouseEnter (const MouseEvent&) override { repaint(); }
    void mouseExit (const MouseEvent&) override { repaint(); }
    void mouseDrag (const MouseEvent& e) override
    {
        if (! dragging && e.getDistanceFromDragStart() > 4 && proc.getResult() != nullptr)
        {
            dragging = true;
            dragOut (proc, this);
        }
    }
    void mouseUp (const MouseEvent&) override { dragging = false; }

private:
    SIRiserProcessor& proc;
    bool dragging = false;
};

//==============================================================================
class Root : public si::ScaledContent, private Timer
{
public:
    explicit Root (SIRiserProcessor& p) : si::ScaledContent (W, H), proc (p), s (p.apvts), presetBar (p.presets), wave (p), pill (p)
    {
        for (auto* c : std::initializer_list<Component*> { &presetBar, &wave, &captureBtn, &previewBtn, &syncBtn, &thruBtn, &loadBtn,
                                                            &exportBtn, &pill, &length, &feed, &tempoSeg, &deess, &gain, &bpmKnob,
                                                            &dryBtn, &normBtn, &mode, &color, &size, &tail, &hicut, &locut, &shape,
                                                            &pitch, &filter, &ftype, &wIn, &wOut, &post, &phone, &chorus, &trem, &stut,
                                                            &lofi, &echo, &rate, &accel })
            addAndMakeVisible (c);

        pitch.slider.getProperties().set ("fromEnd", true);
        color.keepCase();
        captureBtn.getProperties().set ("rec", true);
        previewBtn.getProperties().set ("cta", true);
        si::setIcon (captureBtn, si::Icon::Record);
        si::setIcon (previewBtn, si::Icon::Play);
        si::setIcon (loadBtn, si::Icon::Folder);
        si::setIcon (exportBtn, si::Icon::Export);
        captureBtn.onClick = [this] { proc.toggleCapture(); };
        previewBtn.onClick = [this] { proc.requestPreview(); };
        loadBtn.onClick = [this]
        {
            chooser = std::make_unique<FileChooser> ("Load audio", File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
            {
                if (fc.getResult().existsAsFile()) proc.loadFile (fc.getResult());
            });
        };
        exportBtn.onClick = [this]
        {
            const auto f = proc.writeRenderToFile (false);
            if (f.existsAsFile()) f.revealToUser();
        };
        captureBtn.setTooltip ("Arm, then play your vocal or clip in the DAW. Stops by itself after 1.2 s of silence.");
        previewBtn.setTooltip ("Play the riser through the plugin. With SYNC on and the transport running, the DROP lands on the next bar.");
        syncBtn.setTooltip ("Preview starts so the drop hits the next bar line");
        thruBtn.setTooltip ("Pass the track's own audio through the plugin");
        pill.setTooltip ("Drag the rendered WAV onto an audio track. Place its start one Length before the vocal.");
        feed.setTooltip ("How much of the source excites the reverb. Auto = first word or phrase.");
        p.presets.onChange = [this] { presetBar.refresh(); };
        resized();   // base ctor sized us before this class existed
        startTimerHz (30);
    }

    ~Root() override { proc.presets.onChange = nullptr; }

    /** Dev only (SI_UI_SELFTEST): optionally load SI_UI_SELFTEST_AUDIO, wait for the render, snapshot. */
    void selfTest()
    {
        if (SystemStats::getEnvironmentVariable ("SI_UI_SELFTEST", {}).isEmpty()) return;
        const File audio (SystemStats::getEnvironmentVariable ("SI_UI_SELFTEST_AUDIO", {}));
        if (audio.existsAsFile())
        {
            const int v0 = proc.resultVersion.load();
            if (proc.loadFile (audio))
                for (int i = 0; i < 150 && proc.resultVersion.load() == v0; ++i) Thread::sleep (20);
            wave.refreshPeaks();
            lastVersion = proc.resultVersion.load();
            si::uiSelfTest (*this, "SI Riser-rendered");
        }
        si::uiSelfTest (*this, "SI Riser");
    }

    void paint (Graphics& g) override
    {
        si::drawBackground (g, getLocalBounds());
        si::drawHeaderTitle (g, { 24, 16, 260, 44 }, "SI Riser", "Reverse Riser Engine");
        drawChip (g);

        si::drawPanel (g, { 24, 80, 872, 268 }, {}, true);
        si::drawPanel (g, { 24, 360, 196, 276 }, "Source");
        si::drawPanel (g, { 232, 360, 236, 276 }, "Reverb");
        si::drawPanel (g, { 480, 360, 248, 276 }, "Shape");
        si::drawPanel (g, { 740, 360, 156, 276 }, "FX");

        si::drawSmallLabel (g, { 40, 392, 56, 28 }, "Length");
        si::drawSmallLabel (g, { 40, 426, 56, 28 }, "Feed");
        si::drawSmallLabel (g, { 756, 502, 124, 14 }, "Rate");
    }

    void resized() override
    {
        presetBar.setBounds (318, 20, 300, 38);
        wave.setBounds (40, 96, 840, 180);
        Rectangle<int> row (40, 290, 840, 42);
        captureBtn.setBounds (row.removeFromLeft (128)); row.removeFromLeft (8);
        previewBtn.setBounds (row.removeFromLeft (120)); row.removeFromLeft (8);
        syncBtn.setBounds (row.removeFromLeft (86)); row.removeFromLeft (8);
        thruBtn.setBounds (row.removeFromLeft (84)); row.removeFromLeft (8);
        loadBtn.setBounds (row.removeFromLeft (88)); row.removeFromLeft (8);
        exportBtn.setBounds (row.removeFromLeft (100));
        pill.setBounds (row.removeFromRight (196));

        // source
        length.setBounds (100, 392, 104, 28);
        feed.setBounds (100, 426, 104, 28);
        tempoSeg.setBounds (40, 464, 164, 28);
        Rectangle<int> sk (40, 500, 164, 86);
        for (auto* k : { &deess, &gain, &bpmKnob }) k->setBounds (sk.removeFromLeft (54).reduced (1, 0));
        dryBtn.setBounds (40, 594, 78, 28);
        normBtn.setBounds (126, 594, 78, 28);

        // reverb
        mode.setBounds (248, 392, 204, 30);
        color.setBounds (248, 430, 204, 28);
        Rectangle<int> r1 (248, 466, 204, 80), r2 (248, 548, 204, 80);
        for (auto* k : { &size, &tail }) k->setBounds (r1.removeFromLeft (102).reduced (12, 0));
        for (auto* k : { &hicut, &locut }) k->setBounds (r2.removeFromLeft (102).reduced (12, 0));

        // shape
        Rectangle<int> s1 (496, 392, 216, 94);
        for (auto* k : { &shape, &pitch, &filter }) k->setBounds (s1.removeFromLeft (72).reduced (2, 0));
        ftype.setBounds (496, 494, 216, 28);
        Rectangle<int> s2 (496, 530, 216, 94);
        for (auto* k : { &wIn, &wOut, &post }) k->setBounds (s2.removeFromLeft (72).reduced (2, 0));

        // fx
        int y = 392;
        for (auto pr : { std::pair<Component*, Component*> { &phone, &chorus }, { &trem, &stut }, { &lofi, &echo } })
        {
            pr.first->setBounds (756, y, 60, 28);
            pr.second->setBounds (820, y, 60, 28);
            y += 34;
        }
        rate.setBounds (756, 520, 124, 28);
        accel.setBounds (756, 594, 124, 28);
    }

private:
    Rectangle<int> chipArea() const { return { 764, 20, 132, 38 }; }

    void drawChip (Graphics& g)
    {
        const bool manual = pv (s, "tempoSource") > 0.5f;
        const bool host = proc.hostBpmValid.load();
        const double q = proc.ppqNow.load();
        si::drawTempoChip (g, chipArea().toFloat(), proc.bpm.load(), manual ? "Manual" : (host ? "Host sync" : "No host"),
                           manual ? col::warn : (host ? col::accent : col::hint), proc.hostPlaying.load() ? (float) (q - std::floor (q)) : -1.0f);
    }

    void timerCallback() override
    {
        using St = SIRiserProcessor::Status;
        const auto st = proc.status.load();
        const bool rec = st == St::Armed || st == St::Recording;
        captureBtn.setToggleState (rec, dontSendNotification);
        captureBtn.setButtonText (st == St::Armed ? "Waiting" : (st == St::Recording ? "Stop" : "Capture"));
        si::setIcon (captureBtn, rec ? si::Icon::Stop : si::Icon::Record);
        const bool playing = proc.previewPlaying.load(), waiting = proc.previewWaiting.load();
        previewBtn.setToggleState (playing || waiting, dontSendNotification);
        previewBtn.setButtonText (waiting ? "Next bar" : (playing ? "Stop" : "Preview"));
        si::setIcon (previewBtn, playing || waiting ? si::Icon::Stop : si::Icon::Play);
        const bool manual = pv (s, "tempoSource") > 0.5f;
        bpmKnob.setEnabled (manual);
        bpmKnob.setAlpha (manual ? 1.0f : 0.45f);

        const int ver = proc.resultVersion.load();
        if (ver != lastVersion) { lastVersion = ver; wave.refreshPeaks(); pill.repaint(); }
        if (proc.hostPlaying.load() || ++idle % 8 == 0) repaint (chipArea().expanded (8));
    }

    SIRiserProcessor& proc;
    si::APVTS& s;
    si::PresetBar presetBar;
    WaveView wave;
    TextButton captureBtn { "Capture" }, previewBtn { "Preview" }, loadBtn { "Load" }, exportBtn { "Export" };
    si::Toggle syncBtn { s, "syncPreview", "Sync", si::Icon::Sync }, thruBtn { s, "monitor", "Thru", si::Icon::Thru };
    DragPill pill;
    si::Choice length { s, "length" }, feed { s, "feed" };
    si::Segmented tempoSeg { s, "tempoSource" };
    si::Knob deess { s, "deess", "De-Ess" }, gain { s, "gain", "Gain" }, bpmKnob { s, "tempoBpm", "BPM" };
    si::Toggle dryBtn { s, "includeDry", "+ Dry" }, normBtn { s, "normalize", "Norm" };
    si::Choice mode { s, "verbMode" };
    si::Segmented color { s, "verbColor", { "70s", "80s", "Now" } };
    si::Knob size { s, "size", "Size" }, tail { s, "tail", "Tail" }, hicut { s, "hicut", "Hi Cut" }, locut { s, "locut", "Lo Cut" };
    si::Knob shape { s, "shape", "Curve", true }, pitch { s, "pitch", "Pitch" }, filter { s, "filter", "Filter" };
    si::Segmented ftype { s, "filterType", { "LP", "HP", "BP" } };
    si::Knob wIn { s, "widthStart", "Width In" }, wOut { s, "widthEnd", "Width Out" }, post { s, "post", "Post Verb" };
    si::Toggle phone { s, "phone", "Phone" }, chorus { s, "chorus", "Chorus" }, trem { s, "tremolo", "Trem" },
        stut { s, "stutter", "Stutter" }, lofi { s, "lofi", "Lo-Fi" }, echo { s, "echo", "Echo" };
    si::Choice rate { s, "fxRate" };
    si::Toggle accel { s, "accel", "Accelerate" };
    std::unique_ptr<FileChooser> chooser;
    int lastVersion = -1, idle = 0;
};
} // namespace

//==============================================================================
SIRiserEditor::SIRiserEditor (SIRiserProcessor& p) : AudioProcessorEditor (p)
{
    setLookAndFeel (&lnf);
    root = std::make_unique<Root> (p);
    addAndMakeVisible (*root);
    setResizable (true, true);
    setResizeLimits (W * 3 / 4, H * 3 / 4, W * 3 / 2, H * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) W / H);
    setSize (W, H);
    static_cast<Root*> (root.get())->selfTest();
}

SIRiserEditor::~SIRiserEditor() { setLookAndFeel (nullptr); }

void SIRiserEditor::resized()
{
    if (root) static_cast<si::ScaledContent*> (root.get())->fitInto (getLocalBounds());
}
