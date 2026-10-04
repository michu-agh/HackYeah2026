#include "SerialManager.hpp"

#include <crow.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <string>

using json = nlohmann::json;

#ifndef CRISIS_WEB_DIR
#define CRISIS_WEB_DIR "./web"
#endif

namespace {

constexpr std::size_t MAX_TITLE_LENGTH = 60;
constexpr std::size_t MAX_MESSAGE_LENGTH = 180;

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

crow::response staticFile(const std::string& relativePath,
                          const std::string& contentType) {
    const std::string fullPath =
        std::string(CRISIS_WEB_DIR) + "/" + relativePath;

    const std::string body = readFile(fullPath);

    if (body.empty()) {
        return crow::response(404, "File not found");
    }

    crow::response response;
    response.code = 200;
    response.set_header("Content-Type", contentType);
    response.set_header("Cache-Control", "no-cache");
    response.body = body;
    return response;
}

std::uint64_t unixTimeMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()
    ).count();
}

std::string makeMessageId() {
    static std::atomic<std::uint64_t> counter{0};

    std::random_device rd;
    const std::uint32_t randomPart = rd();

    std::ostringstream out;
    out << std::hex << std::uppercase
        << unixTimeMs()
        << "-"
        << (randomPart & 0xFFFFu)
        << "-"
        << counter.fetch_add(1);

    return out.str();
}

json makeError(const std::string& message) {
    return {
        {"ok", false},
        {"error", message}
    };
}

crow::response jsonResponse(const json& data, int status = 200) {
    crow::response response;
    response.code = status;
    response.set_header("Content-Type", "application/json; charset=utf-8");
    response.body = data.dump();
    return response;
}

// Keeps active browser WebSocket clients.
// The serial thread can broadcast events to every connected dashboard.
class WebSocketHub {
public:
    void add(crow::websocket::connection& connection) {
        std::lock_guard<std::mutex> lock(mutex_);
        clients_.insert(&connection);
    }

    void remove(crow::websocket::connection& connection) {
        std::lock_guard<std::mutex> lock(mutex_);
        clients_.erase(&connection);
    }

    void broadcast(const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);

        for (auto* client : clients_) {
            try {
                client->send_text(text);
            } catch (...) {
                // Crow will call onclose/onerror for dead sockets.
            }
        }
    }

private:
    std::mutex mutex_;
    std::set<crow::websocket::connection*> clients_;
};

