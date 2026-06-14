#include "DragHandle.h"

DragHandle::DragHandle()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void DragHandle::setReady (bool isNowReady)
{
    if (ready != isNowReady)
    {
        ready = isNowReady;
        setMouseCursor (ready ? juce::MouseCursor::DraggingHandCursor
                              : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void DragHandle::mouseEnter (const juce::MouseEvent&) { hovered = true;  repaint(); }
void DragHandle::mouseExit  (const juce::MouseEvent&) { hovered = false; repaint(); }

void DragHandle::mouseDrag (const juce::MouseEvent&)
{
    if (dragging || ! ready || isReady == nullptr || getFileToDrag == nullptr)
        return;

    const juce::File file = getFileToDrag();
    if (! isReady() || ! file.existsAsFile())
        return;

    dragging = true;
    repaint();

    juce::StringArray files;
    files.add (file.getFullPathName());

    // External (OS) drag of the rendered WAV. canMoveFiles=false: the DAW copies
    // it on import; we keep ownership of the temp file (CLAUDE.md §4.6).
    juce::DragAndDropContainer::performExternalDragDropOfFiles (
        files, /*canMoveFiles*/ false, this,
        [safe = juce::Component::SafePointer<DragHandle> (this)]
        {
            if (safe != nullptr) { safe->dragging = false; safe->repaint(); }
        });
}

void DragHandle::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);

    const juce::Colour accent = ready ? NeonLookAndFeel::magenta : NeonLookAndFeel::dim;
    const float glow = ready ? (dragging ? 1.0f : (hovered ? 0.7f : 0.4f)) : 0.0f;

    // Chip background.
    g.setColour (NeonLookAndFeel::panel);
    g.fillRoundedRectangle (bounds, 8.0f);

    // Glow border (layered strokes).
    for (int i = 3; i >= 1 && glow > 0.0f; --i)
    {
        g.setColour (accent.withAlpha (glow * 0.12f * (float) i));
        g.drawRoundedRectangle (bounds.expanded ((float) i * 1.2f), 8.0f, 2.0f);
    }
    g.setColour (accent.withAlpha (ready ? 0.9f : 0.5f));
    g.drawRoundedRectangle (bounds, 8.0f, 1.4f);

    // Mini waveform glyph on the left.
    auto glyph = bounds.removeFromLeft (54.0f).reduced (14.0f, 10.0f);
    g.setColour (accent.withAlpha (ready ? 0.95f : 0.5f));
    const int bars = 7;
    const float bw = glyph.getWidth() / (float) (bars * 2 - 1);
    const float heights[bars] = { 0.35f, 0.7f, 0.5f, 1.0f, 0.55f, 0.8f, 0.4f };
    for (int i = 0; i < bars; ++i)
    {
        const float h = glyph.getHeight() * heights[i];
        const float x = glyph.getX() + (float) i * 2.0f * bw;
        g.fillRect (x, glyph.getCentreY() - h * 0.5f, bw, h);
    }

    // Label.
    g.setColour (accent.withAlpha (ready ? 1.0f : 0.55f));
    g.setFont (NeonLookAndFeel::monoFont (13.0f, true));
    g.drawText (ready ? "DRAG TO TRACK  >>" : "RENDER FIRST",
                bounds, juce::Justification::centred, false);
}
