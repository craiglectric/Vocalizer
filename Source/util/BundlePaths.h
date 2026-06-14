#pragma once

#include <juce_core/juce_core.h>

//==============================================================================
// BundlePaths — locate resources shipped INSIDE the plugin bundle at runtime
// (CLAUDE.md §6/§8 packaging). Uses the address of our own code to find the
// loaded binary, then walks to Contents/Resources. Returns an invalid File when
// running outside a bundle (e.g. the dev console tools), so callers fall back to
// the dev compile-def paths.
//==============================================================================
namespace BundlePaths
{
    // .../Xxx.vst3/Contents/Resources (and the AU equivalent), or invalid File.
    juce::File resourcesDir();

    // Directory that CONTAINS "espeak-ng-data" (espeak appends the folder name).
    // = resourcesDir() when the data is bundled; invalid otherwise.
    juce::File bundledEspeakDir();

    // resourcesDir()/voices, or invalid File.
    juce::File bundledVoicesDir();
}
