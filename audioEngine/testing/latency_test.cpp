#define NOMINMAX

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>

#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "ole32.lib")

constexpr int COBRA_INDEX = 0;
constexpr int MIVI_INDEX  = 1;

constexpr int TEST_SECONDS = 15;
constexpr int CLICK_INTERVAL_MS = 1000;
constexpr int CLICK_DURATION_MS = 8;

constexpr double PI = 3.14159265358979323846;

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

void printHR(HRESULT hr, const char* message)
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

bool isFloat32(WAVEFORMATEX* format)
{
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        return true;

    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        auto* ext =
            reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format);

        return IsEqualGUID(
            ext->SubFormat,
            KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
        );
    }

    return false;
}

std::wstring getDeviceName(IMMDevice* device)
{
    std::wstring result;

    IPropertyStore* properties = nullptr;

    if (SUCCEEDED(
        device->OpenPropertyStore(
            STGM_READ,
            &properties
        )))
    {
        PROPVARIANT name;
        PropVariantInit(&name);

        if (SUCCEEDED(
            properties->GetValue(
                PKEY_Device_FriendlyName,
                &name
            )))
        {
            if (name.vt == VT_LPWSTR)
                result = name.pwszVal;
        }

        PropVariantClear(&name);
        properties->Release();
    }

    return result;
}

