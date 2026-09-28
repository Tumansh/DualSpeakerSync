#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>

#include <iostream>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "ole32.lib")

struct AudioOutput
{
    IMMDevice* device = nullptr;
    IAudioClient* audioClient = nullptr;
    IAudioRenderClient* renderClient = nullptr;
    WAVEFORMATEX* format = nullptr;

    UINT32 bufferSize = 0;
    UINT32 channels = 0;
    double sampleRate = 0.0;

    bool isFloat = false;
    bool isPCM16 = false;

    // Position in this output's audio timeline
    double audioTime = 0.0;
};


// ========================================================
// Detect audio format
// ========================================================

bool detectAudioFormat(AudioOutput& output)
{
    WAVEFORMATEX* format = output.format;

    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        output.isFloat = true;
        return true;
    }

    if (format->wFormatTag == WAVE_FORMAT_PCM)
    {
        if (format->wBitsPerSample == 16)
        {
            output.isPCM16 = true;
            return true;
        }
    }

    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        WAVEFORMATEXTENSIBLE* extensible =
            reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format);

        if (IsEqualGUID(
                extensible->SubFormat,
                KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
        {
            output.isFloat = true;
            return true;
        }

        if (IsEqualGUID(
                extensible->SubFormat,
                KSDATAFORMAT_SUBTYPE_PCM))
        {
            if (format->wBitsPerSample == 16)
            {
                output.isPCM16 = true;
                return true;
            }
        }
    }

    return false;
}


// ========================================================
// Initialize output
// ========================================================

bool initializeOutput(
    IMMDevice* device,
    AudioOutput& output)
{
    HRESULT hr;

    output.device = device;
    output.device->AddRef();

    hr = output.device->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        (void**)&output.audioClient);

    if (FAILED(hr))
    {
        std::cout
            << "AudioClient activation failed: 0x"
            << std::hex
            << hr
            << std::dec
            << "\n";

        return false;
    }

    hr = output.audioClient->GetMixFormat(
        &output.format);

    if (FAILED(hr))
    {
        std::cout
            << "GetMixFormat failed.\n";

        return false;
    }

    output.channels =
        output.format->nChannels;

    output.sampleRate =
        output.format->nSamplesPerSec;

    if (!detectAudioFormat(output))
    {
        std::cout
            << "Unsupported audio format.\n";

        return false;
    }

    std::cout
        << "Sample Rate: "
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

    if (output.isFloat)
        std::cout << "Format: FLOAT\n";

    if (output.isPCM16)
        std::cout << "Format: PCM16\n";


    // 100 ms buffer
    REFERENCE_TIME bufferDuration =
        1000000LL;

    hr = output.audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        0,
        bufferDuration,
        0,
        output.format,
        nullptr);

    if (FAILED(hr))
    {
        std::cout
            << "AudioClient Initialize failed: 0x"
            << std::hex
            << hr
            << std::dec
            << "\n";

        return false;
    }


    hr = output.audioClient->GetService(
        __uuidof(IAudioRenderClient),
        (void**)&output.renderClient);

    if (FAILED(hr))
    {
        std::cout
            << "Get RenderClient failed.\n";

        return false;
    }


    hr = output.audioClient->GetBufferSize(
        &output.bufferSize);

    if (FAILED(hr))
    {
        std::cout
            << "GetBufferSize failed.\n";

        return false;
    }

    std::cout
        << "Buffer Size: "
        << output.bufferSize
        << " frames\n";

    return true;
}


// ========================================================
// Generate audio for a timeline
//
// 0 - 2 seconds   = silence
// 2 - 3 seconds   = 440 Hz tone
// 3 - 5 seconds   = silence
// ========================================================

