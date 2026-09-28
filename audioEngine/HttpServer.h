#pragma once

#include "AudioEngine.h"

#include <string>

class HttpServer
{
public:
    explicit HttpServer(AudioEngine& engine);
    ~HttpServer();

    bool start(unsigned short port = 8765);
    void run();
    void stop();

private:
    AudioEngine& engine;
    unsigned short port;
    bool running;
    void* listenSocket;

    void handleClient(void* clientSocket);
};
