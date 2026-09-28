#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <ksmedia.h>
#include <functiondiscoverykeys_devpkey.h>
#include <iostream>
#include <cmath>

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
};


// ---------------------------------------------------------
// Detect WASAPI format
// ---------------------------------------------------------

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


// ---------------------------------------------------------
// Initialize output
// ---------------------------------------------------------

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
            << "Failed to activate AudioClient. HRESULT: 0x"
            << std::hex << hr << std::dec << "\n";

        return false;
    }

    hr = output.audioClient->GetMixFormat(
        &output.format);

    if (FAILED(hr))
    {
        std::cout << "Failed to get mix format.\n";
        return false;
    }

    output.channels =
        output.format->nChannels;

    output.sampleRate =
        output.format->nSamplesPerSec;

    if (!detectAudioFormat(output))
    {
        std::cout << "Unsupported audio format.\n";

        std::cout
            << "Format tag: "
            << output.format->wFormatTag
            << "\n";

        std::cout
            << "Bits: "
            << output.format->wBitsPerSample
            << "\n";

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


    // 100 ms WASAPI buffer
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
            << "Failed to initialize AudioClient. HRESULT: 0x"
            << std::hex << hr << std::dec
            << "\n";

        return false;
    }


    hr = output.audioClient->GetService(
        __uuidof(IAudioRenderClient),
        (void**)&output.renderClient);

    if (FAILED(hr))
    {
        std::cout
            << "Failed to get RenderClient.\n";

        return false;
    }


    hr = output.audioClient->GetBufferSize(
        &output.bufferSize);

    if (FAILED(hr))
    {
        std::cout
            << "Failed to get buffer size.\n";

        return false;
    }


    std::cout
        << "Buffer Size: "
        << output.bufferSize
        << " frames\n";

    return true;
}


// ---------------------------------------------------------
// Write silence
// ---------------------------------------------------------

void generateSilence(
    AudioOutput& output,
    BYTE* data,
    UINT32 frames)
{
    if (output.isFloat)
    {
        float* samples =
            reinterpret_cast<float*>(data);

        UINT32 totalSamples =
            frames * output.channels;

        for (UINT32 i = 0;
             i < totalSamples;
             i++)
        {
            samples[i] = 0.0f;
        }
    }

    else if (output.isPCM16)
    {
        INT16* samples =
            reinterpret_cast<INT16*>(data);

        UINT32 totalSamples =
            frames * output.channels;

        for (UINT32 i = 0;
             i < totalSamples;
             i++)
        {
            samples[i] = 0;
        }
    }
}


// ---------------------------------------------------------
// Generate pulse
// ---------------------------------------------------------

void generatePulse(
    AudioOutput& output,
    BYTE* data,
    UINT32 frames,
    double startTime,
    double duration)
{
    const double TWO_PI =
        2.0 * 3.14159265358979323846;

    const double frequency = 440.0;

    for (UINT32 frame = 0;
         frame < frames;
         frame++)
    {
        double currentTime =
            static_cast<double>(frame)
            / output.sampleRate;

        double envelope = 0.0;

        if (currentTime >= startTime &&
            currentTime < startTime + duration)
        {
            envelope = 0.20;
        }

        double value =
            envelope *
            sin(
                TWO_PI *
                frequency *
                currentTime
            );


        if (output.isFloat)
        {
            float* samples =
                reinterpret_cast<float*>(data);

            for (UINT32 channel = 0;
                 channel < output.channels;
                 channel++)
            {
                samples[
                    frame * output.channels +
                    channel
                ] = static_cast<float>(value);
            }
        }

        else if (output.isPCM16)
        {
            INT16* samples =
                reinterpret_cast<INT16*>(data);

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
        }
    }
}


// ---------------------------------------------------------
// Cleanup
// ---------------------------------------------------------

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


// ---------------------------------------------------------
// MAIN
// ---------------------------------------------------------

