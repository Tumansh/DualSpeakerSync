#define NOMINMAX

#include "AudioEngine.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>

#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <atomic>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

// ============================================================
// CONFIGURATION
// ============================================================

static const int DEFAULT_COBRA_INDEX = 0;
static const int DEFAULT_MIVI_INDEX = 1;

static const double COBRA_DELAY_SECONDS = 0.0;
static const double MIVI_DELAY_SECONDS = 0.0;

static const double RING_BUFFER_SECONDS = 3.0;

static const int START_BUFFER_MS = 300;

static const double TARGET_BUFFER_SECONDS = 0.30;

static const double MAX_RATE_CORRECTION = 0.0005;

// ============================================================
// FLOAT32 CHECK
// ============================================================

static bool isFloat32(const WAVEFORMATEX* format)
{
    if (format == nullptr)
        return false;

    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        return format->wBitsPerSample == 32;
    }

    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const WAVEFORMATEXTENSIBLE* ext =
            reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);

        return IsEqualGUID(
            ext->SubFormat,
            KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
        )
        && format->wBitsPerSample == 32;
    }

    return false;
}

// ============================================================
// AUDIO OUTPUT
// ============================================================

struct AudioOutput
{
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* renderClient = nullptr;

    WAVEFORMATEX* format = nullptr;

    UINT32 bufferFrames = 0;

    int sampleRate = 0;
    int channels = 0;

    std::string name;
};

// ============================================================
// GET DEVICE NAME
// ============================================================

static std::string getDeviceName(IMMDevice* device)
{
    if (device == nullptr)
        return "Unknown Device";

    IPropertyStore* propertyStore = nullptr;

    HRESULT hr = device->OpenPropertyStore(
        STGM_READ,
        &propertyStore
    );

    if (FAILED(hr))
        return "Unknown Device";

    PROPVARIANT property;
    PropVariantInit(&property);

    hr = propertyStore->GetValue(
        PKEY_Device_FriendlyName,
        &property
    );

    std::string name = "Unknown Device";

    if (SUCCEEDED(hr) &&
        property.vt == VT_LPWSTR &&
        property.pwszVal != nullptr)
    {
        int sizeNeeded =
            WideCharToMultiByte(
                CP_UTF8,
                0,
                property.pwszVal,
                -1,
                nullptr,
                0,
                nullptr,
                nullptr
            );

        if (sizeNeeded > 0)
        {
            std::vector<char> buffer(sizeNeeded);

            WideCharToMultiByte(
                CP_UTF8,
                0,
                property.pwszVal,
                -1,
                buffer.data(),
                sizeNeeded,
                nullptr,
                nullptr
            );

            name = buffer.data();
        }
    }

    PropVariantClear(&property);

    propertyStore->Release();

    return name;
}

// ============================================================
// ENUMERATE AUDIO DEVICES
// ============================================================

static std::vector<IMMDevice*> enumerateDevices()
{
    std::vector<IMMDevice*> devices;

    IMMDeviceEnumerator* enumerator = nullptr;

    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        IID_PPV_ARGS(&enumerator)
    );

    if (FAILED(hr))
    {
        std::cerr
            << "Failed to create MMDeviceEnumerator\n";
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
        enumerator->Release();
        return devices;
    }

    UINT count = 0;
    collection->GetCount(&count);

    // Only expose the two speakers used by this project.
    // All other Windows audio output devices are ignored.
    IMMDevice* cobraDevice = nullptr;
    IMMDevice* miviDevice = nullptr;

    for (UINT i = 0; i < count; ++i)
    {
        IMMDevice* device = nullptr;

        if (FAILED(collection->Item(i, &device)))
            continue;

        std::string name = getDeviceName(device);

        if (name.find("COBRA") != std::string::npos ||
            name.find("Cobra") != std::string::npos)
        {
            if (cobraDevice == nullptr)
            {
                cobraDevice = device;
                continue;
            }
        }

        if (name.find("Mivi") != std::string::npos ||
            name.find("MIVI") != std::string::npos)
        {
            if (miviDevice == nullptr)
            {
                miviDevice = device;
                continue;
            }
        }

        device->Release();
    }

    // Stable order for the React UI and engine:
    // [0] Cobra
    // [1] Mivi
    if (cobraDevice != nullptr)
        devices.push_back(cobraDevice);

    if (miviDevice != nullptr)
        devices.push_back(miviDevice);

    collection->Release();
    enumerator->Release();

    return devices;
}