json normalizeSerialEvent(const std::string& line) {
    // If ESP32 sends JSON, forward it as structured JSON.
    // Otherwise wrap the raw text so the frontend can still display it.
    try {
        json parsed = json::parse(line);

        if (!parsed.contains("type")) {
            parsed["type"] = "SERIAL_EVENT";
        }

        parsed["receivedAt"] = unixTimeMs();
        return parsed;
    } catch (...) {
        return {
            {"type", "SERIAL_LINE"},
            {"line", line},
            {"receivedAt", unixTimeMs()}
        };
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string serialDevice = "/dev/ttyACM0";
    int baudRate = 115200;

    if (argc >= 2) {
        serialDevice = argv[1];
    }
    if (argc >= 3) {
        baudRate = std::stoi(argv[2]);
    }

    crow::SimpleApp app;
    WebSocketHub wsHub;
    SerialManager serial(serialDevice, baudRate);

    // ------------------------------------------------------------
    // FRONTEND
    // ------------------------------------------------------------

    CROW_ROUTE(app, "/")([] {
        return staticFile("index.html", "text/html; charset=utf-8");
    });

    CROW_ROUTE(app, "/style.css")([] {
        return staticFile("style.css", "text/css; charset=utf-8");
    });

    CROW_ROUTE(app, "/app.js")([] {
        return staticFile("app.js", "application/javascript; charset=utf-8");
    });

    // ------------------------------------------------------------
    // API
    // ------------------------------------------------------------

    CROW_ROUTE(app, "/api/health")
    ([] {
        return jsonResponse({
            {"ok", true},
            {"service", "CrisisMesh RPi"}
        });
    });

    CROW_ROUTE(app, "/api/status")
    ([&serial] {
        return jsonResponse({
            {"ok", true},
            {"serialConnected", serial.isConnected()},
            {"serialDevice", serial.device()}
        });
    });

    CROW_ROUTE(app, "/api/messages")
    .methods(crow::HTTPMethod::Post)
    ([&serial, &wsHub](const crow::request& request) {
        json input;

        try {
            input = json::parse(request.body);
        } catch (...) {
            return jsonResponse(makeError("Nieprawidłowy JSON."), 400);
        }

        const std::string title =
            input.value("title", std::string{});
        const std::string message =
            input.value("message", std::string{});
        const std::string type =
            input.value("type", std::string{"ALERT"});
        const int priority =
            input.value("priority", 2);
        const int ttl =
            input.value("ttl", 6);

        if (message.empty()) {
            return jsonResponse(makeError("Treść komunikatu jest pusta."), 400);
        }

        if (title.size() > MAX_TITLE_LENGTH) {
            return jsonResponse(
                makeError("Tytuł jest za długi. Maksymalnie 60 znaków."),
                400
            );
        }

        if (message.size() > MAX_MESSAGE_LENGTH) {
            return jsonResponse(
                makeError("Komunikat jest za długi. Maksymalnie 180 znaków."),
                400
            );
        }

        if (priority < 1 || priority > 3) {
            return jsonResponse(makeError("Priorytet musi być 1..3."), 400);
        }

        if (ttl < 1 || ttl > 20) {
            return jsonResponse(makeError("TTL musi być 1..20."), 400);
        }

        // --------------------------------------------------------
        // THIS IS THE SERIAL PROTOCOL BOUNDARY.
        //
        // Currently RPi -> ESP32 Gateway is one JSON object per line:
        //
        // {"cmd":"SEND", ...}\n
        //
        // If your existing gateway expects another frame format,
        // change ONLY this object / serial.writeLine() section.
        // --------------------------------------------------------

        const std::string id = makeMessageId();

        json frame = {
            {"cmd", "SEND"},
            {"id", id},
            {"type", type},
            {"priority", priority},
            {"ttl", ttl},
            {"timestamp", unixTimeMs()},
            {"title", title},
            {"message", message}
        };

        if (!serial.writeLine(frame.dump())) {
            return jsonResponse(
                makeError("Brak połączenia z ESP32 Gateway."),
                503
            );
        }

        json event = {
            {"type", "MESSAGE_SENT"},
            {"id", id},
            {"messageType", type},
            {"priority", priority},
            {"ttl", ttl},
            {"title", title},
            {"message", message},
            {"timestamp", unixTimeMs()}
        };

        wsHub.broadcast(event.dump());

        return jsonResponse({
            {"ok", true},
            {"id", id},
            {"status", "sent_to_gateway"}
        }, 202);
    });

    // Optional debug endpoint for direct serial testing.
    CROW_ROUTE(app, "/api/serial/raw")
    .methods(crow::HTTPMethod::Post)
    ([&serial](const crow::request& request) {
        json input;

        try {
            input = json::parse(request.body);
        } catch (...) {
            return jsonResponse(makeError("Nieprawidłowy JSON."), 400);
        }

        const std::string line = input.value("line", std::string{});

        if (line.empty()) {
            return jsonResponse(makeError("Brak pola 'line'."), 400);
        }

        if (!serial.writeLine(line)) {
            return jsonResponse(makeError("Serial niedostępny."), 503);
        }

        return jsonResponse({{"ok", true}});
    });

    // ------------------------------------------------------------
    // WEBSOCKET
    // ------------------------------------------------------------

    CROW_WEBSOCKET_ROUTE(app, "/ws")
        .onopen([&wsHub](crow::websocket::connection& connection) {
            wsHub.add(connection);

            connection.send_text(json({
                {"type", "WS_CONNECTED"},
                {"timestamp", unixTimeMs()}
            }).dump());
        })
        .onclose([&wsHub](crow::websocket::connection& connection,
                          const std::string&,
                          std::uint16_t) {
            wsHub.remove(connection);
        })
        .onmessage([](crow::websocket::connection&,
                      const std::string&,
                      bool) {
            // Dashboard currently doesn't need browser -> WS messages.
        });

    // ------------------------------------------------------------
    // SERIAL -> BROWSER
    // ------------------------------------------------------------

    const bool serialStarted = serial.start(
        [&wsHub](const std::string& line) {
            std::cout << "[ESP] " << line << '\n';

            const json event = normalizeSerialEvent(line);
            wsHub.broadcast(event.dump());
        }
    );

    if (!serialStarted) {
        std::cerr
            << "[WARN] Web UI will start, but ESP32 serial is unavailable.\n"
            << "[WARN] Check device path and permissions (dialout group).\n";
    }

    std::cout << "\nCrisisMesh dashboard:\n"
              << "  http://0.0.0.0:8080\n\n";

    // multithreaded() allows REST/WebSocket handling in parallel.
    app.port(8080)
       .bindaddr("0.0.0.0")
       .multithreaded()
       .run();

    serial.stop();
    return 0;
}
