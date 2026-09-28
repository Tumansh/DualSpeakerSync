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
#include <algorithm>
#include <cmath>

#pragma comment(lib, "ole32.lib")

// ------------------------------------------------------------
// TARGET OUTPUTS
// ------------------------------------------------------------

constexpr int COBRA_INDEX = 0;
constexpr int MIVI_INDEX  = 1;

// Software compensation.
// Keep both at 0 until we measure the actual physical offset.
constexpr int COBRA_DELAY_MS = 0;
constexpr int MIVI_DELAY_MS  = 0;

// ------------------------------------------------------------
// BUFFER SETTINGS
// ------------------------------------------------------------

constexpr int RING_BUFFER_SECONDS = 3;
constexpr int START_BUFFER_MS = 300;
constexpr int TEST_SECONDS = 30;

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
// RING BUFFER
// ============================================================

class AudioRingBuffer
{
private:

    std::vector<float> buffer;

    size_t capacityFrames;
    size_t channels;

    size_t readPos = 0;
    size_t writePos = 0;

    size_t availableFrames = 0;

public:

    AudioRingBuffer(
        size_t capacity,
        size_t ch
    )
        : capacityFrames(capacity),
          channels(ch)
    {
        buffer.resize(
            capacityFrames * channels,
            0.0f
        );
    }

    size_t available() const
    {
        return availableFrames;
    }

    size_t freeSpace() const
    {
        return capacityFrames -
               availableFrames;
    }

    bool push(
        const float* data,
        size_t frames
    )
    {
        if (frames > freeSpace())
            return false;

        for (size_t i = 0; i < frames; ++i)
        {
            for (size_t c = 0;
                 c < channels;
                 ++c)
            {
                buffer[
                    writePos * channels + c
                ] =
                    data[
                        i * channels + c
                    ];
            }

            writePos++;

            if (writePos >= capacityFrames)
                writePos = 0;
        }

        availableFrames += frames;

        return true;
    }

    size_t pop(
        float* output,
        size_t frames
    )
    {
        size_t count =
            std::min(
                frames,
                availableFrames
            );

        for (size_t i = 0;
             i < count;
             ++i)
        {
            for (size_t c = 0;
                 c < channels;
                 ++c)
            {
                output[
                    i * channels + c
                ] =
                    buffer[
                        readPos * channels + c
                    ];
            }

            readPos++;

            if (readPos >= capacityFrames)
                readPos = 0;
        }

        availableFrames -= count;

        return count;
    }
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
// FLOAT FORMAT CHECK
// ============================================================

bool isFloat32(
    WAVEFORMATEX* format
)
{
    if (format->wFormatTag ==
        WAVE_FORMAT_IEEE_FLOAT)
    {
        return true;
    }

    if (format->wFormatTag ==
        WAVE_FORMAT_EXTENSIBLE)
    {
        auto* ext =
            reinterpret_cast<
                WAVEFORMATEXTENSIBLE*
            >(format);

        return IsEqualGUID(
            ext->SubFormat,
            KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
        );
    }

    return false;
}

// ============================================================
// DEVICE NAME
// ============================================================

std::wstring getDeviceName(
    IMMDevice* device
)
{
    std::wstring result;

    IPropertyStore* properties =
        nullptr;

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

    if (SUCCEEDED(hr) &&
        name.vt == VT_LPWSTR)
    {
        result = name.pwszVal;
    }

    PropVariantClear(&name);

    properties->Release();

    return result;
}

// ============================================================
// ENUMERATE OUTPUT DEVICES
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

    for (UINT i = 0;
         i < count;
         ++i)
    {
        IMMDevice* device =
            nullptr;

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
            << "ERROR: Output is not FLOAT32.\n";

        return false;
    }

    if (output.channels != 2)
    {
        std::cout
            << "ERROR: Output is not stereo.\n";

        return false;
    }

    REFERENCE_TIME bufferDuration =
        1000000; // 100 ms

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
            "GetService render failed"
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
// LINEAR RESAMPLER
//
// Input:
//      48000 Hz stereo float
//
// Output:
//      44100 Hz stereo float
//
// This is deliberately simple for this stage.
// We can replace it with a higher-quality resampler later.
// ============================================================

class LinearResampler
{
private:

    double inputRate;
    double outputRate;

    double sourcePosition = 0.0;

    float previousLeft = 0.0f;
    float previousRight = 0.0f;

    bool hasPrevious = false;

public:

    LinearResampler(
        double inputSampleRate,
        double outputSampleRate
    )
        : inputRate(inputSampleRate),
          outputRate(outputSampleRate)
    {
    }

