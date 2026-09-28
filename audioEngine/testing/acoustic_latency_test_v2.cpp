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
#include <cstring>

#pragma comment(lib, "ole32.lib")

constexpr double PI = 3.14159265358979323846;

constexpr int SAMPLE_RATE = 44100;
constexpr int CHANNELS = 2;

constexpr double TEST_DURATION = 15.0;

// Each burst is 250 ms long.
constexpr double BURST_DURATION = 0.250;

// Measurement bursts.
const std::vector<double> BURST_TIMES =
{
    1.0,
    2.5,
    4.0,
    5.5,
    7.0,
    8.5,
    10.0,
    11.5,
    13.0
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
        std::cerr
            << "ERROR: "
            << message
            << " HRESULT=0x"
            << std::hex
            << hr
            << std::dec
            << "\n";

        exit(1);
    }
}

std::string getDeviceName(IMMDevice* device)
{
    IPropertyStore* store = nullptr;

    HRESULT hr =
        device->OpenPropertyStore(
            STGM_READ,
            &store
        );

    if (FAILED(hr))
        return "Unknown";

    PROPVARIANT value;
    PropVariantInit(&value);

    hr =
        store->GetValue(
            PKEY_Device_FriendlyName,
            &value
        );

    std::string result = "Unknown";

    if (SUCCEEDED(hr) &&
        value.vt == VT_LPWSTR)
    {
        int size =
            WideCharToMultiByte(
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

    std::cout
        << "\nAvailable render devices:\n";

    for (UINT i = 0; i < count; i++)
    {
        IMMDevice* device = nullptr;

        checkHR(
            collection->Item(
                i,
                &device
            ),
            "Getting device"
        );

        devices.push_back(device);

        std::cout
            << "["
            << i
            << "] "
            << getDeviceName(device)
            << "\n";
    }

    collection->Release();
    enumerator->Release();

    return devices;
}

bool isFloat32(WAVEFORMATEX* format)
{
    if (
        format->wFormatTag ==
            WAVE_FORMAT_IEEE_FLOAT &&
        format->wBitsPerSample == 32
    )
    {
        return true;
    }

    if (
        format->wFormatTag ==
            WAVE_FORMAT_EXTENSIBLE
    )
    {
        auto* ext =
            reinterpret_cast<WAVEFORMATEXTENSIBLE*>(
                format
            );

        if (
            ext->Format.wBitsPerSample == 32 &&
            IsEqualGUID(
                ext->SubFormat,
                KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
            )
        )
        {
            return true;
        }
    }

    return false;
}

AudioOutput initializeOutput(
    IMMDevice* device
)
{
    AudioOutput output;

    output.device = device;

    output.device->AddRef();

    output.name =
        getDeviceName(device);

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

    std::cout
        << "\nOutput: "
        << output.name
        << "\n";

    std::cout
        << "Sample rate: "
        << output.format->nSamplesPerSec
        << "\n";

    std::cout
        << "Channels: "
        << output.format->nChannels
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
        std::cerr
            << "ERROR: Output is not FLOAT32.\n";

        exit(1);
    }

    if (
        output.format->nSamplesPerSec !=
        SAMPLE_RATE
    )
    {
        std::cerr
            << "ERROR: Expected 44100 Hz.\n";

        exit(1);
    }

    if (
        output.format->nChannels !=
        CHANNELS
    )
    {
        std::cerr
            << "ERROR: Expected stereo.\n";

        exit(1);
    }

    REFERENCE_TIME bufferDuration =
        10000000LL / 10;

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

    std::cout
        << "Buffer frames: "
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

/*
    Generate a frequency sweep.

    Starts at 500 Hz and sweeps to 5000 Hz.

    A sweep has a much stronger unique
    fingerprint than a simple tone.
*/
void generateSweep(
    std::vector<float>& audio,
    int startFrame
)
{
    int length =
        static_cast<int>(
            BURST_DURATION *
            SAMPLE_RATE
        );

    for (int i = 0; i < length; i++)
    {
        int frame =
            startFrame + i;

        if (
            frame < 0 ||
            frame >=
                static_cast<int>(
                    audio.size() /
                    CHANNELS
                )
        )
        {
            break;
        }

        double t =
            static_cast<double>(i) /
            SAMPLE_RATE;

        double progress =
            t / BURST_DURATION;

        /*
            Logarithmic frequency sweep.
        */

        double f0 = 500.0;
        double f1 = 5000.0;

        double frequency =
            f0 *
            std::pow(
                f1 / f0,
                progress
            );

        /*
            Phase for logarithmic chirp.
        */

        double phase =
            2.0 *
            PI *
            f0 *
            BURST_DURATION /
            std::log(f1 / f0) *
            (
                std::pow(
                    f1 / f0,
                    progress
                ) - 1.0
            );

        /*
            Fade in/out to avoid clicks
            unrelated to the measurement.
        */

        double fadeTime = 0.010;

        double envelope = 1.0;

        if (t < fadeTime)
        {
            envelope =
                t / fadeTime;
        }
        else if (
            t >
            BURST_DURATION -
                fadeTime
        )
        {
            envelope =
                (
                    BURST_DURATION -
                    t
                ) / fadeTime;
        }

        if (envelope < 0.0)
            envelope = 0.0;

        if (envelope > 1.0)
            envelope = 1.0;

        float sample =
            static_cast<float>(
                0.80 *
                envelope *
                std::sin(phase)
            );

        audio[frame * 2] =
            sample;

        audio[frame * 2 + 1] =
            sample;
    }
}

std::vector<float> generateTestSignal(
    int totalFrames
)
{
    std::vector<float> audio(
        totalFrames *
        CHANNELS,
        0.0f
    );

    for (double time : BURST_TIMES)
    {
        int startFrame =
            static_cast<int>(
                time *
                SAMPLE_RATE
            );

        generateSweep(
            audio,
            startFrame
        );
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
        output.bufferFrames -
        padding;

    if (available == 0)
        return;

    int totalFrames =
        static_cast<int>(
            audio.size() /
            CHANNELS
        );

    int remaining =
        totalFrames -
        currentFrame;

    if (remaining <= 0)
        return;

    UINT32 framesToWrite =
        static_cast<UINT32>(
            remaining <
            static_cast<int>(
                available
            )
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
        reinterpret_cast<float*>(
            data
        );

    const float* source =
        audio.data() +
        currentFrame *
        CHANNELS;

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
        static_cast<int>(
            framesToWrite
        );
}

void releaseOutput(
    AudioOutput& output
)
{
    if (output.render)
        output.render->Release();

    if (output.client)
        output.client->Release();

    if (output.device)
        output.device->Release();

    if (output.format)
        CoTaskMemFree(
            output.format
        );
}

int main()
{
    CoInitializeEx(
        nullptr,
        COINIT_MULTITHREADED
    );

    std::cout
        << "========================================\n"
        << " Acoustic Latency Test V2\n"
        << " Coded Sweep Measurement\n"
        << "========================================\n";

    auto devices =
        enumerateRenderDevices();

    if (devices.size() < 2)
    {
        std::cerr
            << "Need at least two render devices.\n";

        return 1;
    }

    /*
        Current devices:

        [0] Cobra
        [1] Mivi
    */

    int cobraIndex = 0;
    int miviIndex = 1;

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
        << "\nGenerating coded sweep signal...\n";

    auto audio =
        generateTestSignal(
            totalFrames
        );

    /*
        Save exact measurement positions.
    */

    std::ofstream file(
        "acoustic_test_positions.txt"
    );

    if (file)
    {
        file
            << "SampleRate="
            << SAMPLE_RATE
            << "\n";

        file
            << "BurstDuration="
            << BURST_DURATION
            << "\n";

        file
            << "Bursts:\n";

        for (double t : BURST_TIMES)
        {
            file
                << std::fixed
                << std::setprecision(6)
                << t
                << "\n";
        }
    }

    std::cout
        << "\nMeasurement bursts:\n";

    for (double t : BURST_TIMES)
    {
        std::cout
            << "  "
            << std::fixed
            << std::setprecision(3)
            << t
            << " sec -> "
            << BURST_DURATION
            << " sec sweep\n";
    }

    std::cout
        << "\n========================================\n"
        << " RECORDING SETUP\n"
        << "========================================\n\n";

    std::cout
        << "Place the microphone approximately\n"
        << "the same distance from both speakers.\n\n";

    std::cout
        << "Example:\n\n";

    std::cout
        << "       microphone\n"
        << "           |\n"
        << "     1m    |    1m\n"
        << "           |\n"
        << "    Cobra       Mivi\n\n";

    std::cout
        << "Start your phone recorder NOW.\n\n";

    std::cout
        << "The test starts in 5 seconds...\n";

    for (int i = 5; i > 0; i--)
    {
        std::cout
            << i
            << "...\n";

        std::this_thread::sleep_for(
            std::chrono::seconds(1)
        );
    }

    int cobraFrame = 0;
    int miviFrame = 0;

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
            audio,
            cobraFrame
        );

        fillOutput(
            mivi,
            audio,
            miviFrame
        );

        std::this_thread::sleep_for(
            std::chrono::milliseconds(5)
        );

        auto now =
            std::chrono::steady_clock::now();

        double elapsed =
            std::chrono::duration<double>(
                now -
                startTime
            ).count();

        static int lastSecond = -1;

        int currentSecond =
            static_cast<int>(
                elapsed
            );

        if (
            currentSecond !=
            lastSecond
        )
        {
            lastSecond =
                currentSecond;

            std::cout
                << "Time: "
                << currentSecond
                << " sec"
                << " | Cobra frames: "
                << cobraFrame
                << " | Mivi frames: "
                << miviFrame
                << "\n";
        }
    }

    std::cout
        << "\nAll audio submitted.\n";

    /*
        Allow final Bluetooth audio
        to physically play.
    */

    std::this_thread::sleep_for(
        std::chrono::seconds(4)
    );

    cobra.client->Stop();
    mivi.client->Stop();

    std::cout
        << "\n========================================\n"
        << " TEST FINISHED\n"
        << "========================================\n\n";

    std::cout
        << "Stop your phone recording.\n\n";

    std::cout
        << "The recording should contain repeated\n"
        << "500 Hz -> 5000 Hz frequency sweeps.\n\n";

    std::cout
        << "Send me that recording if possible.\n";

    releaseOutput(cobra);
    releaseOutput(mivi);

    for (auto device : devices)
        device->Release();

    CoUninitialize();

    return 0;
}