int main()
{
    HRESULT hr;

    // -----------------------------------------------------
    // COM
    // -----------------------------------------------------

    hr = CoInitialize(nullptr);

    if (FAILED(hr))
    {
        std::cout
            << "COM initialization failed.\n";

        return 1;
    }


    // -----------------------------------------------------
    // Device enumerator
    // -----------------------------------------------------

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


    // -----------------------------------------------------
    // Get devices
    // -----------------------------------------------------

    IMMDeviceCollection* devices =
        nullptr;

    hr = enumerator->EnumAudioEndpoints(
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
        << "========================\n\n";


    for (UINT i = 0;
         i < deviceCount;
         i++)
    {
        IMMDevice* device =
            nullptr;

        if (SUCCEEDED(
                devices->Item(i, &device)))
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


    // -----------------------------------------------------
    // SELECT YOUR TWO SPEAKERS
    // -----------------------------------------------------

    UINT deviceIndex1 = 0;
    UINT deviceIndex2 = 1;


    if (deviceIndex1 >= deviceCount ||
        deviceIndex2 >= deviceCount)
    {
        std::cout
            << "Invalid device indexes.\n";

        devices->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }


    IMMDevice* device1 =
        nullptr;

    IMMDevice* device2 =
        nullptr;


    hr = devices->Item(
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


    hr = devices->Item(
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


    // -----------------------------------------------------
    // Initialize both
    // -----------------------------------------------------

    AudioOutput output1;
    AudioOutput output2;


    std::cout
        << "\nInitializing OUTPUT 1...\n";

    if (!initializeOutput(
            device1,
            output1))
    {
        std::cout
            << "Output 1 failed.\n";

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
            << "Output 2 failed.\n";

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


    // -----------------------------------------------------
    // Prepare full buffers
    // -----------------------------------------------------

    std::cout
        << "\nPreparing synchronization pulse...\n";


    // OUTPUT 1

    UINT32 padding1 = 0;

    output1.audioClient
        ->GetCurrentPadding(
            &padding1);

    UINT32 frames1 =
        output1.bufferSize - padding1;

    BYTE* data1 =
        nullptr;

    hr = output1.renderClient->GetBuffer(
        frames1,
        &data1);

    if (FAILED(hr))
    {
        std::cout
            << "Output 1 GetBuffer failed.\n";

        cleanupOutput(output1);
        cleanupOutput(output2);

        devices->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }


    // IMPORTANT:
    // The pulse occurs at 2 seconds.
    // Our buffer is only ~100 ms, so this section
    // will initially contain silence.

    generateSilence(
        output1,
        data1,
        frames1);


    output1.renderClient->ReleaseBuffer(
        frames1,
        0);


    // OUTPUT 2

    UINT32 padding2 = 0;

    output2.audioClient
        ->GetCurrentPadding(
            &padding2);

    UINT32 frames2 =
        output2.bufferSize - padding2;

    BYTE* data2 =
        nullptr;

    hr = output2.renderClient->GetBuffer(
        frames2,
        &data2);

    if (FAILED(hr))
    {
        std::cout
            << "Output 2 GetBuffer failed.\n";

        cleanupOutput(output1);
        cleanupOutput(output2);

        devices->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }


    generateSilence(
        output2,
        data2,
        frames2);


    output2.renderClient->ReleaseBuffer(
        frames2,
        0);


    // -----------------------------------------------------
    // Start both at exactly the same code point
    // -----------------------------------------------------

    std::cout
        << "\nStarting both outputs...\n";


    output1.audioClient->Start();

    output2.audioClient->Start();


    std::cout
        << "Both outputs started.\n";

    std::cout
        << "Waiting 2 seconds...\n";


    // -----------------------------------------------------
    // Wait 2 seconds
    // -----------------------------------------------------

    Sleep(2000);


    // -----------------------------------------------------
    // Send pulse to BOTH outputs
    // -----------------------------------------------------

    std::cout
        << "Sending synchronization pulse!\n";


    // Create pulse separately for each
    // device because their sample rates differ.

    UINT32 pulseFrames1 =
        static_cast<UINT32>(
            output1.sampleRate * 0.15);

    UINT32 pulseFrames2 =
        static_cast<UINT32>(
            output2.sampleRate * 0.15);


    BYTE* pulseBuffer1 =
        nullptr;

    BYTE* pulseBuffer2 =
        nullptr;


    // -----------------------------------------------------
    // Output 1 pulse
    // -----------------------------------------------------

    while (true)
    {
        UINT32 padding = 0;

        output1.audioClient
            ->GetCurrentPadding(
                &padding);

        UINT32 available =
            output1.bufferSize - padding;

        if (available >= pulseFrames1)
            break;

        Sleep(2);
    }


    output1.renderClient->GetBuffer(
        pulseFrames1,
        &pulseBuffer1);


    generatePulse(
        output1,
        pulseBuffer1,
        pulseFrames1,
        0.0,
        0.15);


    output1.renderClient->ReleaseBuffer(
        pulseFrames1,
        0);


    // -----------------------------------------------------
    // Output 2 pulse
    // -----------------------------------------------------

    while (true)
    {
        UINT32 padding = 0;

        output2.audioClient
            ->GetCurrentPadding(
                &padding);

        UINT32 available =
            output2.bufferSize - padding;

        if (available >= pulseFrames2)
            break;

        Sleep(2);
    }


    output2.renderClient->GetBuffer(
        pulseFrames2,
        &pulseBuffer2);


    generatePulse(
        output2,
        pulseBuffer2,
        pulseFrames2,
        0.0,
        0.15);


    output2.renderClient->ReleaseBuffer(
        pulseFrames2,
        0);


    std::cout
        << "\nPulse sent.\n";

    std::cout
        << "Listen carefully to the two speakers.\n";

    std::cout
        << "Measure the gap between the two beeps.\n";


    // -----------------------------------------------------
    // Keep streams alive
    // -----------------------------------------------------

    Sleep(3000);


    // -----------------------------------------------------
    // Stop
    // -----------------------------------------------------

    output1.audioClient->Stop();

    output2.audioClient->Stop();


    std::cout
        << "\nTest finished.\n";


    cleanupOutput(output1);
    cleanupOutput(output2);

    devices->Release();
    enumerator->Release();

    CoUninitialize();

    return 0;
}