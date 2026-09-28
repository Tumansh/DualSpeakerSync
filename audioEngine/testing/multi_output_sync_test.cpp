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

// ============================================================
// SETTINGS
// ============================================================

constexpr int COBRA_INDEX = 0;
constexpr int MIVI_INDEX  = 1;

constexpr int COBRA_DELAY_MS = 0;
constexpr int MIVI_DELAY_MS  = 0;

constexpr int RING_BUFFER_SECONDS = 3;

constexpr int START_BUFFER_MS = 300;

constexpr int TEST_SECONDS = 30;

// Target ring-buffer level.
// At 44.1 kHz, 300 ms ~= 13,230 frames.
constexpr double TARGET_BUFFER_SECONDS = 0.30;

// Maximum adaptive correction.
// Very small to avoid audible pitch changes.
constexpr double MAX_CORRECTION = 0.0005;


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
// CHECK FLOAT32
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
// GET DEVICE NAME
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
// ENUMERATE AUDIO OUTPUTS
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
// INITIALIZE OUTPUT DEVICE
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

        for (size_t i = 0;
             i < frames;
             ++i)
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
// ADAPTIVE RESAMPLER
//
// Example:
//
//     48000 Hz
//         ↓
//     44100 Hz
//
// The correction slightly changes the effective
// resampling ratio to prevent the ring buffer
// from continuously draining or filling.
// ============================================================

class AdaptiveResampler
{
private:

    double inputRate;

    double outputRate;

    double sourcePosition = 0.0;

public:

    AdaptiveResampler(
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
        std::vector<float>& output,
        double correction
    )
    {
        if (inputFrames < 2)
            return 0;

        double normalRatio =
            inputRate /
            outputRate;

        /*
            Positive correction:

                produce slightly fewer output frames

            Negative correction:

                produce slightly more output frames
        */

        double effectiveRatio =
            normalRatio *
            (1.0 + correction);

        output.clear();

        size_t estimatedFrames =
            static_cast<size_t>(
                std::ceil(
                    inputFrames /
                    effectiveRatio
                )
            ) + 4;

        output.reserve(
            estimatedFrames * 2
        );

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

            const float* sampleA =
                input +
                index * 2;

            const float* sampleB =
                input +
                (index + 1) * 2;

            float left =
                static_cast<float>(
                    sampleA[0] +
                    (
                        sampleB[0] -
                        sampleA[0]
                    ) *
                    fraction
                );

            float right =
                static_cast<float>(
                    sampleA[1] +
                    (
                        sampleB[1] -
                        sampleA[1]
                    ) *
                    fraction
                );

            output.push_back(left);

            output.push_back(right);

            position += effectiveRatio;
        }

        sourcePosition =
            position -
            static_cast<double>(
                inputFrames - 1
            );

