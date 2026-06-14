#include "BundlePaths.h"

#if JUCE_MAC || JUCE_LINUX
 #include <dlfcn.h>
#endif

namespace BundlePaths
{
    static juce::File thisBinaryFile()
    {
       #if JUCE_MAC || JUCE_LINUX
        Dl_info info;
        if (dladdr ((const void*) &thisBinaryFile, &info) != 0 && info.dli_fname != nullptr)
            return juce::File (juce::CharPointer_UTF8 (info.dli_fname));
       #endif
        return {};
    }

    juce::File resourcesDir()
    {
        const auto bin = thisBinaryFile();
        if (bin == juce::File())
            return {};

       #if JUCE_MAC
        // .../Contents/MacOS/<binary> -> .../Contents/Resources
        auto res = bin.getParentDirectory()         // MacOS
                      .getParentDirectory()          // Contents
                      .getChildFile ("Resources");
        return res.isDirectory() ? res : juce::File();
       #else
        // VST3 on Win/Linux: .../Contents/<arch>/Vocalizer.* with Resources at
        // .../Contents/Resources.
        auto res = bin.getParentDirectory().getParentDirectory().getChildFile ("Resources");
        return res.isDirectory() ? res : juce::File();
       #endif
    }

    juce::File bundledEspeakDir()
    {
        const auto res = resourcesDir();
        if (res != juce::File() && res.getChildFile ("espeak-ng-data").isDirectory())
            return res;
        return {};
    }

    juce::File bundledVoicesDir()
    {
        const auto res = resourcesDir();
        if (res != juce::File())
        {
            const auto v = res.getChildFile ("voices");
            if (v.isDirectory())
                return v;
        }
        return {};
    }
}
