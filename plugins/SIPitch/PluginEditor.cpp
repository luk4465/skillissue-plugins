#include "PluginEditor.h"
#include <set>

using namespace juce;
namespace col = si::col;
using si::Weight;
using si::pitch::Note;

namespace
{
constexpr int W = 1100, H = 660;
const char* kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

String noteName (int midi) { return String (kNoteNames[((midi % 12) + 12) % 12]) + String (midi / 12 - 1); }

String clock (double s)
{
    s = jmax (0.0, s);
    const int m = (int) (s / 60.0);
    return String (m) + ":" + String (s - m * 60.0, 1).paddedLeft ('0', 4);
}

String centsText (float c)
{
    const int v = roundToInt (c * 100.0f);
    return (v > 0 ? "+" : "") + String (v) + " ct";
}

void setParam (si::APVTS& s, const String& id, float v)
{
    if (auto* p = s.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

//==============================================================================
/** Melodyne-style note view: blobs (thickness = loudness) at each note's pitch centre, the pitch line on top. */
class PianoRoll : public Component, public FileDragAndDropTarget, private Timer
{
public:
    explicit PianoRoll (SIPitchProcessor& p) : proc (p)
    {
        setWantsKeyboardFocus (true);
        startTimerHz (30);
    }

    std::function<void (const String&)> onInfo;

    void refreshNow() { timerCallback(); }
    void selectFirst() { if (! notes.empty()) selected = { 0 }; repaint(); }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r, 10.0f);
        const auto roll = rollArea();
        const float semi = semiPx();

        // rows + keyboard
        const int lo = (int) std::floor (topMidi - semis), hi = (int) std::ceil (topMidi);
        for (int m = lo; m <= hi; ++m)
        {
            const float y = yOf ((float) m + 0.5f);
            const Rectangle<float> row (roll.getX(), y, roll.getWidth(), semi);
            const bool inScale = (corr.scaleMask >> (((m % 12) + 12) % 12)) & 1;
            if (inScale) { g.setColour (Colours::white.withAlpha (0.028f)); g.fillRect (row.getIntersection (roll)); }
            g.setColour (Colours::white.withAlpha (m % 12 == 0 ? 0.10f : 0.035f));
            if (row.getY() > roll.getY()) g.fillRect (roll.getX(), row.getY(), roll.getWidth(), 1.0f);
            // key strip
            const bool black = (1 << (((m % 12) + 12) % 12)) & 0x54A;
            const Rectangle<float> key (2.0f, y, (float) kKeys - 4.0f, semi);
            if (key.getBottom() < 0 || key.getY() > (float) getHeight()) continue;
            g.setColour (black ? Colours::black.withAlpha (0.45f) : Colours::white.withAlpha (inScale ? 0.10f : 0.05f));
            g.fillRect (key.reduced (0.0f, 0.5f));
            if (semi >= 11.0f || m % 12 == 0)
            {
                g.setColour (inScale ? col::text.withAlpha (0.85f) : col::hint);
                g.setFont (si::font (jmin (11.0f, semi * 0.8f), inScale ? Weight::SemiBold : Weight::Regular));
                g.drawText (noteName (m), key.reduced (4.0f, 0.0f), Justification::centredLeft, false);
            }
        }

        if (take == nullptr) { paintEmpty (g, roll); return; }

        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (roll.toNearestInt());

        // bars / beats from the host tempo
        const double hs = (double) take->hostStart / take->sr;
        const double beat = 60.0 / jmax (30.0, proc.bpm.load());
        const double t0 = viewStart + hs, t1 = viewStart + viewLen + hs;
        const bool showBeats = viewLen / beat < 64.0;
        for (int64 b = (int64) std::floor (t0 / beat); b <= (int64) std::ceil (t1 / beat); ++b)
        {
            const bool bar = b % 4 == 0;
            if (! bar && ! showBeats) continue;
            const float x = xOf (b * beat - hs);
            g.setColour (Colours::white.withAlpha (bar ? 0.09f : 0.035f));
            g.fillRect (x, roll.getY(), 1.0f, roll.getHeight());
            if (bar && (viewLen / beat < 160.0 || b % 16 == 0))
            {
                g.setColour (col::hint);
                g.setFont (si::font (10.0f, Weight::SemiBold));
                g.drawText (String (b / 4 + 1), Rectangle<float> (x + 4.0f, roll.getY() + 3.0f, 40.0f, 12.0f), Justification::centredLeft, false);
            }
        }

        const auto& an = take->an;
        const double sr = take->sr;
        const float peak = jmax (1e-6f, an.peakRms);
        for (int i = 0; i < (int) notes.size(); ++i)
        {
            const auto& nt = notes[(size_t) i];
            const float x0 = xOf (nt.start / sr), x1 = xOf (nt.end / sr);
            if (x1 < roll.getX() || x0 > roll.getRight()) continue;
            const float dc = si::pitch::correctedCenter (nt, corr);
            const float yc = yOf (dc);
            const bool sel = selected.count (i) > 0;
            const bool moved = std::abs (dc - nt.center) > 0.03f;
            // where it was
            if (moved)
            {
                g.setColour (Colours::white.withAlpha (0.16f));
                g.drawRoundedRectangle (Rectangle<float> (x0, yOf (nt.center) - semi * 0.3f, x1 - x0, semi * 0.6f), 3.0f, 1.0f);
            }
            // blob
            Path blob;
            const int f0 = an.frameOf (nt.start), f1 = an.frameOf (nt.end);
            const int stepF = jmax (1, (int) ((f1 - f0) / jmax (1.0f, (x1 - x0) / 2.0f)));
            std::vector<Point<float>> top;
            for (int f = f0; f <= f1; f += stepF)
            {
                const float amp = std::sqrt (jlimit (0.0f, 1.0f, an.rms[(size_t) f] / peak));
                const float th = jmin (semi * 0.95f, jmax (3.0f, semi * (0.15f + 0.8f * amp)));
                top.push_back ({ jlimit (x0, x1, xOf (f * an.hop / sr)), th * 0.5f });
            }
            if (top.size() < 2) top = { { x0, semi * 0.3f }, { x1, semi * 0.3f } };
            top.front().x = x0;
            top.back().x = x1;
            blob.startNewSubPath (top.front().x, yc - top.front().y);
            for (auto& p : top) blob.lineTo (p.x, yc - p.y);
            for (auto it = top.rbegin(); it != top.rend(); ++it) blob.lineTo (it->x, yc + it->y);
            blob.closeSubPath();
            blob = blob.createPathWithRoundedCorners (3.0f);
            const Colour base = sel ? col::accent : (nt.manual ? col::accentHi : col::info);
            const float sungA = nt.manual || sel ? 1.0f : 0.3f + 0.7f * nt.sung;   // spoken syllables: faint
            g.setColour (base.withAlpha ((sel ? 0.85f : 0.55f) * sungA));
            g.fillPath (blob);
            g.setColour (base.brighter (0.3f).withAlpha ((sel ? 1.0f : 0.7f) * sungA));
            g.strokePath (blob, PathStrokeType (1.0f));
            // note name when there is room
            if (x1 - x0 > 34.0f && semi >= 9.0f)
            {
                g.setColour (Colours::white.withAlpha (0.85f));
                g.setFont (si::font (10.0f, Weight::Bold));
                const int m = roundToInt (dc);
                g.drawText (noteName (m) + (std::abs (dc - (float) m) > 0.04f ? " " + centsText (dc - (float) m) : String()),
                            Rectangle<float> (x0 + 4.0f, yc - semi * 0.5f - 13.0f, 90.0f, 12.0f), Justification::centredLeft, false);
            }
        }

        // pitch line (what you will hear)
        if (! off.empty())
        {
            Path line;
            bool open = false;
            const int fa = jmax (0, an.frameOf ((int) (viewStart * sr)) - 1), fb = jmin ((int) an.midi.size() - 1, an.frameOf ((int) ((viewStart + viewLen) * sr)) + 1);
            const int stepF = jmax (1, (int) ((fb - fa) / jmax (1.0f, roll.getWidth())));
            for (int f = fa; f <= fb; f += stepF)
            {
                const float mm = an.midi[(size_t) f];
                if (mm <= 0.0f) { open = false; continue; }
                const Point<float> p (xOf (f * an.hop / sr), yOf (mm + off[(size_t) f]));
                if (! open) { line.startNewSubPath (p); open = true; }
                else line.lineTo (p);
            }
            g.setColour (Colours::white.withAlpha (0.7f));
            g.strokePath (line, PathStrokeType (1.3f, PathStrokeType::curved, PathStrokeType::rounded));
        }

        // marquee + playhead
        if (dragMode == Drag::Marquee)
        {
            g.setColour (col::accent.withAlpha (0.12f));
            g.fillRect (marquee);
            g.setColour (col::accent.withAlpha (0.6f));
            g.drawRect (marquee, 1.0f);
        }
        const double ph = proc.hostSeconds.load() - hs;
        if (ph >= viewStart && ph <= viewStart + viewLen)
        {
            g.setColour (col::accentHi);
            g.fillRect (xOf (ph) - 0.5f, roll.getY(), 1.5f, roll.getHeight());
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        grabKeyboardFocus();
        if (take == nullptr) return;
        down = e.position;
        moved = false;
        const int hit = noteAt (e.position);
        if (hit >= 0)
        {
            if (e.mods.isShiftDown()) { if (! selected.erase (hit)) selected.insert (hit); }
            else if (! selected.count (hit)) selected = { hit };
            dragMode = Drag::Move;
            startCenters.clear();
            for (int i : selected) startCenters[i] = si::pitch::correctedCenter (notes[(size_t) i], corr);
            info (hit);
        }
        else
        {
            if (! e.mods.isShiftDown()) selected.clear();
            dragMode = Drag::Marquee;
            marquee = {};
        }
        repaint();
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (dragMode == Drag::Move)
        {
            const float dy = (down.y - e.position.y) / semiPx();
            if (! moved && std::abs (dy) < 0.3f) return;
            moved = true;
            for (auto& [i, c0] : startCenters)
            {
                auto& nt = notes[(size_t) i];
                nt.manual = true;
                nt.target = e.mods.isAltDown() ? c0 + dy : (float) std::round (c0 + dy);
            }
            if (! selected.empty()) info (*selected.begin());
            updateOffsets();
            repaint();
        }
        else if (dragMode == Drag::Marquee)
        {
            marquee = Rectangle<float> (down, e.position).getIntersection (rollArea());
            selected.clear();
            for (int i = 0; i < (int) notes.size(); ++i)
                if (noteRect (i).intersects (marquee)) selected.insert (i);
            repaint();
        }
    }

    void mouseUp (const MouseEvent&) override
    {
        if (dragMode == Drag::Move && moved) push();
        dragMode = Drag::None;
        repaint();
    }

    void mouseDoubleClick (const MouseEvent& e) override
    {
        const int hit = noteAt (e.position);
        if (hit < 0) return;
        notes[(size_t) hit].manual = false;   // back to the key / Correct settings
        push();
        info (hit);
    }

    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        if (take == nullptr) return;
        const float d = (std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
        if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            const double at = tOf (e.position.x);
            viewLen = jlimit (0.5, jmax (1.0, takeSeconds() + 2.0), viewLen * std::pow (0.8, d * 4.0));
            viewStart = at - (e.position.x - rollArea().getX()) / rollArea().getWidth() * viewLen;
        }
        else if (e.mods.isAltDown())
        {
            const float mid = topMidi - semis * 0.5f;
            semis = jlimit (8.0f, 60.0f, semis * (float) std::pow (0.8, d * 4.0));
            topMidi = mid + semis * 0.5f;
        }
        else if (e.mods.isShiftDown()) topMidi += d * 6.0f;
        else viewStart -= d * viewLen * 0.35;
        clampView();
        followPlayhead = false;
        repaint();
    }

    bool keyPressed (const KeyPress& k) override
    {
        if (take == nullptr) return false;
        if (k == KeyPress ('a', ModifierKeys::commandModifier, 0)) { selected.clear(); for (int i = 0; i < (int) notes.size(); ++i) selected.insert (i); repaint(); return true; }
        if (k.getKeyCode() == KeyPress::upKey || k.getKeyCode() == KeyPress::downKey)
        {
            const float d = (k.getKeyCode() == KeyPress::upKey ? 1.0f : -1.0f) * (k.getModifiers().isAltDown() ? 0.1f : 1.0f);
            for (int i : selected) { auto& nt = notes[(size_t) i]; const float c = si::pitch::correctedCenter (nt, corr); nt.manual = true; nt.target = c + d; }
            push();
            return true;
        }
        if (k.getKeyCode() == KeyPress::deleteKey || k.getKeyCode() == KeyPress::backspaceKey)
        {
            for (int i : selected) notes[(size_t) i].manual = false;
            push();
            return true;
        }
        if (k.getKeyCode() == KeyPress::escapeKey) { selected.clear(); repaint(); return true; }
        return false;
    }

    void resetAllManual()
    {
        for (auto& nt : notes) nt.manual = false;
        push();
    }

    // ---- drop an audio file in: it is analysed and placed at the playhead
    static bool isAudio (const String& f) { return File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg") && ! f.contains ("SI Pitch Exports"); }
    bool isInterestedInFileDrag (const StringArray& files) override
    {
        for (auto& f : files) if (isAudio (f)) return true;
        return false;
    }
    void fileDragEnter (const StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const StringArray&) override { dropHover = false; repaint(); }
    void filesDropped (const StringArray& files, int, int) override
    {
        dropHover = false;
        for (auto& f : files)
            if (isAudio (f))
            {
                const bool ok = proc.loadFile (File (f));
                if (onInfo) onInfo (ok ? "Loading " + File (f).getFileName() + " at " + clock (proc.hostSeconds.load()) + " ..."
                                       : String ("Busy - wait until the current take is analysed"));
                break;
            }
        repaint();
    }
    void paintOverChildren (Graphics& g) override
    {
        if (! dropHover) return;
        auto r = getLocalBounds().toFloat().reduced (6.0f);
        g.setColour (col::accent.withAlpha (0.10f));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (col::accent.withAlpha (0.85f));
        g.drawRoundedRectangle (r, 10.0f, 1.5f);
        si::drawIcon (g, si::Icon::Down, r.withSizeKeepingCentre (28.0f, 28.0f).translated (0.0f, -26.0f), col::accentHi);
        g.setColour (col::text);
        g.setFont (si::font (15.0f, Weight::SemiBold));
        g.drawText ("Drop to load it at the playhead (" + clock (proc.hostSeconds.load()) + ")", r.translated (0.0f, 10.0f), Justification::centred, true);
    }

private:
    enum class Drag { None, Move, Marquee };
    static constexpr int kKeys = 46;

    void timerCallback() override
    {
        auto t = proc.getTake();
        if (t != take)
        {
            take = t;
            notes = proc.getNotes();
            selected.clear();
            fitView();
            updateOffsets();
        }
        const auto c = proc.currentCorrection();
        if (c.scaleMask != corr.scaleMask || c.center != corr.center || c.drift != corr.drift || c.vibrato != corr.vibrato)
        {
            corr = c;
            updateOffsets();
        }
        // follow the playhead while the host plays
        if (take != nullptr && proc.playing.load() && dragMode == Drag::None)
        {
            const double ph = proc.hostSeconds.load() - (double) take->hostStart / take->sr;
            if (! wasPlaying) followPlayhead = true;
            if (followPlayhead && ph > 0.0 && ph < takeSeconds() && (ph < viewStart || ph > viewStart + viewLen * 0.85))
                viewStart = ph - viewLen * 0.1;
            clampView();
        }
        wasPlaying = proc.playing.load();
        repaint();
    }

    void paintEmpty (Graphics& g, Rectangle<float> roll)
    {
        auto r = roll.withSizeKeepingCentre (520.0f, 120.0f);
        const auto st = proc.getStatus();
        si::drawIcon (g, st == SIPitchProcessor::Status::Analysing ? si::Icon::Wave : si::Icon::Record,
                      r.removeFromTop (40.0f).withSizeKeepingCentre (30.0f, 30.0f),
                      st == SIPitchProcessor::Status::Recording ? col::rec : col::accentHi);
        g.setColour (col::text);
        g.setFont (si::font (16.0f, Weight::SemiBold));
        g.drawText (proc.getMessage(), r.removeFromTop (28.0f), Justification::centred, true);
        if (st == SIPitchProcessor::Status::Analysing)
        {
            auto bar = r.removeFromTop (20.0f).withSizeKeepingCentre (300.0f, 6.0f);
            g.setColour (Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (bar, 3.0f);
            g.setGradientFill (si::accentGradient (bar.getTopLeft(), bar.getTopRight()));
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * proc.getProgress()), 3.0f);
        }
        else
        {
            g.setColour (col::label);
            g.setFont (si::font (12.0f));
            g.drawText ("Put SI Pitch first on the vocal track. Transfer records it against the timeline,", r.removeFromTop (18.0f), Justification::centred, true);
            g.drawText ("or drop an audio file here (it lands at the playhead).", r.removeFromTop (18.0f), Justification::centred, true);
        }
    }

    Rectangle<float> rollArea() const { return getLocalBounds().toFloat().withTrimmedLeft ((float) kKeys); }
    float semiPx() const { return (float) getHeight() / semis; }
    float xOf (double t) const { const auto r = rollArea(); return r.getX() + (float) ((t - viewStart) / viewLen) * r.getWidth(); }
    double tOf (float x) const { const auto r = rollArea(); return viewStart + (x - r.getX()) / r.getWidth() * viewLen; }
    float yOf (float midi) const { return (topMidi - midi) * semiPx(); }
    double takeSeconds() const { return take != nullptr ? take->audio.getNumSamples() / take->sr : 10.0; }

    Rectangle<float> noteRect (int i) const
    {
        const auto& nt = notes[(size_t) i];
        const float dc = si::pitch::correctedCenter (nt, corr), s = semiPx();
        const float x0 = xOf (nt.start / take->sr), x1 = xOf (nt.end / take->sr);
        return { x0, yOf (dc) - jmax (5.0f, s * 0.5f), x1 - x0, jmax (10.0f, s) };
    }

    int noteAt (Point<float> p) const
    {
        for (int i = 0; i < (int) notes.size(); ++i)
            if (noteRect (i).contains (p)) return i;
        return -1;
    }

    void fitView()
    {
        viewStart = 0.0;
        viewLen = jlimit (2.0, 14.0, takeSeconds());
        if (notes.empty()) { topMidi = 72.0f; semis = 24.0f; return; }
        float lo = 200.0f, hi = -1.0f;
        for (auto& nt : notes) { lo = jmin (lo, nt.center); hi = jmax (hi, nt.center); }
        semis = jlimit (18.0f, 48.0f, hi - lo + 8.0f);
        topMidi = (lo + hi) * 0.5f + semis * 0.5f;
    }

    void clampView()
    {
        viewStart = jlimit (-1.0, jmax (0.0, takeSeconds() - viewLen * 0.5), viewStart);
        topMidi = jlimit (20.0f + semis, 110.0f, topMidi);
    }

    void updateOffsets()
    {
        if (take == nullptr) { off.clear(); return; }
        off = si::pitch::offsetCurve (take->an, notes, corr);
    }

    void push()
    {
        proc.setNotes (notes);
        updateOffsets();
        repaint();
    }

    void info (int i)
    {
        if (! onInfo || i < 0 || i >= (int) notes.size()) return;
        const auto& nt = notes[(size_t) i];
        const float dc = si::pitch::correctedCenter (nt, corr);
        const int m = roundToInt (nt.center);
        onInfo ("Note sung " + noteName (m) + " " + centsText (nt.center - (float) m) + "  ->  " + noteName (roundToInt (dc))
                + (std::abs (dc - std::round (dc)) > 0.04f ? " " + centsText (dc - std::round (dc)) : String())
                + (nt.manual ? "  (moved by hand, double-click = back to auto)"
                             : (nt.sung < 0.5f ? "  (spoken / glide: Correct leaves it, drag it to move it)" : "  (auto)")));
    }

    SIPitchProcessor& proc;
    std::shared_ptr<const SIPitchProcessor::Take> take;
    std::vector<Note> notes;
    si::pitch::Correction corr;
    std::vector<float> off;
    std::set<int> selected;
    std::map<int, float> startCenters;
    Drag dragMode = Drag::None;
    Point<float> down;
    Rectangle<float> marquee;
    bool moved = false, followPlayhead = true, wasPlaying = false, dropHover = false;
    double viewStart = 0.0, viewLen = 10.0;
    float topMidi = 72.0f, semis = 24.0f;
};

//==============================================================================
/** "DRAG TO DAW": drag the corrected vocal onto a track; a plain click saves it and shows the file. */
class DragPill : public Component, public SettableTooltipClient
{
public:
    explicit DragPill (SIPitchProcessor& p) : proc (p) { setMouseCursor (MouseCursor::DraggingHandCursor); }
    std::function<void (const String&)> onInfo;

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool ok = proc.getTake() != nullptr;
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
        si::drawIcon (g, si::Icon::Grip, Rectangle<float> (r.getX() + 12.0f, r.getCentreY() - 7.0f, 14.0f, 14.0f), c);
        g.setColour (c);
        g.setFont (si::font (11.0f, Weight::SemiBold, 0.1f));
        g.drawText ("DRAG TO DAW", r.withTrimmedLeft (14.0f), Justification::centred);
    }
    void mouseDrag (const MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 5 || proc.getTake() == nullptr) return;
        dragging = true;
        const auto f = proc.exportRendered();
        if (f.existsAsFile())
        {
            if (onInfo) onInfo ("Drop it on an audio track at " + clock ((double) proc.getTake()->hostStart / proc.getTake()->sr) + " (the take's start)");
            DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
        }
    }
    void mouseUp (const MouseEvent& e) override
    {
        if (! dragging && e.getDistanceFromDragStart() < 5 && proc.getTake() != nullptr)
        {
            const auto f = proc.exportRendered();
            if (f.existsAsFile()) { f.revealToUser(); if (onInfo) onInfo ("Saved " + f.getFileName()); }
        }
        dragging = false;
    }