    size_t process(
        const float* input,
        size_t inputFrames,
        std::vector<float>& output
    )
    {
        if (inputFrames == 0)
            return 0;

        const double ratio =
            inputRate / outputRate;

        /*
            Estimate how many output frames
            this input packet can generate.
        */

        size_t estimatedOutput =
            static_cast<size_t>(
                std::ceil(
                    inputFrames / ratio
                )
            ) + 2;

        output.clear();

        output.reserve(
            estimatedOutput * 2
        );

        /*
            For this implementation we process
            the input packet continuously.

            The first sample is retained so that
            packet boundaries don't create gaps.
        */

        if (!hasPrevious)
        {
            previousLeft =
                input[0];

            previousRight =
                input[1];

            hasPrevious = true;
        }

        /*
            We use the input packet as a sequence
            of stereo frames.
        */

        double position =
            sourcePosition;

        while (
            position <
            static_cast<double>(
                inputFrames - 1
            )
        )
        {
            size_t index =
                static_cast<size_t>(
                    position
                );

            double fraction =
                position -
                static_cast<double>(
                    index
                );

            const float* a =
                input +
                index * 2;

            const float* b =
                input +
                (index + 1) * 2;

            float left =
                static_cast<float>(
                    a[0] +
                    (b[0] - a[0]) *
                    fraction
                );

            float right =
                static_cast<float>(
                    a[1] +
                    (b[1] - a[1]) *
                    fraction
                );

            output.push_back(left);
            output.push_back(right);

            position += ratio;
        }

        /*
            Keep the fractional position relative
            to the next packet.
        */

        sourcePosition =
            position -
            static_cast<double>(
                inputFrames - 1
            );

        return output.size() / 2;
    }
};

// ============================================================
// RENDER FROM RING BUFFER
// ============================================================

bool renderFromRingBuffer(
    AudioOutput& output,
    AudioRingBuffer& ring
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

    size_t frames =
        std::min(
            static_cast<size_t>(
                available
            ),
            ring.available()
        );

    if (frames == 0)
    {
        /*
            Underrun.

            Fill the entire available WASAPI
            region with silence.
        */

        BYTE* data = nullptr;

        hr =
            output.render->GetBuffer(
                available,
                &data
            );

        if (FAILED(hr))
            return false;

        ZeroMemory(
            data,
            static_cast<size_t>(
                available
            ) *
            output.channels *
            sizeof(float)
        );

        output.render->ReleaseBuffer(
            available,
            AUDCLNT_BUFFERFLAGS_SILENT
        );

        return true;
    }

    BYTE* data = nullptr;

    hr =
        output.render->GetBuffer(
            static_cast<UINT32>(frames),
            &data
        );

    if (FAILED(hr))
        return false;

    float* destination =
        reinterpret_cast<float*>(
            data
        );

    ring.pop(
        destination,
        frames
    );

    hr =
        output.render->ReleaseBuffer(
            static_cast<UINT32>(frames),
            0
        );

    return SUCCEEDED(hr);
}

// ============================================================
// PUSH SILENCE
// ============================================================

void pushSilence(
    AudioRingBuffer& ring,
    size_t frames,
    UINT32 channels
)
{
    if (frames == 0)
        return;

    std::vector<float> silence(
        frames * channels,
        0.0f
    );

    ring.push(
        silence.data(),
        frames
    );
}

// ============================================================
// MAIN
// ============================================================

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
        << " Multi Speaker Sync Test\n"
        << " 48kHz -> 44.1kHz Resampling\n"
        << "========================================\n";

    // --------------------------------------------------------
    // Enumerate devices
    // --------------------------------------------------------

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
            << "Not enough audio devices.\n";

        CoUninitialize();

