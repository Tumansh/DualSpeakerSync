#define WIN32_LEAN_AND_MEAN

#include "HttpServer.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace
{
std::string jsonEscape(const std::string& value)
{
    std::string result;

    for (char c : value)
    {
        switch (c)
        {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += c; break;
        }
    }

    return result;
}

std::string makeResponse(
    int statusCode,
    const std::string& statusText,
    const std::string& body)
{
    std::ostringstream response;

    response
        << "HTTP/1.1 "
        << statusCode
        << " "
        << statusText
        << "\r\n"
        << "Content-Type: application/json; charset=utf-8\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        << "Access-Control-Allow-Headers: Content-Type\r\n"
        << "Content-Length: "
        << body.size()
        << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;

    return response.str();
}

bool getJsonInt(
    const std::string& body,
    const std::string& key,
    int& value)
{
    std::string search = "\"" + key + "\"";

    size_t keyPos = body.find(search);

    if (keyPos == std::string::npos)
        return false;

    size_t colonPos = body.find(':', keyPos + search.size());

    if (colonPos == std::string::npos)
        return false;

    size_t pos = colonPos + 1;

    while (pos < body.size() &&
           std::isspace(
               static_cast<unsigned char>(body[pos])))
    {
        ++pos;
    }

    bool negative = false;

    if (pos < body.size() && body[pos] == '-')
    {
        negative = true;
        ++pos;
    }

    if (pos >= body.size() ||
        !std::isdigit(
            static_cast<unsigned char>(body[pos])))
    {
        return false;
    }

    int parsed = 0;

    while (pos < body.size() &&
           std::isdigit(
               static_cast<unsigned char>(body[pos])))
    {
        parsed =
            parsed * 10 +
            (body[pos] - '0');

        ++pos;
    }

    value = negative ? -parsed : parsed;

    return true;
}

bool getJsonFloat(
    const std::string& body,
    const std::string& key,
    float& value)
{
    std::string search = "\"" + key + "\"";
    size_t keyPos = body.find(search);

    if (keyPos == std::string::npos)
        return false;

    size_t colonPos = body.find(':', keyPos + search.size());

    if (colonPos == std::string::npos)
        return false;

    size_t pos = colonPos + 1;

    while (pos < body.size() &&
           std::isspace(
               static_cast<unsigned char>(body[pos])))
    {
        ++pos;
    }

    const char* start = body.c_str() + pos;
    char* end = nullptr;

    float parsed = std::strtof(start, &end);

    if (end == start)
        return false;

    value = parsed;
    return true;
}

std::string devicesJson(AudioEngine& engine)
{
    auto devices = engine.getDevices();

    std::ostringstream json;

    json << "{\"devices\":[";

    for (size_t i = 0; i < devices.size(); ++i)
    {
        if (i > 0)
            json << ',';

        json
            << "{"
            << "\"index\":"
            << devices[i].index
            << ","
            << "\"name\":\""
            << jsonEscape(devices[i].name)
            << "\""
            << "}";
    }

    json << "]}";

    return json.str();
}

std::string statusJson(AudioEngine& engine)
{
    std::ostringstream json;

    json
        << "{"
        << "\"running\":"
        << (engine.isRunning() ? "true" : "false")
        << ","
        << "\"cobraVolume\":"
        << engine.getCobraVolume()
        << ","
        << "\"miviVolume\":"
        << engine.getMiviVolume()
        << "}";

    return json.str();
}
}

HttpServer::HttpServer(AudioEngine& engine_)
    : engine(engine_),
      port(8765),
      running(false),
      listenSocket(nullptr)
{
}

HttpServer::~HttpServer()
{
    stop();
}