private:
    SIPitchProcessor& proc;
    bool dragging = false;
};

//==============================================================================
class Root : public si::ScaledContent, private Timer
{
public:
    explicit Root (SIPitchProcessor& p) : si::ScaledContent (W, H), proc (p), s (p.apvts), roll (p)
    {
        for (auto* c : std::initializer_list<Component*> { &transfer, &keyBox, &scaleBox, &correct, &reset, &pill, &roll, &center, &drift, &vibrato })
            addAndMakeVisible (c);
        si::setIcon (transfer, si::Icon::Record);
        transfer.getProperties().set ("rec", true);
        transfer.setTooltip ("Arm, then press play in your DAW: the vocal is recorded against the timeline (stop the DAW to finish)");
        transfer.onClick = [this] { proc.setArmed (! proc.isArmed()); };
        correct.getProperties().set ("cta", true);
        correct.setTooltip ("Puts every note's centre on the nearest note of the key, keeps vibrato and slides");
        correct.onClick = [this]
        {
            setParam (s, "center", 100.0f);
            setParam (s, "drift", 30.0f);
        };
        reset.setTooltip ("Back to the original performance (clears hand edits)");
        reset.onClick = [this]
        {
            setParam (s, "center", 0.0f);
            setParam (s, "drift", 0.0f);
            setParam (s, "vibrato", 100.0f);
            roll.resetAllManual();
        };
        pill.setTooltip ("Drag the corrected vocal onto an audio track (put it at the take's start). Click = save the WAV and show it");
        pill.onInfo = [this] (const String& t) { infoText = t; repaint (infoArea()); };
        center.slider.setTooltip ("How far each note's centre moves onto the key (100% = exactly in tune)");
        drift.slider.setTooltip ("Flattens slow wandering inside a note; vibrato is not touched");
        vibrato.slider.setTooltip ("Scales the singer's own vibrato (100% = untouched)");
        roll.onInfo = [this] (const String& t) { infoText = t; repaint (infoArea()); };
        resized();
        timerCallback();
        startTimerHz (10);
    }

