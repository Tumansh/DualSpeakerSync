#pragma once

#include <string>
#include <vector>

struct AudioDeviceInfo
{
    int index;
    std::string name;
};

class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    bool initialize();
    bool start();
    void stop();

    bool isRunning() const;

    std::vector<AudioDeviceInfo> getDevices() const;
    void printDevices();

    bool setOutputDevices(int firstDeviceIndex, int secondDeviceIndex);

    void setCobraVolume(float volume);
    void setMiviVolume(float volume);

    float getCobraVolume() const;
    float getMiviVolume() const;

private:
    class Impl;
    Impl* impl;
};
