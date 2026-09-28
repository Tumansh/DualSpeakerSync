#define NOMINMAX

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>

#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <thread>
#include <chrono>
#include <fstream>
#include <iomanip>

#pragma comment(lib, "ole32.lib")

constexpr double PI = 3.14159265358979323846;

constexpr int SAMPLE_RATE = 44100;
constexpr int CHANNELS = 2;

constexpr double TEST_DURATION = 12.0;

// Click locations in seconds
const std::vector<double> CLICK_TIMES = {
    1.0,
    2.5,
    4.0,
    5.5,
    7.0,
    8.5,
    10.0,
    11.5
};

struct AudioOutput
{
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;

    WAVEFORMATEX* format = nullptr;

    UINT32 bufferFrames = 0;

    std::string name;
};

void checkHR(HRESULT hr, const char* message)
{
    if (FAILED(hr))
    {
        std::cerr << "ERROR: " << message
                  << " HRESULT=0x"
                  << std::hex << hr << std::dec << "\n";

        exit(1);
    }
}

std::string getDeviceName(IMMDevice* device)
{
    IPropertyStore* store = nullptr;

    HRESULT hr = device->OpenPropertyStore(
        STGM_READ,
        &store
    );

    if (FAILED(hr))
        return "Unknown";

    PROPVARIANT value;
    PropVariantInit(&value);

    hr = store->GetValue(
        PKEY_Device_FriendlyName,
        &value
    );

    std::string result = "Unknown";

    if (SUCCEEDED(hr) && value.vt == VT_LPWSTR)
    {
        int size = WideCharToMultiByte(
            CP_UTF8,
            0,
            value.pwszVal,
            -1,
            nullptr,
            0,
            nullptr,
            nullptr
        );

        if (size > 0)
        {
            std::vector<char> buffer(size);

            WideCharToMultiByte(
                CP_UTF8,
                0,
                value.pwszVal,
                -1,
                buffer.data(),
                size,
                nullptr,
                nullptr
            );

            result = buffer.data();
        }
    }

    PropVariantClear(&value);
    store->Release();

    return result;
}

std::vector<IMMDevice*> enumerateRenderDevices()
{
    IMMDeviceEnumerator* enumerator = nullptr;

    checkHR(
        CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            (void**)&enumerator
        ),
        "Creating device enumerator"
    );

    IMMDeviceCollection* collection = nullptr;

    checkHR(
        enumerator->EnumAudioEndpoints(
            eRender,
            DEVICE_STATE_ACTIVE,
            &collection
        ),
        "Enumerating render devices"
    );

    UINT count = 0;

    collection->GetCount(&count);

    std::vector<IMMDevice*> devices;

    std::cout << "\nAvailable render devices:\n";

    for (UINT i = 0; i < count; i++)
    {
        IMMDevice* device = nullptr;

        collection->Item(i, &device);

        devices.push_back(device);

        std::cout
            << "[" << i << "] "
            << getDeviceName(device)
            << "\n";
    }

    collection->Release();
    enumerator->Release();

    return devices;
}

bool isFloat32(WAVEFORMATEX* format)
{
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT &&
        format->wBitsPerSample == 32)
    {
        return true;
    }

    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        auto* extensible =
            reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format);

        if (extensible->Format.wBitsPerSample == 32 &&
            IsEqualGUID(
                extensible->SubFormat,
                KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
            ))
        {
            return true;
        }
    }

    return false;
}