void generateAudio(
    AudioOutput& output,
    BYTE* data,
    UINT32 frames)
{
    const double TWO_PI =
        2.0 * 3.14159265358979323846;

    const double frequency = 440.0;

    if (output.isFloat)
    {
        float* samples =
            reinterpret_cast<float*>(data);

        for (UINT32 frame = 0;
             frame < frames;
             frame++)
        {
            double time =
                output.audioTime;

            double value = 0.0;

            // Tone from 2.0 to 12.0 seconds
            if (time >= 2.0 &&
                time < 12.0)
            {
                value =
                    0.20 *
                    sin(
                        TWO_PI *
                        frequency *
                        time);
            }

            for (UINT32 channel = 0;
                 channel < output.channels;
                 channel++)
            {
                samples[
                    frame * output.channels +
                    channel
                ] = static_cast<float>(value);
            }

            output.audioTime +=
                1.0 / output.sampleRate;
        }

        return;
    }


    if (output.isPCM16)
    {
        INT16* samples =
            reinterpret_cast<INT16*>(data);

        for (UINT32 frame = 0;
             frame < frames;
             frame++)
        {
            double time =
                output.audioTime;

            double value = 0.0;

            if (time >= 2.0 &&
                time < 3.0)
            {
                value =
                    0.20 *
                    sin(
                        TWO_PI *
                        frequency *
                        time);
            }

            INT16 sample =
                static_cast<INT16>(
                    value * 32767.0
                );

            for (UINT32 channel = 0;
                 channel < output.channels;
                 channel++)
            {
                samples[
                    frame * output.channels +
                    channel
                ] = sample;
            }

            output.audioTime +=
                1.0 / output.sampleRate;
        }
    }
}


// ========================================================
// Fill available buffer
// ========================================================

bool fillBuffer(AudioOutput& output)
{
    UINT32 padding = 0;

    HRESULT hr =
        output.audioClient->GetCurrentPadding(
            &padding);

    if (FAILED(hr))
        return false;


    UINT32 available =
        output.bufferSize - padding;


    if (available == 0)
        return true;


    BYTE* data = nullptr;


    hr =
        output.renderClient->GetBuffer(
            available,
            &data);

    if (FAILED(hr))
        return false;


    generateAudio(
        output,
        data,
        available);


    hr =
        output.renderClient->ReleaseBuffer(
            available,
            0);

    if (FAILED(hr))
        return false;


    return true;
}


// ========================================================
// Cleanup
// ========================================================

void cleanupOutput(AudioOutput& output)
{
    if (output.renderClient)
    {
        output.renderClient->Release();
        output.renderClient = nullptr;
    }

    if (output.audioClient)
    {
        output.audioClient->Release();
        output.audioClient = nullptr;
    }

    if (output.device)
    {
        output.device->Release();
        output.device = nullptr;
    }

    if (output.format)
    {
        CoTaskMemFree(output.format);
        output.format = nullptr;
    }
}


// ========================================================
// MAIN
// ========================================================

