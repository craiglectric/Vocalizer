#include "WaveformDisplay.h"

void WaveformDisplay::setThumbnail (std::vector<float> minMaxPairs)
{
    thumb = std::move (minMaxPairs);
    repaint();
}

void WaveformDisplay::clearThumbnail()
{
    thumb.clear();
    showPlayhead = false;
    repaint();
}

void WaveformDisplay::setPlayhead (float normalised)
{
    playhead = normalised;
    repaint();
}

std::vector<float> WaveformDisplay::buildThumbnail (const float* samples, int numSamples,
                                                    int numColumns)
{
    std::vector<float> out;
    if (samples == nullptr || numSamples <= 0 || numColumns <= 0)
        return out;

    out.resize ((size_t) numColumns * 2);
    const double per = (double) numSamples / (double) numColumns;

    for (int c = 0; c < numColumns; ++c)
    {
        const int start = (int) (c * per);
        const int end   = juce::jmin (numSamples, (int) ((c + 1) * per));

        float lo = 0.0f, hi = 0.0f;
        for (int i = start; i < end; ++i)
        {
            lo = juce::jmin (lo, samples[i]);
            hi = juce::jmax (hi, samples[i]);
        }
        out[(size_t) c * 2]     = lo;
        out[(size_t) c * 2 + 1] = hi;
    }
    return out;
}

void WaveformDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Panel.
    g.setColour (NeonLookAndFeel::panel);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (NeonLookAndFeel::dim);
    g.drawRoundedRectangle (bounds, 8.0f, 1.2f);

    if (thumb.empty())
    {
        g.setColour (NeonLookAndFeel::dim.brighter (0.5f));
        g.setFont (NeonLookAndFeel::monoFont (12.0f));
        g.drawText ("no clip // hit GENERATE", bounds, juce::Justification::centred);
        return;
    }

    const auto area = bounds.reduced (8.0f);
    const int cols = (int) (thumb.size() / 2);
    const float midY = area.getCentreY();
    const float halfH = area.getHeight() * 0.5f;
    const float colW = area.getWidth() / (float) cols;

    // Waveform.
    g.setColour (NeonLookAndFeel::cyan.withAlpha (0.9f));
    for (int c = 0; c < cols; ++c)
    {
        const float lo = thumb[(size_t) c * 2];
        const float hi = thumb[(size_t) c * 2 + 1];
        const float x  = area.getX() + (float) c * colW;
        const float y1 = midY - hi * halfH;
        const float y2 = midY - lo * halfH;
        g.drawLine (x, y1, x, juce::jmax (y2, y1 + 1.0f), juce::jmax (1.0f, colW * 0.8f));
    }

    // Centre line.
    g.setColour (NeonLookAndFeel::dim.withAlpha (0.6f));
    g.drawHorizontalLine ((int) midY, area.getX(), area.getRight());

    // Playhead.
    if (showPlayhead)
    {
        const float px = area.getX() + juce::jlimit (0.0f, 1.0f, playhead) * area.getWidth();
        g.setColour (NeonLookAndFeel::magenta);
        g.drawLine (px, area.getY(), px, area.getBottom(), 1.6f);
    }
}