AudioOutput initializeOutput(IMMDevice* device)
{
    AudioOutput output;

    output.device = device;
    output.device->AddRef();

    output.name = getDeviceName(device);

    checkHR(
        device->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            (void**)&output.client
        ),
        "Activating audio client"
    );

    checkHR(
        output.client->GetMixFormat(
            &output.format
        ),
        "Getting mix format"
    );

    std::cout << "\nOutput: "
              << output.name
              << "\n";

    std::cout << "Sample rate: "
              << output.format->nSamplesPerSec
              << "\n";

    std::cout << "Channels: "
              << output.format->nChannels
              << "\n";

    std::cout << "Bits: "
              << output.format->wBitsPerSample
              << "\n";

    std::cout << "Format tag: "
              << output.format->wFormatTag
              << "\n";

    if (!isFloat32(output.format))
    {
        std::cerr
            << "ERROR: Output is not FLOAT32.\n";

        exit(1);
    }

    if (output.format->nSamplesPerSec != SAMPLE_RATE)
    {
        std::cerr
            << "ERROR: Expected 44100 Hz.\n";

        exit(1);
    }

    if (output.format->nChannels != CHANNELS)
    {
        std::cerr
            << "ERROR: Expected stereo.\n";

        exit(1);
    }

    REFERENCE_TIME bufferDuration =
        10000000LL / 10; // 100 ms

    checkHR(
        output.client->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            0,
            bufferDuration,
            0,
            output.format,
            nullptr
        ),
        "Initializing audio client"
    );

    checkHR(
        output.client->GetBufferSize(
            &output.bufferFrames
        ),
        "Getting buffer size"
    );

    std::cout << "Buffer frames: "
              << output.bufferFrames
              << "\n";

    checkHR(
        output.client->GetService(
            __uuidof(IAudioRenderClient),
            (void**)&output.render
        ),
        "Getting render client"
    );

    return output;
}

std::vector<float> generateClickTrack(
    int totalFrames
)
{
    std::vector<float> audio(
        totalFrames * CHANNELS,
        0.0f
    );

    // Very short click:
    // 2 ms = approximately 88 samples
    constexpr int CLICK_LENGTH = 88;

    constexpr double FREQUENCY = 1000.0;

    for (double clickTime : CLICK_TIMES)
    {
        int startFrame =
            static_cast<int>(
                clickTime * SAMPLE_RATE
            );

        for (int i = 0; i < CLICK_LENGTH; i++)
        {
            int frame = startFrame + i;

            if (frame >= totalFrames)
                break;

            // Short Hann-like envelope
            double envelope =
                0.5 *
                (1.0 -
                 std::cos(
                     2.0 * PI *
                     static_cast<double>(i) /
                     static_cast<double>(CLICK_LENGTH - 1)
                 ));

            double sample =
                0.75 *
                std::sin(
                    2.0 *
                    PI *
                    FREQUENCY *
                    static_cast<double>(i) /
                    SAMPLE_RATE
                ) *
                envelope;

            float value =
                static_cast<float>(sample);

            audio[frame * 2] = value;
            audio[frame * 2 + 1] = value;
        }
    }

    return audio;
}

void fillOutput(
    AudioOutput& output,
    const std::vector<float>& audio,
    int& currentFrame
)
{
    UINT32 padding = 0;

    checkHR(
        output.client->GetCurrentPadding(
            &padding
        ),
        "Getting current padding"
    );

    UINT32 available =
        output.bufferFrames - padding;

    if (available == 0)
        return;

    int remaining =
        static_cast<int>(audio.size() / CHANNELS)
        - currentFrame;

    if (remaining <= 0)
        return;

    UINT32 framesToWrite =
        static_cast<UINT32>(
            remaining < static_cast<int>(available)
                ? remaining
                : available
        );

    BYTE* data = nullptr;

    checkHR(
        output.render->GetBuffer(
            framesToWrite,
            &data
        ),
        "Getting render buffer"
    );

    float* destination =
        reinterpret_cast<float*>(data);

    const float* source =
        audio.data() +
        currentFrame * CHANNELS;

    memcpy(
        destination,
        source,
        framesToWrite *
        CHANNELS *
        sizeof(float)
    );

    checkHR(
        output.render->ReleaseBuffer(
            framesToWrite,
            0
        ),
        "Releasing render buffer"
    );

    currentFrame +=
        static_cast<int>(framesToWrite);
}

void releaseOutput(AudioOutput& output)
{
    if (output.render)
        output.render->Release();

    if (output.client)
        output.client->Release();

    if (output.device)
        output.device->Release();

    if (output.format)
        CoTaskMemFree(output.format);
}