int main()
{
    HRESULT hr;


    // ----------------------------------------------------
    // COM
    // ----------------------------------------------------

    hr = CoInitialize(nullptr);

    if (FAILED(hr))
    {
        std::cout
            << "COM initialization failed.\n";

        return 1;
    }


    // ----------------------------------------------------
    // Device enumerator
    // ----------------------------------------------------

    IMMDeviceEnumerator* enumerator =
        nullptr;

    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&enumerator);

    if (FAILED(hr))
    {
        std::cout
            << "Failed to create device enumerator.\n";

        CoUninitialize();

        return 1;
    }


    // ----------------------------------------------------
    // Get active output devices
    // ----------------------------------------------------

    IMMDeviceCollection* devices =
        nullptr;

    hr =
        enumerator->EnumAudioEndpoints(
            eRender,
            DEVICE_STATE_ACTIVE,
            &devices);

    if (FAILED(hr))
    {
        std::cout
            << "Failed to enumerate devices.\n";

        enumerator->Release();
        CoUninitialize();

        return 1;
    }


    UINT deviceCount = 0;

    devices->GetCount(
        &deviceCount);


    std::cout
        << "\nAvailable audio outputs\n";

    std::cout
        << "========================\n";


    for (UINT i = 0;
         i < deviceCount;
         i++)
    {
        IMMDevice* device =
            nullptr;

        if (SUCCEEDED(
                devices->Item(
                    i,
                    &device)))
        {
            IPropertyStore* properties =
                nullptr;

            if (SUCCEEDED(
                    device->OpenPropertyStore(
                        STGM_READ,
                        &properties)))
            {
                PROPVARIANT name;

                PropVariantInit(&name);

                if (SUCCEEDED(
                        properties->GetValue(
                            PKEY_Device_FriendlyName,
                            &name)))
                {
                    std::wcout
                        << L"["
                        << i
                        << L"] "
                        << name.pwszVal
                        << L"\n";
                }

                PropVariantClear(&name);

                properties->Release();
            }

            device->Release();
        }
    }


    // ----------------------------------------------------
    // CURRENT DEVICE SELECTION
    // ----------------------------------------------------

    // Your current list:
    //
    // [0] Cobra
    // [1] Mivi
    // [2] Realtek
    //

    UINT deviceIndex1 = 0; // Cobra
    UINT deviceIndex2 = 1; // Mivi


    if (deviceIndex1 >= deviceCount ||
        deviceIndex2 >= deviceCount)
    {
        std::cout
            << "\nInvalid device indexes.\n";

        devices->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }


    // ----------------------------------------------------
    // Get devices
    // ----------------------------------------------------

    IMMDevice* device1 =
        nullptr;

    IMMDevice* device2 =
        nullptr;


    hr =
        devices->Item(
            deviceIndex1,
            &device1);

    if (FAILED(hr))
    {
        std::cout
            << "Failed to get device 1.\n";

        devices->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }


    hr =
        devices->Item(
            deviceIndex2,
            &device2);

    if (FAILED(hr))
    {
        std::cout
            << "Failed to get device 2.\n";

        device1->Release();

        devices->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }


    // ----------------------------------------------------
    // Initialize outputs
    // ----------------------------------------------------

    AudioOutput output1;
    AudioOutput output2;


    std::cout
        << "\nInitializing OUTPUT 1...\n";

    if (!initializeOutput(
            device1,
            output1))
    {
        std::cout
            << "Output 1 initialization failed.\n";

        device1->Release();
        device2->Release();

        devices->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }


    std::cout
        << "\nInitializing OUTPUT 2...\n";

    if (!initializeOutput(
            device2,
            output2))
    {
        std::cout
            << "Output 2 initialization failed.\n";

        cleanupOutput(output1);

        device1->Release();
        device2->Release();

        devices->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }


    device1->Release();
    device2->Release();


    // ----------------------------------------------------
    // Reset audio timelines
    // ----------------------------------------------------

    output1.audioTime = 0.0;
    output2.audioTime = 0.0;


    std::cout
        << "\n================================\n";

    std::cout
        << "SYNCHRONIZATION TEST\n";

    std::cout
        << "================================\n";

    std::cout
        << "\nTimeline:\n";

    std::cout
        << "0 - 2 sec : SILENCE\n";

    std::cout
        << "2 - 3 sec : 440 Hz TONE\n";

    std::cout
        << "3 - 5 sec : SILENCE\n";

    std::cout
        << "\nBoth speakers will receive the\n"
        << "tone at the same software time.\n";


    // ----------------------------------------------------
    // Pre-fill both buffers BEFORE starting
    // ----------------------------------------------------

    std::cout
        << "\nPrefilling buffers...\n";


    if (!fillBuffer(output1))
    {
        std::cout
            << "Failed to fill output 1.\n";

        cleanupOutput(output1);
        cleanupOutput(output2);

        devices->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }


    if (!fillBuffer(output2))
    {
        std::cout
            << "Failed to fill output 2.\n";

        cleanupOutput(output1);
        cleanupOutput(output2);

        devices->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }


    // ----------------------------------------------------
    // Start both
    // ----------------------------------------------------

    std::cout
        << "\nStarting both outputs...\n";


    hr =
        output1.audioClient->Start();

    if (FAILED(hr))
    {
        std::cout
            << "Output 1 Start failed.\n";

        cleanupOutput(output1);
        cleanupOutput(output2);

        devices->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }


    hr =
        output2.audioClient->Start();

    if (FAILED(hr))
    {
        std::cout
            << "Output 2 Start failed.\n";

        output1.audioClient->Stop();

        cleanupOutput(output1);
        cleanupOutput(output2);

        devices->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }


    std::cout
        << "Both outputs started.\n";


    // ----------------------------------------------------
    // Feed audio for 6 seconds
    // ----------------------------------------------------

    DWORD startTime =
        GetTickCount();


    while (
        GetTickCount() - startTime
        < 15000)
    {
        if (!fillBuffer(output1))
        {
            std::cout
                << "\nOutput 1 buffer error.\n";

            break;
        }


        if (!fillBuffer(output2))
        {
            std::cout
                << "\nOutput 2 buffer error.\n";

            break;
        }


        Sleep(2);
    }


    // ----------------------------------------------------
    // Stop
    // ----------------------------------------------------

    output1.audioClient->Stop();

    output2.audioClient->Stop();


    std::cout
        << "\n================================\n";

    std::cout
        << "TEST FINISHED\n";

    std::cout
        << "================================\n";


    cleanupOutput(output1);
    cleanupOutput(output2);

    devices->Release();
    enumerator->Release();

    CoUninitialize();

    return 0;
}