// ============================================================
// INITIALIZE OUTPUT
// ============================================================

static bool initializeOutput(
    IMMDevice* device,
    AudioOutput& output
)
{
    if (device == nullptr)
        return false;

    output.device = device;

    output.name = getDeviceName(device);

    HRESULT hr = device->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(&output.client)
    );

    if (FAILED(hr))
    {
        std::cerr
            << "Failed to activate IAudioClient for "
            << output.name
            << "\n";

        return false;
    }

    hr = output.client->GetMixFormat(
        &output.format
    );

    if (FAILED(hr))
    {
        std::cerr
            << "GetMixFormat failed for "
            << output.name
            << "\n";

        return false;
    }

    std::cout
        << "\nOutput: "
        << output.name
        << "\n";

    std::cout
        << "  Sample Rate: "
        << output.format->nSamplesPerSec
        << "\n";

    std::cout
        << "  Channels: "
        << output.format->nChannels
        << "\n";

    std::cout
        << "  Bits: "
        << output.format->wBitsPerSample
        << "\n";

    if (!isFloat32(output.format))
    {
        std::cerr
            << "WARNING: Output is not FLOAT32\n";
    }

    output.sampleRate =
        static_cast<int>(
            output.format->nSamplesPerSec
        );

    output.channels =
        static_cast<int>(
            output.format->nChannels
        );

    // 100 ms WASAPI buffer.
    REFERENCE_TIME bufferDuration = 1000000;

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
        std::cerr
            << "IAudioClient::Initialize failed for "
            << output.name
            << " HRESULT=0x"
            << std::hex
            << hr
            << std::dec
            << "\n";

        return false;
    }

    hr = output.client->GetBufferSize(
        &output.bufferFrames
    );

    if (FAILED(hr))
    {
        std::cerr
            << "GetBufferSize failed\n";

        return false;
    }

    hr = output.client->GetService(
        IID_PPV_ARGS(&output.renderClient)
    );

    if (FAILED(hr))
    {
        std::cerr
            << "GetService(IAudioRenderClient) failed\n";

        return false;
    }

    std::cout
        << "  Buffer Frames: "
        << output.bufferFrames
        << "\n";

    return true;
}

// ============================================================
// RING BUFFER
// ============================================================

class RingBuffer
{
private:

    std::vector<float> data;

    size_t capacityFrames;
    int channels;

    size_t readPos = 0;
    size_t writePos = 0;

    size_t availableFrames = 0;

public:

    RingBuffer(
        size_t frames,
        int channels_
    )
        : capacityFrames(frames),
          channels(channels_)
    {
        data.resize(
            capacityFrames * channels
        );
    }

    size_t available() const
    {
        return availableFrames;
    }

    size_t freeSpace() const
    {
        return capacityFrames - availableFrames;
    }

    size_t capacity() const
    {
        return capacityFrames;
    }

    size_t write(
        const float* input,
        size_t frames
    )
    {
        size_t framesToWrite =
            std::min(
                frames,
                freeSpace()
            );

        for (size_t i = 0;
             i < framesToWrite;
             ++i)
        {
            size_t frameIndex =
                (writePos + i)
                % capacityFrames;

            for (int c = 0;
                 c < channels;
                 ++c)
            {
                data[
                    frameIndex * channels + c
                ] =
                    input[
                        i * channels + c
                    ];
            }
        }

        writePos =
            (writePos + framesToWrite)
            % capacityFrames;

        availableFrames +=
            framesToWrite;

        return framesToWrite;
    }

    size_t read(
        float* output,
        size_t frames
    )
    {
        size_t framesToRead =
            std::min(
                frames,
                availableFrames
            );

        for (size_t i = 0;
             i < framesToRead;
             ++i)
        {
            size_t frameIndex =
                (readPos + i)
                % capacityFrames;

            for (int c = 0;
                 c < channels;
                 ++c)
            {
                output[
                    i * channels + c
                ] =
                    data[
                        frameIndex * channels + c
                    ];
            }
        }

        readPos =
            (readPos + framesToRead)
            % capacityFrames;

        availableFrames -=
            framesToRead;

        return framesToRead;
    }
};

// ============================================================
// ADAPTIVE RESAMPLER
// ============================================================

class AdaptiveResampler
{
private:

    double sourcePosition = 0.0;

public:

    size_t process(
        const float* input,
        size_t inputFrames,
        int channels,
        int inputRate,
        int outputRate,
        double correction,
        std::vector<float>& output
    )
    {
        if (inputFrames < 2)
            return 0;

        double ratio =
            static_cast<double>(inputRate)
            /
            static_cast<double>(outputRate);

        ratio *=
            (1.0 + correction);

        double maxOutputFrames =
            (inputFrames - 1)
            /
            ratio;

        size_t outputFrames =
            static_cast<size_t>(
                maxOutputFrames
            );

        output.resize(
            outputFrames * channels
        );

        size_t produced = 0;

        while (produced < outputFrames)
        {
            size_t index =
                static_cast<size_t>(
                    sourcePosition
                );

            if (index + 1 >= inputFrames)
                break;

            double fraction =
                sourcePosition - index;

            for (int c = 0;
                 c < channels;
                 ++c)
            {
                float a =
                    input[
                        index * channels + c
                    ];

                float b =
                    input[
                        (index + 1)
                        * channels + c
                    ];

                output[
                    produced * channels + c
                ] =
                    static_cast<float>(
                        a
                        +
                        (b - a)
                        * fraction
                    );
            }

            sourcePosition += ratio;

            produced++;
        }

        sourcePosition -=
            static_cast<double>(
                inputFrames
            );

        if (sourcePosition < 0.0)
            sourcePosition = 0.0;

        output.resize(
            produced * channels
        );

        return produced;
    }
};

// ============================================================
// AUDIO ENGINE IMPLEMENTATION
// ============================================================

class AudioEngine::Impl
{
public:

    std::vector<IMMDevice*> devices;

    AudioOutput cobra;
    AudioOutput mivi;

    // Selected output devices.
    int cobraIndex = DEFAULT_COBRA_INDEX;
    int miviIndex = DEFAULT_MIVI_INDEX;

    IMMDevice* captureDevice = nullptr;

    IAudioClient* captureClient = nullptr;

    IAudioCaptureClient* captureService = nullptr;

    WAVEFORMATEX* captureFormat = nullptr;

    UINT32 captureBufferFrames = 0;

    RingBuffer* cobraRing = nullptr;

    RingBuffer* miviRing = nullptr;

    AdaptiveResampler resampler;

    std::vector<float> inputBuffer;

    std::vector<float> resampledBuffer;

    std::atomic<bool> running{ false };

    std::thread audioThread;

    bool initialized = false;

    int captureSampleRate = 0;

    int captureChannels = 0;

    size_t totalCapturedFrames = 0;

    // ========================================================
    // INDIVIDUAL VOLUME
    // ========================================================

    float cobraVolume = 1.0f;

    float miviVolume = 1.0f;

    // ========================================================
    // DESTRUCTOR
    // ========================================================

    ~Impl()
    {
        stop();

        cleanup();
    }

    // ========================================================
    // GET DEVICES
    // ========================================================

    std::vector<AudioDeviceInfo> getDevices() const
    {
        std::vector<AudioDeviceInfo> result;

        HRESULT hr =
            CoInitializeEx(
                nullptr,
                COINIT_MULTITHREADED
            );

        bool ownsCom =
            SUCCEEDED(hr);

        if (FAILED(hr) &&
            hr != RPC_E_CHANGED_MODE)
        {
            return result;
        }

        std::vector<IMMDevice*> foundDevices =
            enumerateDevices();

        for (size_t i = 0;
             i < foundDevices.size();
             ++i)
        {
            AudioDeviceInfo info;

            info.index =
                static_cast<int>(i);

            info.name =
                getDeviceName(foundDevices[i]);

            result.push_back(info);

            foundDevices[i]->Release();
        }

        if (ownsCom)
        {
            CoUninitialize();
        }

        return result;
    }

    // ========================================================
    // SET OUTPUT DEVICES
    // ========================================================

    bool setOutputDevices(
        int firstDeviceIndex,
        int secondDeviceIndex
    )
    {
        if (running)
        {
            std::cerr
                << "Cannot change output devices while "
                   "the engine is running.\n";

            return false;
        }

        if (firstDeviceIndex < 0 ||
            secondDeviceIndex < 0)
        {
            std::cerr
                << "Invalid output device index.\n";

            return false;
        }

        if (firstDeviceIndex == secondDeviceIndex)
        {
            std::cerr
                << "Please select two different speakers.\n";

            return false;
        }

        auto availableDevices = getDevices();

        if (firstDeviceIndex >=
                static_cast<int>(availableDevices.size()) ||
            secondDeviceIndex >=
                static_cast<int>(availableDevices.size()))
        {
            std::cerr
                << "Output device index is out of range.\n";

            return false;
        }

        // If the engine was initialized previously but is currently
        // stopped, release the old WASAPI endpoints so the new selection
        // will be applied when initialize() runs again.
        if (initialized)
        {
            cleanup();
        }

        cobraIndex = firstDeviceIndex;
        miviIndex = secondDeviceIndex;

        return true;
    }

