#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>

#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>

#pragma comment(lib, "ole32.lib")


// ============================================================
// Audio output
// ============================================================

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


// ============================================================
// Detect format
// ============================================================

bool detectFormat(AudioOutput& output)
{
    WAVEFORMATEX* format = output.format;


    if (format->wFormatTag ==
        WAVE_FORMAT_IEEE_FLOAT)
    {
        output.isFloat = true;

        return true;
    }


    if (format->wFormatTag ==
        WAVE_FORMAT_PCM)
    {
        if (format->wBitsPerSample == 16)
        {
            output.isPCM16 = true;

            return true;
        }
    }


    if (format->wFormatTag ==
        WAVE_FORMAT_EXTENSIBLE)
    {
        WAVEFORMATEXTENSIBLE* extensible =
            reinterpret_cast<WAVEFORMATEXTENSIBLE*>(
                format
            );


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


// ============================================================
// Initialize output
// ============================================================

bool initializeOutput(
    IMMDevice* device,
    AudioOutput& output)
{
    HRESULT hr;


    output.device = device;

    output.device->AddRef();


    // --------------------------------------------------------
    // Activate AudioClient
    // --------------------------------------------------------

    hr =
        output.device->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            (void**)&output.audioClient
        );


    if (FAILED(hr))
    {
        std::cout
            << "Failed to activate AudioClient: 0x"
            << std::hex
            << hr
            << std::dec
            << "\n";

        return false;
    }


    // --------------------------------------------------------
    // Get format
    // --------------------------------------------------------

    hr =
        output.audioClient->GetMixFormat(
            &output.format
        );


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


    if (!detectFormat(output))
    {
        std::cout
            << "Unsupported output format.\n";

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
    {
        std::cout
            << "Format: FLOAT\n";
    }


    if (output.isPCM16)
    {
        std::cout
            << "Format: PCM16\n";
    }


    // --------------------------------------------------------
    // Initialize WASAPI
    // --------------------------------------------------------

    REFERENCE_TIME bufferDuration =
        1000000LL; // 100 ms


    hr =
        output.audioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            0,
            bufferDuration,
            0,
            output.format,
            nullptr
        );


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


    // --------------------------------------------------------
    // Render client
    // --------------------------------------------------------

    hr =
        output.audioClient->GetService(
            __uuidof(IAudioRenderClient),
            (void**)&output.renderClient
        );


    if (FAILED(hr))
    {
        std::cout
            << "Get RenderClient failed.\n";

        return false;
    }


    // --------------------------------------------------------
    // Buffer size
    // --------------------------------------------------------

    hr =
        output.audioClient->GetBufferSize(
            &output.bufferSize
        );


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


// ============================================================
// Cleanup output
// ============================================================

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


// ============================================================
// Capture format information
// ============================================================

struct CaptureInfo
{
    WAVEFORMATEX* format = nullptr;

    UINT32 channels = 0;

    UINT32 bits = 0;

    double sampleRate = 0.0;

    bool isFloat = false;

    bool isPCM16 = false;
};


// ============================================================
// Detect capture format
// ============================================================

bool detectCaptureFormat(
    CaptureInfo& info)
{
    WAVEFORMATEX* format =
        info.format;


    if (format->wFormatTag ==
        WAVE_FORMAT_IEEE_FLOAT)
    {
        info.isFloat = true;

        return true;
    }


    if (format->wFormatTag ==
        WAVE_FORMAT_PCM)
    {
        if (format->wBitsPerSample == 16)
        {
            info.isPCM16 = true;

            return true;
        }
    }


    if (format->wFormatTag ==
        WAVE_FORMAT_EXTENSIBLE)
    {
        WAVEFORMATEXTENSIBLE* extensible =
            reinterpret_cast<WAVEFORMATEXTENSIBLE*>(
                format
            );


        if (IsEqualGUID(
                extensible->SubFormat,
                KSDATAFORMAT_SUBTYPE_IEEE_FLOAT))
        {
            info.isFloat = true;

            return true;
        }


        if (IsEqualGUID(
                extensible->SubFormat,
                KSDATAFORMAT_SUBTYPE_PCM))
        {
            if (format->wBitsPerSample == 16)
            {
                info.isPCM16 = true;

                return true;
            }
        }
    }


    return false;
}


// ============================================================
// Copy captured audio to output
//
// FIRST VERSION:
//
// We expect capture and outputs to all be:
// 44.1 kHz
// stereo
// float
//
// If they aren't, we stop instead of silently producing
// incorrect audio.
//
// ============================================================

bool writeToOutput(
    AudioOutput& output,
    const BYTE* source,
    UINT32 sourceFrames,
    UINT32 sourceChannels,
    bool sourceFloat,
    bool sourcePCM16)
{
    if (sourceFrames == 0)
        return true;


    // --------------------------------------------------------
    // For this first experiment, require same format
    // --------------------------------------------------------

    if (output.channels != sourceChannels)
    {
        std::cout
            << "\nChannel mismatch.\n";

        return false;
    }


    if (output.isFloat != sourceFloat ||
        output.isPCM16 != sourcePCM16)
    {
        std::cout
            << "\nFormat mismatch.\n";

        return false;
    }


    // --------------------------------------------------------
    // Check available WASAPI space
    // --------------------------------------------------------

    UINT32 padding = 0;


    HRESULT hr =
        output.audioClient->GetCurrentPadding(
            &padding
        );


    if (FAILED(hr))
        return false;


    UINT32 available =
        output.bufferSize - padding;


    if (available < sourceFrames)
    {
        return false;
    }


    // --------------------------------------------------------
    // Get output buffer
    // --------------------------------------------------------

    BYTE* destination = nullptr;


    hr =
        output.renderClient->GetBuffer(
            sourceFrames,
            &destination
        );


    if (FAILED(hr))
        return false;


    // --------------------------------------------------------
    // Copy PCM
    // --------------------------------------------------------

    UINT32 bytesPerFrame =
        output.format->nBlockAlign;


    UINT32 totalBytes =
        sourceFrames * bytesPerFrame;


    memcpy(
        destination,
        source,
        totalBytes
    );


    // --------------------------------------------------------
    // Release
    // --------------------------------------------------------

    hr =
        output.renderClient->ReleaseBuffer(
            sourceFrames,
            0
        );


    if (FAILED(hr))
        return false;


    return true;
}


// ============================================================
// MAIN
// ============================================================

int main()
{
    HRESULT hr;


    // ========================================================
    // COM
    // ========================================================

    hr =
        CoInitialize(nullptr);


    if (FAILED(hr))
    {
        std::cout
            << "COM initialization failed.\n";

        return 1;
    }


    // ========================================================
    // Device enumerator
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
        std::cout
            << "Failed to create device enumerator.\n";

        CoUninitialize();

        return 1;
    }


    // ========================================================
    // Get all output devices
    // ========================================================

    IMMDeviceCollection* devices =
        nullptr;


    hr =
        enumerator->EnumAudioEndpoints(
            eRender,
            DEVICE_STATE_ACTIVE,
            &devices
        );


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
        &deviceCount
    );


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


                PropVariantClear(
                    &name
                );


                properties->Release();
            }


            device->Release();
        }
    }


    // ========================================================
    // Select Cobra + Mivi
    // ========================================================

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


    devices->Item(
        deviceIndex1,
        &device1
    );


    devices->Item(
        deviceIndex2,
        &device2
    );


    // ========================================================
    // Initialize outputs
    // ========================================================

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


    // ========================================================
    // Capture default output
    // ========================================================

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
        std::cout
            << "Failed to get default output.\n";

        cleanupOutput(output1);

        cleanupOutput(output2);

        devices->Release();

        enumerator->Release();

        CoUninitialize();

        return 1;
    }


    // ========================================================
    // Activate capture AudioClient
    // ========================================================

    IAudioClient* captureAudioClient =
        nullptr;


    hr =
        captureDevice->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            (void**)&captureAudioClient
        );


    if (FAILED(hr))
    {
        std::cout
            << "Failed to activate capture client.\n";

        captureDevice->Release();

        cleanupOutput(output1);

        cleanupOutput(output2);

        devices->Release();

        enumerator->Release();

        CoUninitialize();

        return 1;
    }


    // ========================================================
    // Capture format
    // ========================================================

    CaptureInfo captureInfo;


    hr =
        captureAudioClient->GetMixFormat(
            &captureInfo.format
        );


    if (FAILED(hr))
    {
        std::cout
            << "Failed to get capture format.\n";

        captureAudioClient->Release();

        captureDevice->Release();

        cleanupOutput(output1);

        cleanupOutput(output2);

        devices->Release();

        enumerator->Release();

        CoUninitialize();

        return 1;
    }


    captureInfo.channels =
        captureInfo.format->nChannels;


    captureInfo.bits =
        captureInfo.format->wBitsPerSample;


    captureInfo.sampleRate =
        captureInfo.format->nSamplesPerSec;


    if (!detectCaptureFormat(
            captureInfo))
    {
        std::cout
            << "Unsupported capture format.\n";

        CoTaskMemFree(
            captureInfo.format
        );

        captureAudioClient->Release();

        captureDevice->Release();

        cleanupOutput(output1);

        cleanupOutput(output2);

        devices->Release();

        enumerator->Release();

        CoUninitialize();

        return 1;
    }


    std::cout
        << "\nCapture format\n";

    std::cout
        << "==============\n";

    std::cout
        << "Sample Rate: "
        << captureInfo.sampleRate
        << "\n";

    std::cout
        << "Channels: "
        << captureInfo.channels
        << "\n";

    std::cout
        << "Bits: "
        << captureInfo.bits
        << "\n";


    // ========================================================
    // IMPORTANT FORMAT CHECK
    // ========================================================

    bool compatible =
        captureInfo.channels ==
            output1.channels &&

        captureInfo.channels ==
            output2.channels &&

        captureInfo.sampleRate ==
            output1.sampleRate &&

        captureInfo.sampleRate ==
            output2.sampleRate &&

        captureInfo.isFloat ==
            output1.isFloat &&

        captureInfo.isFloat ==
            output2.isFloat;


    if (!compatible)
    {
        std::cout
            << "\n================================\n";

        std::cout
            << "FORMAT MISMATCH\n";

        std::cout
            << "================================\n";

        std::cout
            << "\nThis first test requires:\n";

        std::cout
            << "Capture = Cobra = Mivi\n";

        std::cout
            << "same sample rate, channels and format.\n";

        std::cout
            << "\nWe will add a resampler later.\n";

        CoTaskMemFree(
            captureInfo.format
        );

        captureAudioClient->Release();

        captureDevice->Release();

        cleanupOutput(output1);

        cleanupOutput(output2);

        devices->Release();

        enumerator->Release();

        CoUninitialize();

        return 1;
    }


    // ========================================================
    // Initialize loopback
    // ========================================================

    REFERENCE_TIME bufferDuration =
        10000000LL; // 1 sec


    hr =
        captureAudioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK,
            bufferDuration,
            0,
            captureInfo.format,
            nullptr
        );


    if (FAILED(hr))
    {
        std::cout
            << "\nLoopback Initialize failed: 0x"
            << std::hex
            << hr
            << std::dec
            << "\n";

        CoTaskMemFree(
            captureInfo.format
        );

        captureAudioClient->Release();

        captureDevice->Release();

        cleanupOutput(output1);

        cleanupOutput(output2);

        devices->Release();

        enumerator->Release();

        CoUninitialize();

        return 1;
    }


    // ========================================================
    // Capture service
    // ========================================================

    IAudioCaptureClient* captureClient =
        nullptr;


    hr =
        captureAudioClient->GetService(
            __uuidof(IAudioCaptureClient),
            (void**)&captureClient
        );


    if (FAILED(hr))
    {
        std::cout
            << "Failed to get capture service.\n";

        CoTaskMemFree(
            captureInfo.format
        );

        captureAudioClient->Release();

        captureDevice->Release();

        cleanupOutput(output1);

        cleanupOutput(output2);

        devices->Release();

        enumerator->Release();

        CoUninitialize();

        return 1;
    }


    // ========================================================
    // Start output clients first
    // ========================================================

    std::cout
        << "\nStarting Cobra...\n";


    hr =
        output1.audioClient->Start();


    if (FAILED(hr))
    {
        std::cout
            << "Cobra Start failed.\n";

        captureClient->Release();

        CoTaskMemFree(
            captureInfo.format
        );

        captureAudioClient->Release();

        captureDevice->Release();

        cleanupOutput(output1);

        cleanupOutput(output2);

        devices->Release();

        enumerator->Release();

        CoUninitialize();

        return 1;
    }


    std::cout
        << "Starting Mivi...\n";


    hr =
        output2.audioClient->Start();


    if (FAILED(hr))
    {
        std::cout
            << "Mivi Start failed.\n";

        output1.audioClient->Stop();

        captureClient->Release();

        CoTaskMemFree(
            captureInfo.format
        );

        captureAudioClient->Release();

        captureDevice->Release();

        cleanupOutput(output1);

        cleanupOutput(output2);

        devices->Release();

        enumerator->Release();

        CoUninitialize();

        return 1;
    }


    // ========================================================
    // Start loopback capture
    // ========================================================

    hr =
        captureAudioClient->Start();


    if (FAILED(hr))
    {
        std::cout
            << "Loopback Start failed.\n";

        output1.audioClient->Stop();

        output2.audioClient->Stop();

        captureClient->Release();

        CoTaskMemFree(
            captureInfo.format
        );

        captureAudioClient->Release();

        captureDevice->Release();

        cleanupOutput(output1);

        cleanupOutput(output2);

        devices->Release();

        enumerator->Release();

        CoUninitialize();

        return 1;
    }


    // ========================================================
    // Running
    // ========================================================

    std::cout
        << "\n========================================\n";

    std::cout
        << "MULTI SPEAKER AUDIO TEST\n";

    std::cout
        << "========================================\n";

    std::cout
        << "\nWindows audio -> Loopback -> Cobra + Mivi\n";

    std::cout
        << "\nPlay YouTube / Spotify / music now.\n";

    std::cout
        << "Running for 30 seconds...\n\n";


    DWORD startTime =
        GetTickCount();


    UINT64 totalFrames =
        0;


    // ========================================================
    // Main capture/render loop
    // ========================================================

    while (
        GetTickCount() - startTime
        < 30000)
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
            Sleep(2);

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


            totalFrames += frames;


            // ------------------------------------------------
            // If Windows reports silence
            // ------------------------------------------------

            if (flags &
                AUDCLNT_BUFFERFLAGS_SILENT)
            {
                // Send zeroed audio to both outputs.

                UINT32 bytesPerFrame =
                    output1.format->nBlockAlign;


                UINT32 totalBytes =
                    frames *
                    bytesPerFrame;


                std::vector<BYTE>
                    silence(
                        totalBytes,
                        0
                    );


                writeToOutput(
                    output1,
                    silence.data(),
                    frames,
                    captureInfo.channels,
                    captureInfo.isFloat,
                    captureInfo.isPCM16
                );


                writeToOutput(
                    output2,
                    silence.data(),
                    frames,
                    captureInfo.channels,
                    captureInfo.isFloat,
                    captureInfo.isPCM16
                );
            }
            else
            {
                // ------------------------------------------------
                // Send same captured audio to both speakers
                // ------------------------------------------------

                bool result1 =
                    writeToOutput(
                        output1,
                        data,
                        frames,
                        captureInfo.channels,
                        captureInfo.isFloat,
                        captureInfo.isPCM16
                    );


                bool result2 =
                    writeToOutput(
                        output2,
                        data,
                        frames,
                        captureInfo.channels,
                        captureInfo.isFloat,
                        captureInfo.isPCM16
                    );


                if (!result1 ||
                    !result2)
                {
                    std::cout
                        << "\nOutput buffer could not "
                        << "accept captured audio.\n";
                }
            }


            // ------------------------------------------------
            // Release capture packet
            // ------------------------------------------------

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


            // ------------------------------------------------
            // Next packet
            // ------------------------------------------------

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


    // ========================================================
    // Stop everything
    // ========================================================

    captureAudioClient->Stop();

    output1.audioClient->Stop();

    output2.audioClient->Stop();


    std::cout
        << "\n========================================\n";

    std::cout
        << "TEST FINISHED\n";

    std::cout
        << "========================================\n";

    std::cout
        << "Total captured frames: "
        << totalFrames
        << "\n";


    // ========================================================
    // Cleanup
    // ========================================================

    captureClient->Release();

    CoTaskMemFree(
        captureInfo.format
    );

    captureAudioClient->Release();

    captureDevice->Release();

    cleanupOutput(output1);

    cleanupOutput(output2);

    devices->Release();

    enumerator->Release();

    CoUninitialize();


    return 0;
}