        return output.size() / 2;
    }
};


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
// RENDER AUDIO FROM RING BUFFER
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

    // --------------------------------------------------------
    // UNDERRUN
    // --------------------------------------------------------

    if (frames == 0)
    {
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

    // --------------------------------------------------------
    // NORMAL RENDER
    // --------------------------------------------------------

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

    ring.pop(
        destination,
        frames
    );

    hr =
        output.render->ReleaseBuffer(
            static_cast<UINT32>(
                frames
            ),
            0
        );

    return SUCCEEDED(hr);
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
        << " Adaptive 48k -> 44.1k Resampling\n"
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
    // VERIFY OUTPUT FORMATS
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

        CoUninitialize();

        return 1;
    }

    if (
        cobra.channels != 2 ||
        mivi.channels != 2
    )
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


    // ========================================================
    // GET DEFAULT WINDOWS AUDIO DEVICE
    // ========================================================

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


    // ========================================================
    // ACTIVATE CAPTURE CLIENT
    // ========================================================

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


    // ========================================================
    // CAPTURE FORMAT
    // ========================================================

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
        << "\nCapture source format:\n";

    std::cout
        << "Sample rate: "
        << captureRate
        << "\n";

    std::cout
        << "Channels: "
        << captureChannels
        << "\n";

    std::cout
        << "Bits: "
        << captureFormat->wBitsPerSample
        << "\n";

    std::cout
        << "Format tag: "
        << captureFormat->wFormatTag
        << "\n";


    if (!isFloat32(captureFormat))
    {
        std::cout
            << "ERROR: Capture source is not FLOAT32.\n";

        return 1;
    }

    if (captureChannels != 2)
    {
        std::cout
            << "ERROR: Capture source is not stereo.\n";

        return 1;
    }


    // ========================================================
    // INITIALIZE LOOPBACK CAPTURE
    // ========================================================

    REFERENCE_TIME captureDuration =
        1000000;

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


    // ========================================================
    // CAPTURE SERVICE
    // ========================================================

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


    // ========================================================
    // RING BUFFERS
    // ========================================================

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


    // ========================================================
    // TARGET BUFFER
    // ========================================================

    const size_t targetBufferFrames =
        static_cast<size_t>(
            outputRate *
            TARGET_BUFFER_SECONDS
        );


    // ========================================================
    // OPTIONAL FIXED DELAY
    // ========================================================

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


    // ========================================================
    // INFORMATION
    // ========================================================

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
        << "Target buffer: "
        << targetBufferFrames
        << " frames\n";


    std::cout
        << "Cobra delay: "
        << COBRA_DELAY_MS
        << " ms\n";


    std::cout
        << "Mivi delay: "
        << MIVI_DELAY_MS
        << " ms\n";


    // ========================================================
    // RESAMPLER
    // ========================================================

    AdaptiveResampler resampler(
        static_cast<double>(
            captureRate
        ),
        static_cast<double>(
            outputRate
        )
    );


    std::vector<float> resampledAudio;


    // ========================================================
    // START CAPTURE
    // ========================================================

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


    // ========================================================
    // INITIAL BUFFER
    // ========================================================

    std::cout
        << "\nCollecting initial audio buffer...\n";


    const size_t initialFrames =
        static_cast<size_t>(
            outputRate
        ) *
        START_BUFFER_MS /
        1000;


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


        if (
            flags &
            AUDCLNT_BUFFERFLAGS_SILENT
        )
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
                reinterpret_cast<float*>(
                    data
                );


            resampler.process(
                input,
                frames,
                resampledAudio,
                0.0
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


    // ========================================================
    // START BOTH OUTPUTS
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


    // ========================================================
    // MAIN LOOP
    // ========================================================

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


        // ====================================================
        // CALCULATE ADAPTIVE CORRECTION
        // ====================================================

        double cobraError =
            static_cast<double>(
                cobraRing.available()
            ) -
            static_cast<double>(
                targetBufferFrames
            );


        double miviError =
            static_cast<double>(
                miviRing.available()
            ) -
            static_cast<double>(
                targetBufferFrames
            );


        double averageError =
            (
                cobraError +
                miviError
            ) / 2.0;


        /*
            Convert buffer error into correction.

            Buffer LOW:
                correction becomes negative
                -> produce slightly MORE output

            Buffer HIGH:
                correction becomes positive
                -> produce slightly LESS output
        */

        double correction =
            -(
                averageError /
                static_cast<double>(
                    targetBufferFrames
                )
            );


if (correction > MAX_CORRECTION)
{
    correction = MAX_CORRECTION;
}
else if (correction < -MAX_CORRECTION)
{
    correction = -MAX_CORRECTION;
}


        // ====================================================
        // CAPTURE
        // ====================================================

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


            if (
                flags &
                AUDCLNT_BUFFERFLAGS_SILENT
            )
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
                    reinterpret_cast<float*>(
                        data
                    );


                resampler.process(
                    input,
                    frames,
                    resampledAudio,
                    correction
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


        // ====================================================
        // RENDER COBRA
        // ====================================================

        renderFromRingBuffer(
            cobra,
            cobraRing
        );


        // ====================================================
        // RENDER MIVI
        // ====================================================

        renderFromRingBuffer(
            mivi,
            miviRing
        );


        // ====================================================
        // DIAGNOSTICS
        // ====================================================

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
                << " frames"
                << " | Correction: "
                << correction
                << "\n";


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

    captureClient->Stop();


    // ========================================================
    // CLEANUP
    // ========================================================

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
        CoTaskMemFree(
            captureFormat
        );

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