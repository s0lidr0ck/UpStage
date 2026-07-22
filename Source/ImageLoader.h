#pragma once

#include <JuceHeader.h>

#if JUCE_WINDOWS
 #include <wincodec.h>
 #pragma comment (lib, "windowscodecs.lib")
#endif

/**
 * Image loading for library pictures. JUCE decodes png/jpeg/gif natively;
 * anything else (notably webp) falls back to the Windows Imaging Component,
 * which uses whatever codecs the OS has installed (Windows 11 ships webp).
 */
namespace UpStageImages
{
    /** File-chooser pattern for every place that picks a picture. */
    inline const char* filePattern() { return "*.png;*.jpg;*.jpeg;*.webp"; }

    inline bool hasSupportedExtension (const juce::String& path)
    {
        return path.endsWithIgnoreCase (".png") || path.endsWithIgnoreCase (".jpg")
            || path.endsWithIgnoreCase (".jpeg") || path.endsWithIgnoreCase (".webp");
    }

   #if JUCE_WINDOWS
    inline juce::Image loadViaWic (const juce::File& f)
    {
        juce::Image result;

        IWICImagingFactory* factory = nullptr;
        if (FAILED (CoCreateInstance (CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS (&factory))) || factory == nullptr)
            return result;

        IWICBitmapDecoder* decoder = nullptr;
        if (SUCCEEDED (factory->CreateDecoderFromFilename (f.getFullPathName().toWideCharPointer(),
                                                           nullptr, GENERIC_READ,
                                                           WICDecodeMetadataCacheOnDemand, &decoder))
            && decoder != nullptr)
        {
            IWICBitmapFrameDecode* frame = nullptr;
            if (SUCCEEDED (decoder->GetFrame (0, &frame)) && frame != nullptr)
            {
                IWICBitmapSource* converted = nullptr;
                if (SUCCEEDED (WICConvertBitmapSource (GUID_WICPixelFormat32bppPBGRA,
                                                       frame, &converted))
                    && converted != nullptr)
                {
                    UINT w = 0, h = 0;
                    if (SUCCEEDED (converted->GetSize (&w, &h)) && w > 0 && h > 0)
                    {
                        juce::Image img (juce::Image::ARGB, (int) w, (int) h, true);
                        juce::Image::BitmapData data (img, juce::Image::BitmapData::writeOnly);

                        if (data.pixelStride == 4
                            && SUCCEEDED (converted->CopyPixels (nullptr, (UINT) data.lineStride,
                                                                 (UINT) (data.lineStride * (int) h),
                                                                 data.data)))
                            result = img;
                    }
                    converted->Release();
                }
                frame->Release();
            }
            decoder->Release();
        }
        factory->Release();
        return result;
    }
   #endif

    /** Loads a picture file: JUCE codecs first, then the OS (webp etc.). */
    inline juce::Image load (const juce::File& f)
    {
        if (! f.existsAsFile())
            return {};

        auto img = juce::ImageCache::getFromFile (f);
        if (img.isValid())
            return img;

       #if JUCE_WINDOWS
        return loadViaWic (f);
       #else
        return {};
       #endif
    }
}