    PianoRoll& getRoll() { return roll; }

    void paint (Graphics& g) override
    {
        si::drawBackground (g, getLocalBounds());
        si::drawHeaderTitle (g, { 24, 16, 250, 44 }, "SI Pitch", "Note Editor");
        si::drawPanel (g, { 24, 76, 1052, 452 }, {}, true);
        si::drawPanel (g, { 24, 540, 1052, 104 }, "Correct Pitch");
        g.setColour (col::text);
        g.setFont (si::font (13.0f, Weight::SemiBold));
        g.drawText (proc.getMessage(), statusArea(), Justification::centredLeft, true);
        g.setColour (infoText.isNotEmpty() ? col::accentHi : col::label);
        g.setFont (si::font (11.5f));
        g.drawText (infoText.isNotEmpty() ? infoText : String ("Drag a note = move by semitones   Alt+drag = fine   Double-click = back to auto   Ctrl+wheel = zoom   Alt+wheel = height"),
                    infoArea(), Justification::centredLeft, true);
    }

    void resized() override
    {
        transfer.setBounds (290, 20, 128, 36);
        keyBox.setBounds (430, 20, 104, 36);
        scaleBox.setBounds (542, 20, 150, 36);
        correct.setBounds (704, 20, 120, 36);
        reset.setBounds (832, 20, 96, 36);
        pill.setBounds (936, 20, 140, 36);
        roll.setBounds (36, 88, 1028, 428);
        center.setBounds (40, 566, 96, 74);
        drift.setBounds (144, 566, 96, 74);
        vibrato.setBounds (248, 566, 96, 74);
    }

private:
    Rectangle<int> statusArea() const { return { 372, 572, 690, 22 }; }
    Rectangle<int> infoArea() const { return { 372, 600, 690, 30 }; }