int main()
{
    CoInitializeEx(
        nullptr,
        COINIT_MULTITHREADED
    );

    std::cout
        << "========================================\n"
        << " Acoustic Bluetooth Latency Test\n"
        << "========================================\n";

    auto devices =
        enumerateRenderDevices();

    if (devices.size() < 2)
    {
        std::cerr
            << "Need at least two render devices.\n";

        return 1;
    }

    // Current devices:
    // 0 = Cobra
    // 1 = Mivi
    int cobraIndex = 0;
    int miviIndex = 1;

    std::cout
        << "\nUsing:\n"
        << "Cobra = device ["
        << cobraIndex
        << "]\n"
        << "Mivi  = device ["
        << miviIndex
        << "]\n";

    AudioOutput cobra =
        initializeOutput(
            devices[cobraIndex]
        );

    AudioOutput mivi =
        initializeOutput(
            devices[miviIndex]
        );

    int totalFrames =
        static_cast<int>(
            TEST_DURATION *
            SAMPLE_RATE
        );

    std::cout
        << "\nGenerating click track...\n";

    auto clickTrack =
        generateClickTrack(
            totalFrames
        );

    std::cout
        << "\nClick positions:\n";

    for (double t : CLICK_TIMES)
    {
        std::cout
            << "  "
            << std::fixed
            << std::setprecision(3)
            << t
            << " sec\n";
    }

    /*
        Save the click positions.

        This makes it easier to compare the
        recorded microphone waveform later.
    */

    std::ofstream timingFile(
        "click_positions.txt"
    );

    if (timingFile)
    {
        timingFile
            << "SampleRate="
            << SAMPLE_RATE
            << "\n";

        for (double t : CLICK_TIMES)
        {
            timingFile
                << std::fixed
                << std::setprecision(6)
                << t
                << "\n";
        }
    }

    int cobraFrame = 0;
    int miviFrame = 0;

    std::cout
        << "\n========================================\n"
        << " GET READY TO RECORD\n"
        << "========================================\n";

    std::cout
        << "\nPlace the microphone approximately\n"
        << "the same distance from both speakers.\n\n";

    std::cout
        << "Start your phone/recorder NOW.\n\n";

    std::cout
        << "The test will start in 3 seconds...\n";

    std::this_thread::sleep_for(
        std::chrono::seconds(3)
    );

    /*
        Start both WASAPI clients as close
        together as possible.
    */

    checkHR(
        cobra.client->Start(),
        "Starting Cobra"
    );

    checkHR(
        mivi.client->Start(),
        "Starting Mivi"
    );

    auto startTime =
        std::chrono::steady_clock::now();

    std::cout
        << "\n========================================\n"
        << " PLAYBACK STARTED\n"
        << "========================================\n";

    while (
        cobraFrame < totalFrames ||
        miviFrame < totalFrames
    )
    {
        fillOutput(
            cobra,
            clickTrack,
            cobraFrame
        );

        fillOutput(
            mivi,
            clickTrack,
            miviFrame
        );

        std::this_thread::sleep_for(
            std::chrono::milliseconds(5)
        );

        auto now =
            std::chrono::steady_clock::now();

        double elapsed =
            std::chrono::duration<double>(
                now - startTime
            ).count();

        static int lastSecond = -1;

        int currentSecond =
            static_cast<int>(elapsed);

        if (currentSecond != lastSecond)
        {
            lastSecond = currentSecond;

            std::cout
                << "Time: "
                << currentSecond
                << " sec | Cobra frames: "
                << cobraFrame
                << " | Mivi frames: "
                << miviFrame
                << "\n";
        }
    }

    std::cout
        << "\nAll audio submitted.\n";

    /*
        Wait so the final audio actually
        leaves the Bluetooth speakers.
    */

    std::this_thread::sleep_for(
        std::chrono::seconds(3)
    );

    cobra.client->Stop();
    mivi.client->Stop();

    std::cout
        << "\n========================================\n"
        << " TEST FINISHED\n"
        << "========================================\n";

    std::cout
        << "\nNow stop the recording.\n";

    std::cout
        << "\nNext step:\n"
        << "Send me the recording file.\n";

    std::cout
        << "\nI will analyze the waveform and find:\n"
        << "  Cobra click arrival time\n"
        << "  Mivi click arrival time\n"
        << "  Difference in milliseconds\n";

    releaseOutput(cobra);
    releaseOutput(mivi);

    for (auto device : devices)
        device->Release();

    CoUninitialize();

    return 0;
}