bool HttpServer::start(unsigned short port_)
{
    port = port_;

    WSADATA wsaData{};

    int result = WSAStartup(
        MAKEWORD(2, 2),
        &wsaData
    );

    if (result != 0)
    {
        std::cerr
            << "WSAStartup failed: "
            << result
            << "\n";

        return false;
    }

    SOCKET serverSocket = socket(
        AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP
    );

    if (serverSocket == INVALID_SOCKET)
    {
        std::cerr
            << "socket() failed: "
            << WSAGetLastError()
            << "\n";

        WSACleanup();
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(
        AF_INET,
        "127.0.0.1",
        &address.sin_addr
    );

    int reuse = 1;

    setsockopt(
        serverSocket,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse),
        sizeof(reuse)
    );

    if (bind(
            serverSocket,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) == SOCKET_ERROR)
    {
        std::cerr
            << "bind() failed: "
            << WSAGetLastError()
            << "\n";

        closesocket(serverSocket);
        WSACleanup();
        return false;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cerr
            << "listen() failed: "
            << WSAGetLastError()
            << "\n";

        closesocket(serverSocket);
        WSACleanup();
        return false;
    }

    listenSocket = reinterpret_cast<void*>(serverSocket);
    running = true;

    std::cout
        << "HTTP control server running at "
        << "http://127.0.0.1:"
        << port
        << "\n";

    return true;
}

void HttpServer::run()
{
    if (!running || listenSocket == nullptr)
        return;

    SOCKET serverSocket =
        reinterpret_cast<SOCKET>(listenSocket);

    while (running)
    {
        sockaddr_in clientAddress{};
        int clientAddressLength = sizeof(clientAddress);

        SOCKET clientSocket = accept(
            serverSocket,
            reinterpret_cast<sockaddr*>(&clientAddress),
            &clientAddressLength
        );

        if (clientSocket == INVALID_SOCKET)
        {
            if (running)
            {
                std::cerr
                    << "accept() failed: "
                    << WSAGetLastError()
                    << "\n";
            }

            break;
        }

        handleClient(
            reinterpret_cast<void*>(clientSocket)
        );
    }
}

