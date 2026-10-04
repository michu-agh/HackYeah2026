#include "SerialManager.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace {
speed_t baudToTermios(int baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
#ifdef B230400
        case 230400: return B230400;
#endif
        default:     return B115200;
    }
}
}

SerialManager::SerialManager(std::string device, int baudRate)
    : device_(std::move(device)), baudRate_(baudRate) {}

SerialManager::~SerialManager() {
    stop();
}

bool SerialManager::openPort() {
    fd_ = ::open(device_.c_str(), O_RDWR | O_NOCTTY | O_SYNC);

    if (fd_ < 0) {
        std::cerr << "[SERIAL] Cannot open " << device_
                  << ": " << std::strerror(errno) << '\n';
        return false;
    }

    termios tty{};
    if (tcgetattr(fd_, &tty) != 0) {
        std::cerr << "[SERIAL] tcgetattr failed: "
                  << std::strerror(errno) << '\n';
        closePort();
        return false;
    }

    const speed_t speed = baudToTermios(baudRate_);
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    // 8N1
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;

    // Short timeout makes stop() responsive.
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 2;  // 0.2 s

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
#ifdef CRTSCTS
    tty.c_cflag &= ~CRTSCTS;
#endif

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        std::cerr << "[SERIAL] tcsetattr failed: "
                  << std::strerror(errno) << '\n';
        closePort();
        return false;
    }

    tcflush(fd_, TCIOFLUSH);

    std::cout << "[SERIAL] Connected: " << device_
              << " @ " << baudRate_ << " baud\n";
    return true;
}

void SerialManager::closePort() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool SerialManager::start(LineCallback callback) {
    if (running_) {
        return true;
    }

    callback_ = std::move(callback);

    if (!openPort()) {
        return false;
    }

    running_ = true;
    readerThread_ = std::thread(&SerialManager::readerLoop, this);
    return true;
}

void SerialManager::stop() {
    running_ = false;

    if (readerThread_.joinable()) {
        readerThread_.join();
    }

    closePort();
}

bool SerialManager::writeLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(writeMutex_);

    if (fd_ < 0) {
        return false;
    }

    std::string packet = line;
    if (packet.empty() || packet.back() != '\n') {
        packet.push_back('\n');
    }

    const char* ptr = packet.data();
    std::size_t remaining = packet.size();

    while (remaining > 0) {
        const ssize_t written = ::write(fd_, ptr, remaining);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }

            std::cerr << "[SERIAL] write failed: "
                      << std::strerror(errno) << '\n';
            return false;
        }

        ptr += written;
        remaining -= static_cast<std::size_t>(written);
    }

    tcdrain(fd_);
    return true;
}

bool SerialManager::isConnected() const {
    return fd_ >= 0;
}

const std::string& SerialManager::device() const {
    return device_;
}

void SerialManager::readerLoop() {
    std::string pending;
    char buffer[256];

    while (running_) {
        const ssize_t count = ::read(fd_, buffer, sizeof(buffer));

        if (count > 0) {
            pending.append(buffer, static_cast<std::size_t>(count));

            std::size_t pos = 0;
            while ((pos = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, pos);
                pending.erase(0, pos + 1);

                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                if (!line.empty() && callback_) {
                    callback_(line);
                }
            }
        } else if (count < 0 && errno != EINTR) {
            std::cerr << "[SERIAL] read failed: "
                      << std::strerror(errno) << '\n';
            break;
        }
    }
}