    // ========================================================
    // PRINT DEVICES
    // ========================================================

    void printDevices()
    {
        std::cout
            << "\nAvailable audio output devices:\n\n";

        for (size_t i = 0;
             i < devices.size();
             ++i)
        {
            std::cout
                << "["
                << i
                << "] "
                << getDeviceName(
                    devices[i]
                )
                << "\n";
        }

        std::cout << "\n";
    }

    // ========================================================
    // INITIALIZE
    // ========================================================

    bool initialize()
    {
        HRESULT hr =
            CoInitializeEx(
                nullptr,
                COINIT_MULTITHREADED
            );

        if (FAILED(hr) &&
            hr != RPC_E_CHANGED_MODE)
        {
            std::cerr
                << "CoInitializeEx failed\n";

            return false;
        }

        devices =
            enumerateDevices();

        if (devices.empty())
        {
            std::cerr
                << "No audio output devices found.\n";

            return false;
        }

        printDevices();

        // ----------------------------------------------------
        // CHECK OUTPUT INDICES
        // ----------------------------------------------------

        if (cobraIndex < 0 ||
            cobraIndex >= static_cast<int>(
                devices.size()
            ))
        {
            std::cerr
                << "Invalid Cobra device index.\n";

            return false;
        }

        if (miviIndex < 0 ||
            miviIndex >= static_cast<int>(
                devices.size()
            ))
        {
            std::cerr
                << "Invalid Mivi device index.\n";

            return false;
        }

        // ----------------------------------------------------
        // OUTPUT SELECTION
        // ----------------------------------------------------

        std::cout
            << "Using first output: ["
            << cobraIndex
            << "] "
            << getDeviceName(
                devices[cobraIndex]
            )
            << "\n";

        std::cout
            << "Using second output: ["
            << miviIndex
            << "] "
            << getDeviceName(
                devices[miviIndex]
            )
            << "\n";

        // ----------------------------------------------------
        // INITIALIZE COBRA
        // ----------------------------------------------------

        if (!initializeOutput(
                devices[cobraIndex],
                cobra))
        {
            return false;
        }

        // ----------------------------------------------------
        // INITIALIZE MIVI
        // ----------------------------------------------------

        if (!initializeOutput(
                devices[miviIndex],
                mivi))
        {
            return false;
        }

        // ----------------------------------------------------
        // CAPTURE DEVICE
        // ----------------------------------------------------

        IMMDeviceEnumerator* enumerator =
            nullptr;

        hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            IID_PPV_ARGS(&enumerator)
        );

        if (FAILED(hr))
        {
            std::cerr
                << "Failed to create device enumerator "
                   "for capture.\n";

            return false;
        }

        // ----------------------------------------------------
        // CAPTURE DEVICE
        // ----------------------------------------------------
        //
        // Loopback capture uses the Windows default render endpoint.
        // This is separate from the two output speakers exposed by
        // this engine. Other output devices are not selectable.

        hr = enumerator->GetDefaultAudioEndpoint(
            eRender,
            eConsole,
            &captureDevice
        );

        if (FAILED(hr) || captureDevice == nullptr)
        {
            std::cerr
                << "Failed to get Windows default audio output for loopback capture.\n";

            enumerator->Release();
            return false;
        }

        enumerator->Release();

        std::cout
            << "\nCapture source: "
            << getDeviceName(captureDevice)
            << "\n";

        // ----------------------------------------------------
        // CAPTURE CLIENT
        // ----------------------------------------------------

