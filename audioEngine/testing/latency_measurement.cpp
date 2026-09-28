#define NOMINMAX

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>

#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <thread>
#include <chrono>

#pragma comment(lib, "ole32.lib")

// ============================================================
// SETTINGS
// ============================================================

constexpr int COBRA_INDEX = 0;
constexpr int MIVI_INDEX  = 1;

constexpr int TEST_SECONDS = 15;

constexpr double CLICK_FREQUENCY = 1000.0;

// Each click lasts 10 ms
constexpr double CLICK_DURATION_MS = 10.0;

constexpr double PI =
    3.1415926535897932384626433832795;


// ============================================================
// AUDIO OUTPUT
// ============================================================

struct AudioOutput
{
    IMMDevice* device = nullptr;

    IAudioClient* client = nullptr;

    IAudioRenderClient* render = nullptr;

    WAVEFORMATEX* format = nullptr;

    UINT32 bufferFrames = 0;

    UINT32 sampleRate = 0;

    UINT32 channels = 0;

    std::wstring name;
};


// ============================================================
// ERROR HELPER
// ============================================================

void printHR(
    HRESULT hr,
    const char* message
)
{
    if (FAILED(hr))
    {
        std::cerr
            << message
            << " HRESULT=0x"
            << std::hex
            << hr
            << std::dec
            << "\n";
    }
}


// ============================================================
// GET DEVICE NAME
// ============================================================

std::wstring getDeviceName(
    IMMDevice* device
)
{
    std::wstring result;

    IPropertyStore* properties = nullptr;

    HRESULT hr =
        device->OpenPropertyStore(
            STGM_READ,
            &properties
        );

    if (FAILED(hr))
        return result;

    PROPVARIANT name;

    PropVariantInit(&name);

    hr =
        properties->GetValue(
            PKEY_Device_FriendlyName,
            &name
        );

    if (
        SUCCEEDED(hr) &&
        name.vt == VT_LPWSTR
    )
    {
        result = name.pwszVal;
    }

    PropVariantClear(&name);

    properties->Release();

    return result;
}


// ============================================================
// ENUMERATE AUDIO OUTPUT DEVICES
// ============================================================

std::vector<IMMDevice*> enumerateDevices()
{
    std::vector<IMMDevice*> devices;

    IMMDeviceEnumerator* enumerator =
        nullptr;

    HRESULT hr =
        CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            (void**)&enumerator
        );

    if (FAILED(hr))
    {
        printHR(
            hr,
            "Failed to create device enumerator"
        );

        return devices;
    }

    IMMDeviceCollection* collection =
        nullptr;

    hr =
        enumerator->EnumAudioEndpoints(
            eRender,
            DEVICE_STATE_ACTIVE,
            &collection
        );

    if (FAILED(hr))
    {
        printHR(
            hr,
            "Failed to enumerate devices"
        );

        enumerator->Release();

        return devices;
    }

    UINT count = 0;

    collection->GetCount(&count);

    std::cout
        << "\nAvailable render devices:\n";

    for (
        UINT i = 0;
        i < count;
        ++i
    )
    {
        IMMDevice* device = nullptr;

        collection->Item(
            i,
            &device
        );

        std::wstring name =
            getDeviceName(device);

        std::wcout
            << L"["
            << i
            << L"] "
            << name
            << L"\n";

        devices.push_back(device);
    }

    collection->Release();

    enumerator->Release();

    return devices;
}


// ============================================================
// INITIALIZE OUTPUT
// ============================================================