std::vector<IMMDevice*> enumerateDevices()
{
    std::vector<IMMDevice*> devices;

    IMMDeviceEnumerator* enumerator = nullptr;

    HRESULT hr = CoCreateInstance(
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
            "Failed to create MMDeviceEnumerator"
        );

        return devices;
    }

    IMMDeviceCollection* collection = nullptr;

    hr = enumerator->EnumAudioEndpoints(
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

    for (UINT i = 0; i < count; ++i)
    {
        IMMDevice* device = nullptr;

        collection->Item(i, &device);

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

bool initializeOutput(
    AudioOutput& output,
    IMMDevice* device
)
{
    output.device = device;
    output.name = getDeviceName(device);

    HRESULT hr = device->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        (void**)&output.client
    );

    if (FAILED(hr))
    {
        printHR(
            hr,
            "IAudioClient activation failed"
        );

        return false;
    }

    hr = output.client->GetMixFormat(
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
        << "\nChannels: "
        << output.channels
        << "\nBits: "
        << output.format->wBitsPerSample
        << "\nFormat tag: "
        << output.format->wFormatTag
        << "\n";

    if (!isFloat32(output.format))
    {
        std::cout
            << "ERROR: Device is not FLOAT32.\n";

        return false;
    }

    if (output.channels != 2)
    {
        std::cout
            << "ERROR: Device is not stereo.\n";

        return false;
    }

    REFERENCE_TIME bufferDuration =
        1000000; // 100 ms

    hr = output.client->Initialize(
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
            "IAudioClient Initialize failed"
        );

        return false;
    }

    hr = output.client->GetBufferSize(
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

    hr = output.client->GetService(
        __uuidof(IAudioRenderClient),
        (void**)&output.render
    );

    if (FAILED(hr))
    {
        printHR(
            hr,
            "GetService IAudioRenderClient failed"
        );

        return false;
    }

    std::cout
        << "Buffer frames: "
        << output.bufferFrames
        << "\n";

    return true;
}

/*
    Create the COMPLETE test signal before
    playback begins.

    Both speakers will consume exactly the
    same samples.

    Signal:

       0s       1s       2s       3s
        |        |        |        |
        █        █        █        █

    Each █ is an 8 ms sharp click.
*/
std::vector<float> createTestSignal(
    UINT32 sampleRate,
    UINT32 channels
)
{
    const size_t totalFrames =
        static_cast<size_t>(sampleRate) *
        TEST_SECONDS;

    std::vector<float> signal(
        totalFrames * channels,
        0.0f
    );

    const size_t intervalFrames =
        (
            static_cast<size_t>(sampleRate) *
            CLICK_INTERVAL_MS
        ) / 1000;

    const size_t clickFrames =
        (
            static_cast<size_t>(sampleRate) *
            CLICK_DURATION_MS
        ) / 1000;

    for (
        size_t start = 0;
        start < totalFrames;
        start += intervalFrames
    )
    {
        for (
            size_t i = 0;
            i < clickFrames &&
            start + i < totalFrames;
            ++i
        )
        {
            /*
                Very short 2 kHz pulse.

                Fade-in/out prevents a huge discontinuity
                while remaining extremely sharp.
            */

            double t =
                static_cast<double>(i) /
                sampleRate;

            double duration =
                static_cast<double>(clickFrames) /
                sampleRate;

            double envelope =
                1.0 -
                (t / duration);

            float sample =
                static_cast<float>(
                    0.8 *
                    envelope *
                    std::sin(
                        2.0 *
                        PI *
                        2000.0 *
                        t
                    )
                );

            for (UINT32 c = 0;
                 c < channels;
                 ++c)
            {
                signal[
                    (start + i) *
                    channels +
                    c
                ] = sample;
            }
        }
    }

    return signal;
}

bool renderFrames(
    AudioOutput& output,
    const std::vector<float>& signal,
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
        output.bufferFrames - padding;

    if (available == 0)
        return true;

    size_t remaining =
        (signal.size() / output.channels) -
        position;

    if (remaining == 0)
        return true;

    UINT32 frames =
        static_cast<UINT32>(
            std::min(
                static_cast<size_t>(available),
                remaining
            )
        );

    BYTE* data = nullptr;

    hr =
        output.render->GetBuffer(
            frames,
            &data
        );

    if (FAILED(hr))
        return false;

    float* destination =
        reinterpret_cast<float*>(data);

    const float* source =
        signal.data() +
        position * output.channels;

    std::copy(
        source,
        source +
            frames * output.channels,
        destination
    );

    hr =
        output.render->ReleaseBuffer(
            frames,
            0
        );

    if (FAILED(hr))
        return false;

    position += frames;

    return true;
}

int main()
{
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
        << " PRE-GENERATED BLUETOOTH LATENCY TEST\n"
        << "========================================\n";

    auto devices =
        enumerateDevices();

    if (devices.size() <=
        static_cast<size_t>(
            std::max(
                COBRA_INDEX,
                MIVI_INDEX
            )))
    {
        std::cout
            << "Not enough devices.\n";

        CoUninitialize();

        return 1;
    }

    AudioOutput cobra;
    AudioOutput mivi;

    if (!initializeOutput(
            cobra,
            devices[COBRA_INDEX]
        ))
    {
        CoUninitialize();
        return 1;
    }

    if (!initializeOutput(
            mivi,
            devices[MIVI_INDEX]
        ))
    {
        CoUninitialize();
        return 1;
    }

    if (cobra.sampleRate !=
            mivi.sampleRate ||
        cobra.channels !=
            mivi.channels)
    {
        std::cout
            << "\nERROR:\n"
            << "Cobra and Mivi have different "
               "audio formats.\n";

        CoUninitialize();

        return 1;
    }

    UINT32 sampleRate =
        cobra.sampleRate;

    UINT32 channels =
        cobra.channels;

    /*
        Generate the COMPLETE signal first.
    */

    std::cout
        << "\nGenerating "
        << TEST_SECONDS
        << " second test signal...\n";

    std::vector<float> signal =
        createTestSignal(
            sampleRate,
            channels
        );

    std::cout
        << "Generated "
        << signal.size() / channels
        << " frames.\n";

    std::cout
        << "\n========================================\n"
        << " IMPORTANT\n"
        << "========================================\n";

    std::cout
        << "Place your phone approximately midway "
           "between Cobra and Mivi.\n\n";

    std::cout
        << "Record the entire test.\n\n";

    std::cout
        << "You should hear one sharp click every "
           "second.\n";

    std::cout
        << "\nStarting in 3 seconds...\n";

    std::this_thread::sleep_for(
        std::chrono::seconds(3)
    );

    /*
        Start BOTH WASAPI streams as close together
        as possible.
    */

    hr = cobra.client->Start();

    if (FAILED(hr))
    {
        printHR(
            hr,
            "Cobra Start failed"
        );

        return 1;
    }

    hr = mivi.client->Start();

    if (FAILED(hr))
    {
        printHR(
            hr,
            "Mivi Start failed"
        );

        return 1;
    }

    std::cout
        << "\nTEST STARTED\n\n";

    size_t cobraPosition = 0;
    size_t miviPosition = 0;

    auto start =
        std::chrono::steady_clock::now();

    auto lastPrint = start;

    while (
        cobraPosition <
            signal.size() / channels ||
        miviPosition <
            signal.size() / channels
    )
    {
        renderFrames(
            cobra,
            signal,
            cobraPosition
        );

        renderFrames(
            mivi,
            signal,
            miviPosition
        );

        auto now =
            std::chrono::steady_clock::now();

        if (
            std::chrono::duration_cast<
                std::chrono::seconds
            >(now - lastPrint).count() >= 1
        )
        {
            std::cout
                << "Cobra: "
                << cobraPosition
                << " / "
                << signal.size() / channels
                << " frames";

            std::cout
                << " | Mivi: "
                << miviPosition
                << " / "
                << signal.size() / channels
                << "\n";

            lastPrint = now;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(2)
        );
    }

    /*
        Let the final audio leave the devices.
    */

    std::this_thread::sleep_for(
        std::chrono::milliseconds(1000)
    );

    cobra.client->Stop();
    mivi.client->Stop();

    std::cout
        << "\n========================================\n"
        << " TEST FINISHED\n"
        << "========================================\n";

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

    return 0;
}