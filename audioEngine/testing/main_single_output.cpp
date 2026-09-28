#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <iostream>
#include <cmath>

#pragma comment(lib, "ole32.lib")

int main()
{
    // Initialize COM
    HRESULT hr = CoInitialize(nullptr);

    if (FAILED(hr))
    {
        std::cout << "COM initialization failed\n";
        return 1;
    }

    // Create device enumerator
    IMMDeviceEnumerator* enumerator = nullptr;

    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&enumerator
    );

    if (FAILED(hr))
    {
        std::cout << "Failed to create device enumerator\n";
        CoUninitialize();
        return 1;
    }

    // Get active output devices
    IMMDeviceCollection* devices = nullptr;

    hr = enumerator->EnumAudioEndpoints(
        eRender,
        DEVICE_STATE_ACTIVE,
        &devices
    );

    if (FAILED(hr))
    {
        std::cout << "Failed to enumerate audio devices\n";
        enumerator->Release();
        CoUninitialize();
        return 1;
    }

    UINT count = 0;
    devices->GetCount(&count);

    std::cout << "Available audio outputs:\n\n";

    for (UINT i = 0; i < count; i++)
    {
        std::cout << "[" << i << "] Output device\n";
    }

    std::cout << "\n";

    // --------------------------------------------------
    // SELECT DEVICE
    // --------------------------------------------------

    // Currently selecting device index 1
    // Change this if your Mivi speaker has another index.
    UINT deviceIndex = 1;

    if (deviceIndex >= count)
    {
        std::cout << "Invalid device index\n";

        devices->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }

    IMMDevice* device = nullptr;

    hr = devices->Item(deviceIndex, &device);

    if (FAILED(hr))
    {
        std::cout << "Failed to get selected device\n";

        devices->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }

    // --------------------------------------------------
    // ACTIVATE AUDIO CLIENT
    // --------------------------------------------------

    IAudioClient* audioClient = nullptr;

    hr = device->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        (void**)&audioClient
    );

    if (FAILED(hr))
    {
        std::cout << "Failed to activate audio client\n";

        device->Release();
        devices->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }

    // Get device mix format
    WAVEFORMATEX* format = nullptr;

    hr = audioClient->GetMixFormat(&format);

    if (FAILED(hr))
    {
        std::cout << "Failed to get audio format\n";

        audioClient->Release();
        device->Release();
        devices->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }

    std::cout << "Audio format:\n";
    std::cout << "Channels: " << format->nChannels << "\n";
    std::cout << "Sample Rate: " << format->nSamplesPerSec << "\n";
    std::cout << "Bits: " << format->wBitsPerSample << "\n\n";

    // --------------------------------------------------
    // INITIALIZE WASAPI
    // --------------------------------------------------

    REFERENCE_TIME bufferDuration = 2 * 10000000LL; // 2 seconds

    hr = audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        0,
        bufferDuration,
        0,
        format,
        nullptr
    );

    if (FAILED(hr))
    {
        std::cout << "AudioClient initialization failed\n";

        CoTaskMemFree(format);
        audioClient->Release();
        device->Release();
        devices->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }

    // Get render client
    IAudioRenderClient* renderClient = nullptr;

    hr = audioClient->GetService(
        __uuidof(IAudioRenderClient),
        (void**)&renderClient
    );

    if (FAILED(hr))
    {
        std::cout << "Failed to get render client\n";

        CoTaskMemFree(format);
        audioClient->Release();
        device->Release();
        devices->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }

    UINT32 bufferSize = 0;

    audioClient->GetBufferSize(&bufferSize);

    std::cout << "Buffer size: " << bufferSize << " frames\n";
    std::cout << "Playing 440 Hz test tone...\n";

    // --------------------------------------------------
    // START AUDIO
    // --------------------------------------------------

    hr = audioClient->Start();

    if (FAILED(hr))
    {
        std::cout << "Failed to start audio\n";

        renderClient->Release();
        CoTaskMemFree(format);
        audioClient->Release();
        device->Release();
        devices->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }

    // --------------------------------------------------
    // GENERATE SINE WAVE
    // --------------------------------------------------

    double phase = 0.0;

    const double frequency = 440.0;
    const double sampleRate = format->nSamplesPerSec;

    DWORD startTime = GetTickCount();

    while (GetTickCount() - startTime < 5000)
    {
        UINT32 padding = 0;

        hr = audioClient->GetCurrentPadding(&padding);

        if (FAILED(hr))
            break;

        UINT32 availableFrames = bufferSize - padding;

        if (availableFrames == 0)
        {
            Sleep(5);
            continue;
        }

        BYTE* data = nullptr;

        hr = renderClient->GetBuffer(
            availableFrames,
            &data
        );

        if (FAILED(hr))
            break;

        // This assumes 32-bit float audio,
        // which is common for WASAPI shared mode.
        float* samples = reinterpret_cast<float*>(data);

        for (UINT32 frame = 0; frame < availableFrames; frame++)
        {
            float value =
                static_cast<float>(
                    0.15 * sin(phase)
                );

            for (UINT32 channel = 0;
                 channel < format->nChannels;
                 channel++)
            {
                samples[frame * format->nChannels + channel] = value;
            }

            phase += 2.0 * 3.14159265358979323846 *
                     frequency / sampleRate;

            if (phase >= 2.0 * 3.14159265358979323846)
            {
                phase -= 2.0 * 3.14159265358979323846;
            }
        }

        renderClient->ReleaseBuffer(
            availableFrames,
            0
        );

        Sleep(5);
    }

    // --------------------------------------------------
    // STOP
    // --------------------------------------------------

    audioClient->Stop();

    std::cout << "Test finished.\n";

    // Cleanup
    renderClient->Release();
    CoTaskMemFree(format);
    audioClient->Release();
    device->Release();
    devices->Release();
    enumerator->Release();

    CoUninitialize();

    return 0;
}