    void timerCallback() override
    {
        const auto st = proc.getStatus();
        const bool on = proc.isArmed() || st == SIPitchProcessor::Status::Recording;
        transfer.setToggleState (on, dontSendNotification);
        transfer.setButtonText (st == SIPitchProcessor::Status::Recording ? "Recording" : (on ? "Armed" : "Transfer"));
        const bool has = proc.getTake() != nullptr;
        pill.repaint();
        correct.setEnabled (has);
        transfer.setEnabled (st != SIPitchProcessor::Status::Analysing);
        repaint (statusArea());
    }

    SIPitchProcessor& proc;
    si::APVTS& s;
    PianoRoll roll;
    TextButton transfer { "Transfer" }, correct { "Correct" }, reset { "Reset" };
    DragPill pill { proc };
    si::Choice keyBox { s, "key" }, scaleBox { s, "scale" };
    si::Knob center { s, "center", "Pitch Center" }, drift { s, "drift", "Pitch Drift" }, vibrato { s, "vibrato", "Vibrato" };
    String infoText;
};
} // namespace

//==============================================================================
SIPitchEditor::SIPitchEditor (SIPitchProcessor& p) : AudioProcessorEditor (p)
{
    setLookAndFeel (&lnf);
    auto r = std::make_unique<Root> (p);
    auto* rp = r.get();
    root = std::move (r);
    addAndMakeVisible (*root);
    setResizable (true, true);
    setResizeLimits (W * 3 / 4, H * 3 / 4, W * 3 / 2, H * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) W / H);
    setSize (W, H);
    if (SystemStats::getEnvironmentVariable ("SI_UI_SELFTEST", {}).isNotEmpty())
    {
        const File demo (SystemStats::getEnvironmentVariable ("SI_UI_SELFTEST_AUDIO", {}));
        if (demo.existsAsFile() && p.loadTakeSync (demo))
        {
            setParam (p.apvts, "center", 100.0f);
            rp->getRoll().refreshNow();
            rp->getRoll().selectFirst();
        }
        si::uiSelfTest (*root, "SI Pitch");
    }
}

SIPitchEditor::~SIPitchEditor() { setLookAndFeel (nullptr); }

void SIPitchEditor::resized()
{
    if (root) static_cast<si::ScaledContent*> (root.get())->fitInto (getLocalBounds());
}