        hr = captureDevice->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            reinterpret_cast<void**>(
                &captureClient
            )
        );

        if (FAILED(hr))
        {
            std::cerr
                << "Failed to activate capture client.\n";

            return false;
        }

        hr =
            captureClient->GetMixFormat(
                &captureFormat
            );

        if (FAILED(hr))
        {
            std::cerr
                << "Capture GetMixFormat failed.\n";

            return false;
        }

        captureSampleRate =
            static_cast<int>(
                captureFormat->nSamplesPerSec
            );

        captureChannels =
            static_cast<int>(
                captureFormat->nChannels
            );

        std::cout
            << "Capture sample rate: "
            << captureSampleRate
            << "\n";

        std::cout
            << "Capture channels: "
            << captureChannels
            << "\n";

        if (!isFloat32(
                captureFormat))
        {
            std::cerr
                << "WARNING: Capture format is not "
                   "FLOAT32.\n";

            return false;
        }

        // ----------------------------------------------------
        // LOOPBACK CAPTURE
        // ----------------------------------------------------

        REFERENCE_TIME bufferDuration =
            1000000;

        hr =
            captureClient->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_LOOPBACK,
                bufferDuration,
                0,
                captureFormat,
                nullptr
            );

        if (FAILED(hr))
        {
            std::cerr
                << "Capture Initialize failed. "
                << "HRESULT=0x"
                << std::hex
                << hr
                << std::dec
                << "\n";

            return false;
        }

        hr =
            captureClient->GetBufferSize(
                &captureBufferFrames
            );

        if (FAILED(hr))
        {
            std::cerr
                << "Capture GetBufferSize failed.\n";

            return false;
        }

        hr =
            captureClient->GetService(
                IID_PPV_ARGS(
                    &captureService
                )
            );

        if (FAILED(hr))
        {
            std::cerr
                << "Failed to get "
                   "IAudioCaptureClient.\n";

            return false;
        }

        // ----------------------------------------------------
        // RING BUFFERS
        // ----------------------------------------------------

        size_t cobraRingFrames =
            static_cast<size_t>(
                cobra.sampleRate
                *
                RING_BUFFER_SECONDS
            );

        size_t miviRingFrames =
            static_cast<size_t>(
                mivi.sampleRate
                *
                RING_BUFFER_SECONDS
            );

        cobraRing =
            new RingBuffer(
                cobraRingFrames,
                cobra.channels
            );

        miviRing =
            new RingBuffer(
                miviRingFrames,
                mivi.channels
            );

        std::cout
            << "\nRing buffer:\n";

        std::cout
            << "  Cobra: "
            << cobraRingFrames
            << " frames\n";

        std::cout
            << "  Mivi: "
            << miviRingFrames
            << " frames\n";

        initialized = true;

        std::cout
            << "\nAudioEngine initialized successfully.\n";

        return true;
    }

    // ========================================================
    // START
    // ========================================================

    bool start()
    {
        if (!initialized)
        {
            std::cerr
                << "AudioEngine is not initialized.\n";

            return false;
        }

        if (running)
            return true;

        std::cout
            << "\nStarting audio engine...\n";

        // ----------------------------------------------------
        // START CAPTURE
        // ----------------------------------------------------

        HRESULT hr =
            captureClient->Start();

        if (FAILED(hr))
        {
            std::cerr
                << "Capture Start failed.\n";

            return false;
        }

        // ----------------------------------------------------
        // INITIAL BUFFER
        // ----------------------------------------------------

        std::cout
            << "Filling initial buffer...\n";

        const size_t requiredCobraFrames =
            static_cast<size_t>(
                cobra.sampleRate
                *
                START_BUFFER_MS
                /
                1000.0
            );

        while (
            cobraRing->available()
            <
            requiredCobraFrames
        )
        {
            if (!captureAudioPacket())
            {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(1)
                );
            }
        }

        std::cout
            << "Initial buffer ready.\n";

        std::cout
            << "Cobra buffer: "
            << cobraRing->available()
            << " frames\n";

        std::cout
            << "Mivi buffer: "
            << miviRing->available()
            << " frames\n";

        // ----------------------------------------------------
        // START COBRA
        // ----------------------------------------------------

        hr =
            cobra.client->Start();

        if (FAILED(hr))
        {
            std::cerr
                << "Cobra Start failed.\n";

            captureClient->Stop();

            return false;
        }

        // ----------------------------------------------------
        // START MIVI
        // ----------------------------------------------------

        hr =
            mivi.client->Start();

        if (FAILED(hr))
        {
            std::cerr
                << "Mivi Start failed.\n";

            cobra.client->Stop();

            captureClient->Stop();

            return false;
        }

        running = true;

        audioThread =
            std::thread(
                &Impl::audioLoop,
                this
            );

        std::cout
            << "\n========================================\n";

        std::cout
            << "AUDIO ENGINE RUNNING\n";

        std::cout
            << "========================================\n";

        return true;
    }

    // ========================================================
    // CAPTURE AUDIO PACKET
    // ========================================================

    bool captureAudioPacket()
    {
        if (captureService == nullptr)
            return false;

        UINT32 packetLength = 0;

        HRESULT hr =
            captureService->GetNextPacketSize(
                &packetLength
            );

        if (FAILED(hr))
            return false;

        if (packetLength == 0)
            return false;

        BYTE* data = nullptr;

        UINT32 numFrames = 0;

        DWORD flags = 0;

        hr =
            captureService->GetBuffer(
                &data,
                &numFrames,
                &flags,
                nullptr,
                nullptr
            );

        if (FAILED(hr))
            return false;

        // ----------------------------------------------------
        // SILENCE
        // ----------------------------------------------------

        if (flags &
            AUDCLNT_BUFFERFLAGS_SILENT)
        {
            inputBuffer.assign(
                static_cast<size_t>(
                    numFrames
                )
                *
                captureChannels,
                0.0f
            );
        }
        else
        {
            float* floatData =
                reinterpret_cast<float*>(
                    data
                );

            inputBuffer.assign(
                floatData,
                floatData
                +
                static_cast<size_t>(
                    numFrames
                )
                *
                captureChannels
            );
        }

        captureService->ReleaseBuffer(
            numFrames
        );

        totalCapturedFrames +=
            numFrames;

        // ----------------------------------------------------
        // CHANNEL CHECK
        // ----------------------------------------------------

        if (captureChannels !=
            cobra.channels ||
            captureChannels !=
            mivi.channels)
        {
            std::cerr
                << "Channel count mismatch. "
                << "Capture="
                << captureChannels
                << " Cobra="
                << cobra.channels
                << " Mivi="
                << mivi.channels
                << "\n";

            return false;
        }

        // ----------------------------------------------------
        // RESAMPLE
        // ----------------------------------------------------

        size_t outputFrames =
            resampler.process(
                inputBuffer.data(),
                numFrames,
                captureChannels,
                captureSampleRate,
                cobra.sampleRate,
                0.0,
                resampledBuffer
            );

        if (outputFrames == 0)
            return true;

        // ----------------------------------------------------
        // WRITE SAME AUDIO TO BOTH RINGS
        // ----------------------------------------------------

        cobraRing->write(
            resampledBuffer.data(),
            outputFrames
        );

        miviRing->write(
            resampledBuffer.data(),
            outputFrames
        );

        return true;
    }

    // ========================================================
    // RENDER OUTPUT
    // ========================================================

    bool renderOutput(
        AudioOutput& output,
        RingBuffer& ring,
        size_t frames,
        float volume
    )
    {
        if (frames == 0)
            return true;

        UINT32 padding = 0;

        HRESULT hr =
            output.client->GetCurrentPadding(
                &padding
            );

        if (FAILED(hr))
            return false;

        UINT32 available =
            output.bufferFrames
            -
            padding;

        if (available == 0)
            return true;

        UINT32 framesToRender =
            static_cast<UINT32>(
                std::min(
                    static_cast<size_t>(
                        available
                    ),
                    frames
                )
            );

        BYTE* data = nullptr;

        hr =
            output.renderClient->GetBuffer(
                framesToRender,
                &data
            );

        if (FAILED(hr))
            return false;

        float* destination =
            reinterpret_cast<float*>(
                data
            );

        size_t actualRead =
            ring.read(
                destination,
                framesToRender
            );

        // ----------------------------------------------------
        // INDIVIDUAL VOLUME
        // ----------------------------------------------------
        //
        // volume:
        //
        // 0.0 = mute
        // 0.5 = 50%
        // 1.0 = 100%
        //

        for (size_t i = 0;
             i <
             actualRead *
             output.channels;
             ++i)
        {
            destination[i] *= volume;
        }

        // ----------------------------------------------------
        // FILL UNDERRUN WITH SILENCE
        // ----------------------------------------------------

        if (actualRead <
            framesToRender)
        {
            std::fill(
                destination
                +
                actualRead *
                output.channels,

                destination
                +
                framesToRender *
                output.channels,

                0.0f
            );
        }

        DWORD flags = 0;

        hr =
            output.renderClient->ReleaseBuffer(
                framesToRender,
                flags
            );

        return SUCCEEDED(hr);
    }

    // ========================================================
    // AUDIO LOOP
    // ========================================================

    void audioLoop()
    {
        const double targetCobraFrames =
            cobra.sampleRate
            *
            TARGET_BUFFER_SECONDS;

        const double targetMiviFrames =
            mivi.sampleRate
            *
            TARGET_BUFFER_SECONDS;

        while (running)
        {
            // ------------------------------------------------
            // CAPTURE AVAILABLE PACKETS
            // ------------------------------------------------

            bool capturedSomething = false;

            while (true)
            {
                if (!captureAudioPacket())
                    break;

                capturedSomething = true;
            }

            // ------------------------------------------------
            // BUFFER LEVELS
            // ------------------------------------------------

            double cobraBuffer =
                static_cast<double>(
                    cobraRing->available()
                );

            double miviBuffer =
                static_cast<double>(
                    miviRing->available()
                );

            // ------------------------------------------------
            // BUFFER ERROR
            // ------------------------------------------------

            double cobraError =
                cobraBuffer
                -
                targetCobraFrames;

            double miviError =
                miviBuffer
                -
                targetMiviFrames;

            double averageError =
                (
                    cobraError
                    +
                    miviError
                )
                /
                2.0;

            double normalizedError =
                averageError
                /
                (
                    cobra.sampleRate
                    *
                    TARGET_BUFFER_SECONDS
                );

            double correction =
                normalizedError
                *
                0.0005;

            // ------------------------------------------------
            // LIMIT CORRECTION
            // ------------------------------------------------

            if (correction >
                MAX_RATE_CORRECTION)
            {
                correction =
                    MAX_RATE_CORRECTION;
            }

            if (correction <
                -MAX_RATE_CORRECTION)
            {
                correction =
                    -MAX_RATE_CORRECTION;
            }

            // ------------------------------------------------
            // GET HARDWARE BUFFER PADDING
            // ------------------------------------------------

            UINT32 cobraPadding = 0;

            UINT32 miviPadding = 0;

            HRESULT hr1 =
                cobra.client->GetCurrentPadding(
                    &cobraPadding
                );

            HRESULT hr2 =
                mivi.client->GetCurrentPadding(
                    &miviPadding
                );

            if (FAILED(hr1) ||
                FAILED(hr2))
            {
                std::cerr
                    << "GetCurrentPadding failed.\n";

                break;
            }

            UINT32 cobraAvailable =
                cobra.bufferFrames
                -
                cobraPadding;

            UINT32 miviAvailable =
                mivi.bufferFrames
                -
                miviPadding;

            // ------------------------------------------------
            // RENDER SAME NUMBER OF FRAMES
            // ------------------------------------------------

            size_t renderFrames =
                std::min(
                    static_cast<size_t>(
                        cobraAvailable
                    ),
                    static_cast<size_t>(
                        miviAvailable
                    )
                );

            renderFrames =
                std::min(
                    renderFrames,
                    cobraRing->available()
                );

            renderFrames =
                std::min(
                    renderFrames,
                    miviRing->available()
                );

            // ------------------------------------------------
            // RENDER COBRA + MIVI
            // ------------------------------------------------

            if (renderFrames > 0)
            {
                renderOutput(
                    cobra,
                    *cobraRing,
                    renderFrames,
                    cobraVolume
                );

                renderOutput(
                    mivi,
                    *miviRing,
                    renderFrames,
                    miviVolume
                );
            }
            else
            {
                if (!capturedSomething)
                {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(1)
                    );
                }
            }

            // ------------------------------------------------
            // DIAGNOSTICS
            // ------------------------------------------------

            static auto lastPrint =
                std::chrono::steady_clock::now();

            auto now =
                std::chrono::steady_clock::now();

            auto elapsed =
                std::chrono::duration_cast<
                    std::chrono::milliseconds
                >(
                    now - lastPrint
                ).count();

            if (elapsed >= 1000)
            {
                std::cout
                    << "\r"
                    << "Cobra="
                    << cobraBuffer
                    << " frames | "
                    << "Mivi="
                    << miviBuffer
                    << " frames | "
                    << "Correction="
                    << correction
                    << " | "
                    << "CobraVol="
                    << cobraVolume
                    << " | "
                    << "MiviVol="
                    << miviVolume
                    << "       "
                    << std::flush;

                lastPrint = now;
            }
        }
    }

    // ========================================================
    // STOP
    // ========================================================

    void stop()
    {
        if (!running)
            return;

        running = false;

        if (audioThread.joinable())
        {
            audioThread.join();
        }

        if (cobra.client)
        {
            cobra.client->Stop();
        }

        if (mivi.client)
        {
            mivi.client->Stop();
        }

        if (captureClient)
        {
            captureClient->Stop();
        }

        std::cout
            << "\nAudio engine stopped.\n";
    }

    // ========================================================
    // CLEANUP
    // ========================================================

    void cleanup()
    {
        stop();

        // ----------------------------------------------------
        // RENDER CLIENTS
        // ----------------------------------------------------

        if (cobra.renderClient)
        {
            cobra.renderClient->Release();

            cobra.renderClient = nullptr;
        }

        if (mivi.renderClient)
        {
            mivi.renderClient->Release();

            mivi.renderClient = nullptr;
        }

        // ----------------------------------------------------
        // AUDIO CLIENTS
        // ----------------------------------------------------

        if (cobra.client)
        {
            cobra.client->Release();

            cobra.client = nullptr;
        }

        if (mivi.client)
        {
            mivi.client->Release();

            mivi.client = nullptr;
        }

        // ----------------------------------------------------
        // CAPTURE SERVICE
        // ----------------------------------------------------

        if (captureService)
        {
            captureService->Release();

            captureService = nullptr;
        }

        // ----------------------------------------------------
        // CAPTURE CLIENT
        // ----------------------------------------------------

        if (captureClient)
        {
            captureClient->Release();

            captureClient = nullptr;
        }

        // ----------------------------------------------------
        // FORMATS
        // ----------------------------------------------------

        if (captureFormat)
        {
            CoTaskMemFree(
                captureFormat
            );

            captureFormat = nullptr;
        }

        if (cobra.format)
        {
            CoTaskMemFree(
                cobra.format
            );

            cobra.format = nullptr;
        }

        if (mivi.format)
        {
            CoTaskMemFree(
                mivi.format
            );

            mivi.format = nullptr;
        }

        // ----------------------------------------------------
        // CAPTURE DEVICE
        // ----------------------------------------------------

        if (captureDevice)
        {
            captureDevice->Release();

            captureDevice = nullptr;
        }

        // ----------------------------------------------------
        // OUTPUT DEVICES
        // ----------------------------------------------------

        if (cobra.device)
        {
            cobra.device->Release();

            cobra.device = nullptr;
        }

        if (mivi.device)
        {
            mivi.device->Release();

            mivi.device = nullptr;
        }

        // ----------------------------------------------------
        // DEVICE LIST
        // ----------------------------------------------------

        for (IMMDevice* device :
             devices)
        {
            if (device)
            {
                device->Release();
            }
        }

        devices.clear();

        // ----------------------------------------------------
        // RING BUFFERS
        // ----------------------------------------------------

        delete cobraRing;

        cobraRing = nullptr;

        delete miviRing;

        miviRing = nullptr;

        initialized = false;

        CoUninitialize();
    }

    // ========================================================
    // IS RUNNING
    // ========================================================

    bool isRunning() const
    {
        return running;
    }
};