        return 1;
    }

    // --------------------------------------------------------
    // Initialize outputs
    // --------------------------------------------------------

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
        mivi.sampleRate)
    {
        std::cout
            << "\nERROR:\n"
            << "Cobra and Mivi have different "
               "sample rates.\n";

        CoUninitialize();

        return 1;
    }

    if (cobra.channels != 2 ||
        mivi.channels != 2)
    {
        std::cout
            << "\nERROR:\n"
            << "Stereo output required.\n";

        CoUninitialize();

        return 1;
    }

    const UINT32 outputRate =
        cobra.sampleRate;

    const UINT32 outputChannels =
        cobra.channels;

    // --------------------------------------------------------
    // Capture default Windows output
    // --------------------------------------------------------

    IMMDeviceEnumerator* enumerator =
        nullptr;

    hr =
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
            "Failed to create capture enumerator"
        );

        CoUninitialize();

        return 1;
    }

    IMMDevice* captureDevice =
        nullptr;

    hr =
        enumerator->GetDefaultAudioEndpoint(
            eRender,
            eConsole,
            &captureDevice
        );

    if (FAILED(hr))
    {
        printHR(
            hr,
            "Failed to get default render device"
        );

        enumerator->Release();

        CoUninitialize();

        return 1;
    }

    std::wcout
        << L"\nCapture source: "
        << getDeviceName(captureDevice)
        << L"\n";

    // --------------------------------------------------------
    // Capture client
    // --------------------------------------------------------

    IAudioClient* captureClient =
        nullptr;

    hr =
        captureDevice->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            (void**)&captureClient
        );

    if (FAILED(hr))
    {
        printHR(
            hr,
            "Failed to activate capture client"
        );

        return 1;
    }

    WAVEFORMATEX* captureFormat =
        nullptr;

    hr =
        captureClient->GetMixFormat(
            &captureFormat
        );

    if (FAILED(hr))
    {
        printHR(
            hr,
            "Capture GetMixFormat failed"
        );

        return 1;
    }

    const UINT32 captureRate =
        captureFormat->nSamplesPerSec;

    const UINT32 captureChannels =
        captureFormat->nChannels;

    std::cout
        << "\nCapture source format:\n"
        << "Sample rate: "
        << captureRate
        << "\nChannels: "
        << captureChannels
        << "\nBits: "
        << captureFormat->wBitsPerSample
        << "\nFormat tag: "
        << captureFormat->wFormatTag
        << "\n";

    if (!isFloat32(captureFormat))
    {
        std::cout
            << "ERROR: Capture source is not "
               "FLOAT32.\n";

        return 1;
    }

    if (captureChannels != 2)
    {
        std::cout
            << "ERROR: Capture source is not stereo.\n";

        return 1;
    }

    // --------------------------------------------------------
    // Initialize loopback capture
    // --------------------------------------------------------

    REFERENCE_TIME captureDuration =
        1000000; // 100 ms

    hr =
        captureClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK,
            captureDuration,
            0,
            captureFormat,
            nullptr
        );

    if (FAILED(hr))
    {
        printHR(
            hr,
            "Loopback Initialize failed"
        );

        return 1;
    }

    IAudioCaptureClient* capture =
        nullptr;

    hr =
        captureClient->GetService(
            __uuidof(IAudioCaptureClient),
            (void**)&capture
        );

    if (FAILED(hr))
    {
        printHR(
            hr,
            "Get IAudioCaptureClient failed"
        );

        return 1;
    }

    // --------------------------------------------------------
    // Ring buffers
    // --------------------------------------------------------

    const size_t ringCapacity =
        static_cast<size_t>(
            outputRate
        ) *
        RING_BUFFER_SECONDS;

    AudioRingBuffer cobraRing(
        ringCapacity,
        outputChannels
    );

    AudioRingBuffer miviRing(
        ringCapacity,
        outputChannels
    );

    // --------------------------------------------------------
    // Delay
    // --------------------------------------------------------

    const size_t cobraDelayFrames =
        static_cast<size_t>(
            outputRate
        ) *
        COBRA_DELAY_MS /
        1000;

    const size_t miviDelayFrames =
        static_cast<size_t>(
            outputRate
        ) *
        MIVI_DELAY_MS /
        1000;

    pushSilence(
        cobraRing,
        cobraDelayFrames,
        outputChannels
    );

    pushSilence(
        miviRing,
        miviDelayFrames,
        outputChannels
    );

    std::cout
        << "\n========================================\n"
        << " Configuration\n"
        << "========================================\n";

    std::cout
        << "Capture rate: "
        << captureRate
        << "\n";

    std::cout
        << "Output rate: "
        << outputRate
        << "\n";

    std::cout
        << "Resampling: "
        << captureRate
        << " -> "
        << outputRate
        << "\n";

    std::cout
        << "Cobra delay: "
        << COBRA_DELAY_MS
        << " ms\n";

    std::cout
        << "Mivi delay: "
        << MIVI_DELAY_MS
        << " ms\n";

    // --------------------------------------------------------
    // Resampler
    // --------------------------------------------------------

    LinearResampler resampler(
        static_cast<double>(captureRate),
        static_cast<double>(outputRate)
    );

    std::vector<float> resampledAudio;

    // --------------------------------------------------------
    // Start capture
    // --------------------------------------------------------

    hr =
        captureClient->Start();

    if (FAILED(hr))
    {
        printHR(
            hr,
            "Capture Start failed"
        );

        return 1;
    }

    std::cout
        << "\nCollecting initial audio buffer...\n";

    const size_t initialFrames =
        static_cast<size_t>(
            outputRate
        ) *
        START_BUFFER_MS /
        1000;

    /*
        Fill both output queues before playback.
    */

    while (
        cobraRing.available() <
            initialFrames ||
        miviRing.available() <
            initialFrames
    )
    {
        UINT32 packetSize = 0;

        hr =
            capture->GetNextPacketSize(
                &packetSize
            );

        if (FAILED(hr))
            break;

        if (packetSize == 0)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(2)
            );

            continue;
        }

        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;

        hr =
            capture->GetBuffer(
                &data,
                &frames,
                &flags,
                nullptr,
                nullptr
            );

        if (FAILED(hr))
            break;

        if (flags &
            AUDCLNT_BUFFERFLAGS_SILENT)
        {
            /*
                Silence is already at the capture
                sample rate. We need equivalent
                output silence after resampling.
            */

            size_t outputFrames =
                static_cast<size_t>(
                    std::ceil(
                        static_cast<double>(frames) *
                        outputRate /
                        captureRate
                    )
                );

            pushSilence(
                cobraRing,
                outputFrames,
                outputChannels
            );

            pushSilence(
                miviRing,
                outputFrames,
                outputChannels
            );
        }
        else
        {
            float* input =
                reinterpret_cast<float*>(data);

            resampler.process(
                input,
                frames,
                resampledAudio
            );

            size_t outputFrames =
                resampledAudio.size() / 2;

            if (outputFrames > 0)
            {
                cobraRing.push(
                    resampledAudio.data(),
                    outputFrames
                );

                miviRing.push(
                    resampledAudio.data(),
                    outputFrames
                );
            }
        }

        capture->ReleaseBuffer(frames);
    }

    std::cout
        << "Initial buffer ready.\n";

    std::cout
        << "Cobra queued: "
        << cobraRing.available()
        << " frames\n";

    std::cout
        << "Mivi queued:  "
        << miviRing.available()
        << " frames\n";

    // --------------------------------------------------------
    // Start both outputs
    // --------------------------------------------------------

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

    auto startTime =
        std::chrono::steady_clock::now();

    auto lastPrint =
        startTime;

    while (true)
    {
        auto now =
            std::chrono::steady_clock::now();

        auto elapsed =
            std::chrono::duration_cast<
                std::chrono::seconds
            >(
                now - startTime
            ).count();

        if (elapsed >= TEST_SECONDS)
            break;

        // ----------------------------------------------------
        // CAPTURE
        // ----------------------------------------------------

        while (true)
        {
            UINT32 packetSize = 0;

            hr =
                capture->GetNextPacketSize(
                    &packetSize
                );

            if (FAILED(hr))
                break;

            if (packetSize == 0)
                break;

            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;

            hr =
                capture->GetBuffer(
                    &data,
                    &frames,
                    &flags,
                    nullptr,
                    nullptr
                );

            if (FAILED(hr))
                break;

            if (flags &
                AUDCLNT_BUFFERFLAGS_SILENT)
            {
                size_t outputFrames =
                    static_cast<size_t>(
                        std::ceil(
                            static_cast<double>(
                                frames
                            ) *
                            outputRate /
                            captureRate
                        )
                    );

                pushSilence(
                    cobraRing,
                    outputFrames,
                    outputChannels
                );

                pushSilence(
                    miviRing,
                    outputFrames,
                    outputChannels
                );
            }
            else
            {
                float* input =
                    reinterpret_cast<float*>(data);

                resampler.process(
                    input,
                    frames,
                    resampledAudio
                );

                size_t outputFrames =
                    resampledAudio.size() / 2;

                if (outputFrames > 0)
                {
                    cobraRing.push(
                        resampledAudio.data(),
                        outputFrames
                    );

                    miviRing.push(
                        resampledAudio.data(),
                        outputFrames
                    );
                }
            }

            capture->ReleaseBuffer(
                frames
            );
        }

        // ----------------------------------------------------
        // RENDER
        // ----------------------------------------------------

        renderFromRingBuffer(
            cobra,
            cobraRing
        );

        renderFromRingBuffer(
            mivi,
            miviRing
        );

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
                << "Time: "
                << elapsed
                << " sec"
                << " | Cobra buffer: "
                << cobraRing.available()
                << " frames"
                << " | Mivi buffer: "
                << miviRing.available()
                << " frames\n";

            lastPrint = now;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(2)
        );
    }

    // --------------------------------------------------------
    // STOP
    // --------------------------------------------------------

    std::cout
        << "\nStopping...\n";

    cobra.client->Stop();
    mivi.client->Stop();
    captureClient->Stop();

    // --------------------------------------------------------
    // CLEANUP
    // --------------------------------------------------------

    if (capture)
        capture->Release();

    if (captureClient)
        captureClient->Release();

    if (captureDevice)
        captureDevice->Release();

    if (enumerator)
        enumerator->Release();

    if (cobra.render)
        cobra.render->Release();

    if (cobra.client)
        cobra.client->Release();

    if (mivi.render)
        mivi.render->Release();

    if (mivi.client)
        mivi.client->Release();

    if (captureFormat)
        CoTaskMemFree(captureFormat);

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

    std::cout
        << "\nTest finished.\n";

    return 0;
}