bool initializeOutput(
    AudioOutput& output,
    IMMDevice* device
)
{
    output.device = device;

    output.name =
        getDeviceName(device);

    HRESULT hr =
        device->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            (void**)&output.client
        );

    if (FAILED(hr))
    {
        printHR(
            hr,
            "Failed to activate output"
        );

        return false;
    }

    hr =
        output.client->GetMixFormat(
            &output.format
        );

    if (FAILED(hr))
    {
        printHR(
            hr,
            "GetMixFormat failed"
        );

        return false;
    }

    output.sampleRate =
        output.format->nSamplesPerSec;

    output.channels =
        output.format->nChannels;

    std::wcout
        << L"\nOutput: "
        << output.name
        << L"\n";

    std::cout
        << "Sample rate: "
        << output.sampleRate
        << "\n";

    std::cout
        << "Channels: "
        << output.channels
        << "\n";

    std::cout
        << "Bits: "
        << output.format->wBitsPerSample
        << "\n";

    std::cout
        << "Format tag: "
        << output.format->wFormatTag
        << "\n";

    if (output.channels != 2)
    {
        std::cout
            << "ERROR: Output must be stereo.\n";

        return false;
    }

    // 100 ms WASAPI buffer
    REFERENCE_TIME bufferDuration =
        1000000;

    hr =
        output.client->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            0,
            bufferDuration,
            0,
            output.format,
            nullptr
        );

    if (FAILED(hr))
    {
        printHR(
            hr,
            "Output Initialize failed"
        );

        return false;
    }

    hr =
        output.client->GetBufferSize(
            &output.bufferFrames
        );

    if (FAILED(hr))
    {
        printHR(
            hr,
            "GetBufferSize failed"
        );

        return false;
    }

    hr =
        output.client->GetService(
            __uuidof(IAudioRenderClient),
            (void**)&output.render
        );

    if (FAILED(hr))
    {
        printHR(
            hr,
            "GetService failed"
        );

        return false;
    }

    std::cout
        << "Buffer frames: "
        << output.bufferFrames
        << "\n";

    return true;
}


// ============================================================
// GENERATE CLICK TRACK
//
// Timeline:
//
// 0 sec  silence
// 1 sec  CLICK
// 2 sec  silence
// 3 sec  CLICK
// 4 sec  silence
// 5 sec  CLICK
// ...
//
// EXACT SAME PCM DATA IS SENT TO BOTH DEVICES.
// ============================================================

std::vector<float> generateClickTrack(
    UINT32 sampleRate,
    UINT32 channels,
    int durationSeconds
)
{
    size_t totalFrames =
        static_cast<size_t>(
            sampleRate
        ) *
        durationSeconds;

    std::vector<float> audio(
        totalFrames * channels,
        0.0f
    );

    const size_t clickLength =
        static_cast<size_t>(
            sampleRate *
            CLICK_DURATION_MS /
            1000.0
        );

    const size_t clickSpacing =
        static_cast<size_t>(
            sampleRate * 2
        );

    const size_t firstClick =
        static_cast<size_t>(
            sampleRate
        );

    for (
        size_t clickStart = firstClick;
        clickStart < totalFrames;
        clickStart += clickSpacing
    )
    {
        for (
            size_t i = 0;
            i < clickLength &&
            clickStart + i < totalFrames;
            ++i
        )
        {
            double time =
                static_cast<double>(i) /
                static_cast<double>(sampleRate);

            // Fade the click out.
            double envelope =
                1.0 -
                (
                    static_cast<double>(i) /
                    static_cast<double>(clickLength)
                );

            float sample =
                static_cast<float>(
                    0.7 *
                    envelope *
                    std::sin(
                        2.0 *
                        PI *
                        CLICK_FREQUENCY *
                        time
                    )
                );

            for (
                UINT32 c = 0;
                c < channels;
                ++c
            )
            {
                audio[
                    (clickStart + i) *
                    channels +
                    c
                ] = sample;
            }
        }
    }

    return audio;
}


// ============================================================
// FILL WASAPI BUFFER
// ============================================================