void HttpServer::handleClient(void* clientSocketPointer)
{
    SOCKET clientSocket =
        reinterpret_cast<SOCKET>(clientSocketPointer);

    std::string request;
    char buffer[4096];

    size_t headerEnd = std::string::npos;
    size_t contentLength = 0;

    while (request.size() < 1024 * 1024)
    {
        int received = recv(
            clientSocket,
            buffer,
            sizeof(buffer),
            0
        );

        if (received <= 0)
            break;

        request.append(
            buffer,
            received
        );

        headerEnd = request.find("\r\n\r\n");

        if (headerEnd != std::string::npos)
        {
            std::string headers =
                request.substr(
                    0,
                    headerEnd
                );

            std::string contentLengthHeader =
                "Content-Length:";

            size_t lengthPos =
                headers.find(
                    contentLengthHeader
                );

            if (lengthPos == std::string::npos)
            {
                // Browser normally uses this exact casing.
                lengthPos = headers.find(
                    "content-length:"
                );

                if (lengthPos != std::string::npos)
                    contentLengthHeader = "content-length:";
            }

            if (lengthPos != std::string::npos)
            {
                size_t valuePos =
                    lengthPos + contentLengthHeader.size();

                while (valuePos < headers.size() &&
                       std::isspace(
                           static_cast<unsigned char>(
                               headers[valuePos])))
                {
                    ++valuePos;
                }

                contentLength =
                    static_cast<size_t>(
                        std::strtoul(
                            headers.c_str() + valuePos,
                            nullptr,
                            10
                        )
                    );
            }

            size_t bodyStart = headerEnd + 4;

            if (request.size() >=
                bodyStart + contentLength)
            {
                break;
            }
        }
    }

    std::string response;

    size_t requestLineEnd =
        request.find("\r\n");

    if (requestLineEnd == std::string::npos)
    {
        response = makeResponse(
            400,
            "Bad Request",
            "{\"error\":\"Invalid HTTP request\"}"
        );
    }
    else
    {
        std::string requestLine =
            request.substr(
                0,
                requestLineEnd
            );

        std::istringstream line(requestLine);

        std::string method;
        std::string path;
        std::string version;

        line >> method >> path >> version;

        size_t bodyStart =
            request.find("\r\n\r\n");

        std::string body;

        if (bodyStart != std::string::npos)
        {
            bodyStart += 4;
            body = request.substr(bodyStart);
        }

        if (method == "OPTIONS")
        {
            response = makeResponse(
                200,
                "OK",
                "{}"
            );
        }
        else if (method == "GET" &&
                 path == "/devices")
        {
            response = makeResponse(
                200,
                "OK",
                devicesJson(engine)
            );
        }
        else if (method == "GET" &&
                 path == "/status")
        {
            response = makeResponse(
                200,
                "OK",
                statusJson(engine)
            );
        }
        else if (method == "POST" &&
                 path == "/select")
        {
            int first = -1;
            int second = -1;

            bool firstOk = getJsonInt(
                body,
                "speaker1",
                first
            );

            bool secondOk = getJsonInt(
                body,
                "speaker2",
                second
            );

            if (!firstOk || !secondOk)
            {
                response = makeResponse(
                    400,
                    "Bad Request",
                    "{\"error\":\"speaker1 and speaker2 are required\"}"
                );
            }
            else if (!engine.setOutputDevices(first, second))
            {
                response = makeResponse(
                    400,
                    "Bad Request",
                    "{\"error\":\"Invalid speaker selection or engine is running\"}"
                );
            }
            else
            {
                response = makeResponse(
                    200,
                    "OK",
                    "{\"success\":true}"
                );
            }
        }
        else if (method == "POST" &&
                 path == "/start")
        {
            if (engine.isRunning())
            {
                response = makeResponse(
                    200,
                    "OK",
                    "{\"success\":true,\"running\":true}"
                );
            }
            else
            {
                // If this is the first start, initialize the engine.
                // After a normal stop, start() can reuse the initialized
                // WASAPI objects. If the user changed speakers while
                // stopped, setOutputDevices() releases them first, so
                // start() will fail and we initialize again below.
                if (!engine.start())
                {
                    if (!engine.initialize() || !engine.start())
                    {
                        response = makeResponse(
                            500,
                            "Internal Server Error",
                            "{\"error\":\"Failed to start audio engine\"}"
                        );
                    }
                    else
                    {
                        response = makeResponse(
                            200,
                            "OK",
                            "{\"success\":true,\"running\":true}"
                        );
                    }
                }
                else
                {
                    response = makeResponse(
                        200,
                        "OK",
                        "{\"success\":true,\"running\":true}"
                    );
                }
            }
        }
        else if (method == "POST" &&
                 path == "/stop")
        {
            engine.stop();

            response = makeResponse(
                200,
                "OK",
                "{\"success\":true,\"running\":false}"
            );
        }
        else if (method == "POST" &&
                 path == "/volume")
        {
            float speaker1 = 1.0f;
            float speaker2 = 1.0f;

            bool hasSpeaker1 = getJsonFloat(
                body,
                "speaker1",
                speaker1
            );

            bool hasSpeaker2 = getJsonFloat(
                body,
                "speaker2",
                speaker2
            );

            if (!hasSpeaker1 && !hasSpeaker2)
            {
                response = makeResponse(
                    400,
                    "Bad Request",
                    "{\"error\":\"No volume values supplied\"}"
                );
            }
            else
            {
                if (hasSpeaker1)
                    engine.setCobraVolume(speaker1);

                if (hasSpeaker2)
                    engine.setMiviVolume(speaker2);

                response = makeResponse(
                    200,
                    "OK",
                    statusJson(engine)
                );
            }
        }
        else
        {
            response = makeResponse(
                404,
                "Not Found",
                "{\"error\":\"Endpoint not found\"}"
            );
        }
    }

    send(
        clientSocket,
        response.c_str(),
        static_cast<int>(response.size()),
        0
    );

    closesocket(clientSocket);
}

void HttpServer::stop()
{
    if (!running && listenSocket == nullptr)
        return;

    running = false;

    if (listenSocket != nullptr)
    {
        SOCKET serverSocket =
            reinterpret_cast<SOCKET>(listenSocket);

        closesocket(serverSocket);
        listenSocket = nullptr;
    }

    WSACleanup();
}