// ============================================================
// AUDIO ENGINE PUBLIC API
// ============================================================

AudioEngine::AudioEngine()
{
    impl = new Impl();
}

AudioEngine::~AudioEngine()
{
    delete impl;

    impl = nullptr;
}

bool AudioEngine::initialize()
{
    return impl->initialize();
}

bool AudioEngine::start()
{
    return impl->start();
}

void AudioEngine::stop()
{
    impl->stop();
}

bool AudioEngine::isRunning() const
{
    return impl->isRunning();
}

void AudioEngine::printDevices()
{
    impl->printDevices();
}

std::vector<AudioDeviceInfo> AudioEngine::getDevices() const
{
    return impl->getDevices();
}

bool AudioEngine::setOutputDevices(
    int firstDeviceIndex,
    int secondDeviceIndex
)
{
    return impl->setOutputDevices(
        firstDeviceIndex,
        secondDeviceIndex
    );
}

// ============================================================
// VOLUME CONTROL
// ============================================================

void AudioEngine::setCobraVolume(float volume)
{
    if (volume < 0.0f)
        volume = 0.0f;

    if (volume > 1.0f)
        volume = 1.0f;

    impl->cobraVolume = volume;
}

void AudioEngine::setMiviVolume(float volume)
{
    if (volume < 0.0f)
        volume = 0.0f;

    if (volume > 1.0f)
        volume = 1.0f;

    impl->miviVolume = volume;
}

float AudioEngine::getCobraVolume() const
{
    return impl->cobraVolume;
}

float AudioEngine::getMiviVolume() const
{
    return impl->miviVolume;
}