bool fillOutputBuffer(
    AudioOutput& output,
    const std::vector<float>& audio,
    size_t& position
)
{
    UINT32 padding = 0;

    HRESULT hr =
        output.client->GetCurrentPadding(
            &padding
        );

    if (FAILED(hr))
        return false;

    UINT32 available =
        output.bufferFrames -
        padding;

    if (available == 0)
        return true;

    size_t totalAudioFrames =
        audio.size() /
        output.channels;

    if (position >= totalAudioFrames)
        return true;

    size_t remaining =
        totalAudioFrames -
        position;

    size_t frames =
        std::min(
            static_cast<size_t>(
                available
            ),
            remaining
        );

    BYTE* data = nullptr;

    hr =
        output.render->GetBuffer(
            static_cast<UINT32>(
                frames
            ),
            &data
        );

    if (FAILED(hr))
        return false;

    float* destination =
        reinterpret_cast<float*>(
            data
        );

    std::copy(
        audio.begin() +
            position *
            output.channels,

        audio.begin() +
            (
                position +
                frames
            ) *
            output.channels,

        destination
    );

    hr =
        output.render->ReleaseBuffer(
            static_cast<UINT32>(
                frames
            ),
            0
        );

    if (FAILED(hr))
        return false;

    position += frames;

    return true;
}


// ============================================================
// MAIN
// ============================================================

