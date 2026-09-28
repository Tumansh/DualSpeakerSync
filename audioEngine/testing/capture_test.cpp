#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <iostream>
#include <iomanip>

#pragma comment(lib, "ole32.lib")

int main()
{
    HRESULT hr;

    // =====================================================
    // Initialize COM
    // =====================================================

    hr = CoInitialize(nullptr);

    if (FAILED(hr))
    {
        std::cout
            << "COM initialization failed. HRESULT: 0x"
            << std::hex << hr << std::dec
            << "\n";

        return 1;
    }


    // =====================================================
    // Create device enumerator
    // =====================================================

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
        std::cout
            << "Failed to create device enumerator.\n";

        CoUninitialize();
        return 1;
    }


    // =====================================================
    // Get DEFAULT playback device
    // =====================================================

    IMMDevice* device = nullptr;

    hr = enumerator->GetDefaultAudioEndpoint(
        eRender,
        eConsole,
        &device
    );

    if (FAILED(hr))
    {
        std::cout
            << "Failed to get default playback device.\n";

        enumerator->Release();
        CoUninitialize();

        return 1;
    }


    // =====================================================
    // Print device name
    // =====================================================

    IPropertyStore* properties = nullptr;

    hr = device->OpenPropertyStore(
        STGM_READ,
        &properties
    );

    if (SUCCEEDED(hr))
    {
        PROPVARIANT name;

        PropVariantInit(&name);

        if (SUCCEEDED(
            properties->GetValue(
                PKEY_Device_FriendlyName,
                &name)))
        {
            std::wcout
                << L"Capture device: "
                << name.pwszVal
                << L"\n";
        }

        PropVariantClear(&name);

        properties->Release();
    }


    // =====================================================
    // Activate AudioClient
    // =====================================================

    IAudioClient* audioClient = nullptr;

    hr = device->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        (void**)&audioClient
    );

    if (FAILED(hr))
    {
        std::cout
            << "Failed to activate AudioClient.\n";

        device->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }


    // =====================================================
    // Get mix format
    // =====================================================

    WAVEFORMATEX* format = nullptr;

    hr = audioClient->GetMixFormat(
        &format
    );

    if (FAILED(hr))
    {
        std::cout
            << "Failed to get mix format.\n";

        audioClient->Release();
        device->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }


    std::cout
        << "\nCapture format\n";

    std::cout
        << "==============\n";

    std::cout
        << "Sample rate: "
        << format->nSamplesPerSec
        << " Hz\n";

    std::cout
        << "Channels: "
        << format->nChannels
        << "\n";

    std::cout
        << "Bits: "
        << format->wBitsPerSample
        << "\n";


    // =====================================================
    // Initialize LOOPBACK capture
    // =====================================================

    REFERENCE_TIME bufferDuration =
        10000000LL; // 1 second

    hr = audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,

        // IMPORTANT:
        // Capture audio that Windows is rendering.
        AUDCLNT_STREAMFLAGS_LOOPBACK,

        bufferDuration,

        0,

        format,

        nullptr
    );

    if (FAILED(hr))
    {
        std::cout
            << "\nAudioClient Initialize failed.\n";

        std::cout
            << "HRESULT: 0x"
            << std::hex
            << hr
            << std::dec
            << "\n";

        CoTaskMemFree(format);

        audioClient->Release();
        device->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }


    // =====================================================
    // Get CaptureClient
    // =====================================================

    IAudioCaptureClient* captureClient = nullptr;

    hr = audioClient->GetService(
        __uuidof(IAudioCaptureClient),
        (void**)&captureClient
    );

    if (FAILED(hr))
    {
        std::cout
            << "Failed to get IAudioCaptureClient.\n";

        CoTaskMemFree(format);

        audioClient->Release();
        device->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }


    // =====================================================
    // Start capture
    // =====================================================

    hr = audioClient->Start();

    if (FAILED(hr))
    {
        std::cout
            << "Failed to start capture.\n";

        captureClient->Release();

        CoTaskMemFree(format);

        audioClient->Release();
        device->Release();
        enumerator->Release();
        CoUninitialize();

        return 1;
    }


    std::cout
        << "\n================================\n";

    std::cout
        << "WASAPI LOOPBACK CAPTURE STARTED\n";

    std::cout
        << "================================\n";

    std::cout
        << "\nPlay some audio on Windows now.\n";

    std::cout
        << "YouTube / Spotify / music / etc.\n";

    std::cout
        << "\nCapturing for 15 seconds...\n\n";


    // =====================================================
    // Capture loop
    // =====================================================

    DWORD startTime =
        GetTickCount();


    UINT64 totalFrames = 0;

    UINT64 packetCount = 0;


    while (
        GetTickCount() - startTime
        < 15000)
    {
        UINT32 packetLength = 0;

        hr =
            captureClient->GetNextPacketSize(
                &packetLength
            );

        if (FAILED(hr))
        {
            std::cout
                << "GetNextPacketSize failed.\n";

            break;
        }


        if (packetLength == 0)
        {
            Sleep(5);
            continue;
        }


        while (packetLength > 0)
        {
            BYTE* data = nullptr;

            UINT32 frames = 0;

            DWORD flags = 0;

            UINT64 devicePosition = 0;

            UINT64 qpcPosition = 0;


            hr =
                captureClient->GetBuffer(
                    &data,
                    &frames,
                    &flags,
                    &devicePosition,
                    &qpcPosition
                );


            if (FAILED(hr))
            {
                std::cout
                    << "GetBuffer failed.\n";

                break;
            }


            packetCount++;

            totalFrames += frames;


            std::cout
                << "Captured: "
                << std::setw(5)
                << frames
                << " frames | "
                << "Total: "
                << std::setw(8)
                << totalFrames
                << " frames";


            if (flags &
                AUDCLNT_BUFFERFLAGS_SILENT)
            {
                std::cout
                    << " | SILENT";
            }
            else
            {
                std::cout
                    << " | AUDIO";
            }


            std::cout << "\n";


            hr =
                captureClient->ReleaseBuffer(
                    frames
                );

            if (FAILED(hr))
            {
                std::cout
                    << "ReleaseBuffer failed.\n";

                break;
            }


            hr =
                captureClient->GetNextPacketSize(
                    &packetLength
                );

            if (FAILED(hr))
            {
                std::cout
                    << "GetNextPacketSize failed.\n";

                break;
            }
        }
    }


    // =====================================================
    // Stop
    // =====================================================

    audioClient->Stop();


    std::cout
        << "\n================================\n";

    std::cout
        << "CAPTURE FINISHED\n";

    std::cout
        << "================================\n";

    std::cout
        << "Packets captured: "
        << packetCount
        << "\n";

    std::cout
        << "Total frames: "
        << totalFrames
        << "\n";


    // =====================================================
    // Cleanup
    // =====================================================

    captureClient->Release();

    CoTaskMemFree(format);

    audioClient->Release();

    device->Release();

    enumerator->Release();

    CoUninitialize();

    return 0;
}
