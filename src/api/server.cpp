#ifdef ENABLE_API

#include "api/api_vm_adapter.h"
#include <drogon/drogon.h>
#include <iostream>

namespace api {

class ApiServer {
public:
    static void Start(const std::string &bind_addr, int port = 8080) {
        // Configure Drogon to listen on given address and port
        drogon::app().addListener(bind_addr, port);
        std::cout << "Starting API server on " << bind_addr << ":" << port << std::endl;
        drogon::app().run();
    }

    // Backwards-compatible overload
    static void Start(int port) {
        Start("0.0.0.0", port);
    }

    static void Stop() {
        drogon::app().quit();
    }
};

// Convenience C-style entrypoints used by the main executable.
void StartApiServer(const std::string &bind_addr, int port = 8080) {
    ApiServer::Start(bind_addr, port);
}

void StartApiServer(int port) {
    ApiServer::Start(port);
}

void StopApiServer() {
    ApiServer::Stop();
}

} // namespace api

#endif // ENABLE_API
