#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class SerialManager {
public:
    using LineCallback = std::function<void(const std::string&)>;

    SerialManager(std::string device, int baudRate);
    ~SerialManager();

    bool start(LineCallback callback);
    void stop();

    bool writeLine(const std::string& line);
    bool isConnected() const;
    const std::string& device() const;

private:
    bool openPort();
    void closePort();
    void readerLoop();

    std::string device_;
    int baudRate_;
    int fd_{-1};

    std::atomic<bool> running_{false};
    std::thread readerThread_;
    LineCallback callback_;

    mutable std::mutex writeMutex_;
};
