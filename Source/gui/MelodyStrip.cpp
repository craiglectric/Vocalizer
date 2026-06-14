#include "MelodyStrip.h"
#include <algorithm>

void MelodyStrip::setMelody (const Melody& m)
{
    melody = m;

    lowNote = 48; highNote = 72;
    if (! melody.notes.empty())
    {
        int lo = 127, hi = 0;
        for (const auto& n : melody.notes) { lo = std::min (lo, n.note); hi = std::max (hi, n.note); }
        lowNote  = lo - 2;
        highNote = hi + 2;
        if (highNote - lowNote < 6) { highNote = lowNote + 6; }
    }
    repaint();
}

void MelodyStrip::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (NeonLookAndFeel::panel);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (NeonLookAndFeel::dim);
    g.drawRoundedRectangle (bounds, 8.0f, 1.2f);

    if (melody.notes.empty() || melody.durationSec <= 0.0)
    {
        g.setColour (NeonLookAndFeel::dim.brighter (0.5f));
        g.setFont (NeonLookAndFeel::monoFont (11.0f));
        g.drawText ("no melody // ARM then play a MIDI clip",
                    bounds, juce::Justification::centred);
        return;
    }

    const auto area = bounds.reduced (8.0f);
    const double dur = melody.durationSec;
    const float span = (float) juce::jmax (1, highNote - lowNote);

    auto noteY = [&] (int note)
    {
        const float t = ((float) note - (float) lowNote) / span;   // 0..1 low..high
        return area.getBottom() - t * area.getHeight();
    };

    g.setColour (NeonLookAndFeel::magenta.withAlpha (0.95f));
    for (const auto& n : melody.notes)
    {
        const float x1 = area.getX() + (float) (n.startSec / dur) * area.getWidth();
        const float x2 = area.getX() + (float) (n.endSec   / dur) * area.getWidth();
        const float y  = noteY (n.note);
        g.fillRoundedRectangle (x1, y - 2.0f, juce::jmax (3.0f, x2 - x1), 4.0f, 2.0f);
    }
}
