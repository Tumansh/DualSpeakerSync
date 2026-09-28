#include "AudioEngine.h"
#include "HttpServer.h"

#include <iostream>

int main()
{
    std::cout
        << "========================================\n"
        << "          MULTI SPEAKER ENGINE\n"
        << "========================================\n\n";

    AudioEngine engine;

    // Discover devices once at startup so we can verify that
    // Windows audio enumeration is working before React connects.
    auto devices = engine.getDevices();

    if (devices.empty())
    {
        std::cerr
            << "No active audio output devices found.\n";

        return 1;
    }

    std::cout
        << "Available output devices:\n\n";

    for (const auto& device : devices)
    {
        std::cout
            << "["
            << device.index
            << "] "
            << device.name
            << "\n";
    }

    std::cout
        << "\nWaiting for React UI selection...\n\n";

    HttpServer server(engine);

    if (!server.start(8765))
    {
        std::cerr
            << "Failed to start HTTP control server.\n";

        return 1;
    }

    std::cout
        << "Open your React application at:\n"
        << "http://localhost:5173\n\n";

    std::cout
        << "API endpoints:\n"
        << "  GET  /devices\n"
        << "  GET  /status\n"
        << "  POST /select\n"
        << "  POST /start\n"
        << "  POST /stop\n"
        << "  POST /volume\n\n";

    server.run();

    engine.stop();

    return 0;
}