int main()
{
    // ========================================================
    // COM
    // ========================================================

    HRESULT hr =
        CoInitializeEx(
            nullptr,
            COINIT_MULTITHREADED
        );

    if (FAILED(hr))
    {
        printHR(
            hr,
            "COM initialization failed"
        );

        return 1;
    }

    std::cout
        << "========================================\n"
        << " Bluetooth Output Timing Test\n"
        << "========================================\n";


    // ========================================================
    // ENUMERATE DEVICES
    // ========================================================

    auto devices =
        enumerateDevices();

    if (
        devices.size() <=
        static_cast<size_t>(
            std::max(
                COBRA_INDEX,
                MIVI_INDEX
            )
        )
    )
    {
        std::cout
            << "Not enough audio devices.\n";

        CoUninitialize();

        return 1;
    }


    // ========================================================
    // INITIALIZE COBRA
    // ========================================================

    AudioOutput cobra;

    if (!initializeOutput(
            cobra,
            devices[COBRA_INDEX]
        ))
    {
        CoUninitialize();

        return 1;
    }


    // ========================================================
    // INITIALIZE MIVI
    // ========================================================

    AudioOutput mivi;

    if (!initializeOutput(
            mivi,
            devices[MIVI_INDEX]
        ))
    {
        CoUninitialize();

        return 1;
    }


    // ========================================================
    // CHECK FORMATS
    // ========================================================

    if (
        cobra.sampleRate !=
        mivi.sampleRate
    )
    {
        std::cout
            << "\nERROR:\n"
            << "Cobra and Mivi have different "
               "sample rates.\n";

        std::cout
            << "Cobra: "
            << cobra.sampleRate
            << "\n";

        std::cout
            << "Mivi:  "
            << mivi.sampleRate
            << "\n";

        // Cleanup
        if (cobra.render)
            cobra.render->Release();

        if (cobra.client)
            cobra.client->Release();

        if (mivi.render)
            mivi.render->Release();

        if (mivi.client)
            mivi.client->Release();

        if (cobra.format)
            CoTaskMemFree(cobra.format);

        if (mivi.format)
            CoTaskMemFree(mivi.format);

        for (auto* device : devices)
        {
            if (device)
                device->Release();
        }

        CoUninitialize();

        return 1;
    }


    if (
        cobra.channels !=
        mivi.channels
    )
    {
        std::cout
            << "\nERROR:\n"
            << "Cobra and Mivi have different "
               "channel counts.\n";

        return 1;
    }


    // ========================================================
    // GENERATE CLICK TRACK
    // ========================================================

    std::cout
        << "\nGenerating identical click track...\n";

    std::vector<float> audio =
        generateClickTrack(
            cobra.sampleRate,
            cobra.channels,
            TEST_SECONDS
        );


    std::cout
        << "\nClick positions:\n";

    for (
        int second = 1;
        second < TEST_SECONDS;
        second += 2
    )
    {
        std::cout
            << "  "
            << second
            << " sec\n";
    }


    // ========================================================
    // PLAYBACK POSITIONS
    // ========================================================

    size_t cobraPosition = 0;

    size_t miviPosition = 0;


    // ========================================================
    // START COBRA
    // ========================================================

    hr =
        cobra.client->Start();

    if (FAILED(hr))
    {
        printHR(
            hr,
            "Cobra Start failed"
        );

        return 1;
    }


    // ========================================================
    // START MIVI
    // ========================================================

    hr =
        mivi.client->Start();

    if (FAILED(hr))
    {
        printHR(
            hr,
            "Mivi Start failed"
        );

        return 1;
    }


    std::cout
        << "\n========================================\n"
        << " PLAYBACK STARTED\n"
        << "========================================\n";


    // ========================================================
    // TIMER
    // ========================================================

    auto startTime =
        std::chrono::steady_clock::now();

    auto lastPrint =
        startTime;


    // ========================================================
    // PLAYBACK LOOP
    // ========================================================

    while (true)
    {
        auto now =
            std::chrono::steady_clock::now();


        auto elapsedMs =
            std::chrono::duration_cast<
                std::chrono::milliseconds
            >(
                now - startTime
            ).count();


        if (
            elapsedMs >=
            TEST_SECONDS * 1000
        )
        {
            break;
        }


        // ----------------------------------------------------
        // FEED COBRA
        // ----------------------------------------------------

        if (
            !fillOutputBuffer(
                cobra,
                audio,
                cobraPosition
            )
        )
        {
            std::cout
                << "Cobra render error.\n";

            break;
        }


        // ----------------------------------------------------
        // FEED MIVI
        // ----------------------------------------------------

        if (
            !fillOutputBuffer(
                mivi,
                audio,
                miviPosition
            )
        )
        {
            std::cout
                << "Mivi render error.\n";

            break;
        }


        // ----------------------------------------------------
        // DIAGNOSTICS
        // ----------------------------------------------------

        if (
            std::chrono::duration_cast<
                std::chrono::seconds
            >(
                now - lastPrint
            ).count() >= 1
        )
        {
            std::cout
                << "\nTime: "
                << elapsedMs / 1000
                << " sec\n";

            std::cout
                << "Cobra samples sent: "
                << cobraPosition
                << "\n";

            std::cout
                << "Mivi samples sent:  "
                << miviPosition
                << "\n";

            size_t difference =
                0;

            if (
                cobraPosition >
                miviPosition
            )
            {
                difference =
                    cobraPosition -
                    miviPosition;
            }
            else
            {
                difference =
                    miviPosition -
                    cobraPosition;
            }

            std::cout
                << "Software sample difference: "
                << difference
                << "\n";

            double milliseconds =
                (
                    static_cast<double>(
                        difference
                    ) /
                    static_cast<double>(
                        cobra.sampleRate
                    )
                ) *
                1000.0;

            std::cout
                << "Software time difference: "
                << milliseconds
                << " ms\n";

            lastPrint = now;
        }


        std::this_thread::sleep_for(
            std::chrono::milliseconds(2)
        );
    }


    // ========================================================
    // STOP
    // ========================================================

    std::cout
        << "\nStopping...\n";

    cobra.client->Stop();

    mivi.client->Stop();


    // ========================================================
    // CLEANUP
    // ========================================================

    if (cobra.render)
        cobra.render->Release();

    if (cobra.client)
        cobra.client->Release();

    if (mivi.render)
        mivi.render->Release();

    if (mivi.client)
        mivi.client->Release();

    if (cobra.format)
        CoTaskMemFree(
            cobra.format
        );

    if (mivi.format)
        CoTaskMemFree(
            mivi.format
        );

    for (auto* device : devices)
    {
        if (device)
            device->Release();
    }

    CoUninitialize();


    std::cout
        << "\nTest finished.\n";

    return 0;
}