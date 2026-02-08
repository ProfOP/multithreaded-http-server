#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <csignal>

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <chrono>
#include <iomanip>

// =====================
// Global shutdown state
// =====================
std::atomic<bool> shutdown_requested(false);
int server_fd_global = -1;

// =====================
// Runtime config
// =====================
int SERVER_PORT = 8080;
int THREAD_COUNT = 4;
std::string WEB_ROOT = "./www";
const size_t MAX_REQUEST_SIZE = 4096;

// =====================
// Signal Handler
// =====================
void signal_handler(int) {
    shutdown_requested = true;
    if (server_fd_global != -1) {
        close(server_fd_global); // unblock accept()
    }
}

// =====================
// Logger
// =====================
class Logger {
public:
    Logger(const std::string& filename) {
        logfile.open(filename, std::ios::app);
    }
    ~Logger() {
        logfile.close();
    }

    void info(const std::string& msg) { log("INFO", msg); }
    void error(const std::string& msg) { log("ERROR", msg); }

private:
    std::ofstream logfile;
    std::mutex log_mutex;

    void log(const std::string& level, const std::string& msg) {
        std::lock_guard<std::mutex> lock(log_mutex);
        auto now = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now()
        );
        logfile << "[" << std::put_time(std::localtime(&now), "%F %T") << "] "
                << "[" << level << "] "
                << "[thread " << std::this_thread::get_id() << "] "
                << msg << std::endl;
    }
};

Logger logger("server.log");

// =====================
// Thread Pool
// =====================
class ThreadPool {
public:
    ThreadPool(int threads) : stop(false) {
        for (int i = 0; i < threads; i++) {
            workers.emplace_back([this]() { worker_loop(); });
        }
    }

    ~ThreadPool() {
        shutdown();
    }

    void enqueue(std::function<void()> task) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.push(task);
        }
        condition.notify_one();
    }

    void shutdown() {
        stop = true;
        condition.notify_all();
        for (auto& t : workers) {
            if (t.joinable()) t.join();
        }
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop;

    void worker_loop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                condition.wait(lock, [this]() {
                    return stop || !tasks.empty();
                });
                if (stop && tasks.empty()) return;
                task = tasks.front();
                tasks.pop();
            }
            task();
        }
    }
};

// =====================
// HTTP Logic
// =====================
struct HttpRequest {
    std::string method;
    std::string path;
    bool valid;
};

std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

HttpRequest parse_request(const std::string& raw) {
    std::istringstream stream(raw);
    HttpRequest req;
    req.valid = false;

    stream >> req.method >> req.path;

    if (req.method.empty() || req.path.empty()) {
        return req;
    }

    if (req.method != "GET") {
        req.valid = true;
        return req;
    }

    req.valid = true;
    return req;
}

std::string http_response(int code, const std::string& body) {
    std::string status;

    if (code == 200) status = "200 OK";
    else if (code == 400) status = "400 Bad Request";
    else if (code == 403) status = "403 Forbidden";
    else if (code == 404) status = "404 Not Found";
    else if (code == 405) status = "405 Method Not Allowed";
    else status = "500 Internal Server Error";

    return "HTTP/1.1 " + status +
           "\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\nContent-Type: text/plain\r\n\r\n" + body;
}

std::string build_response(const HttpRequest& req) {
    if (!req.valid) {
        return http_response(400, "Bad Request");
    }

    if (req.method != "GET") {
        return http_response(405, "Method Not Allowed");
    }

    std::string path = req.path;
    if (path == "/") path = "/index.html";

    if (path.find("..") != std::string::npos) {
        return http_response(403, "Forbidden");
    }

    std::string full_path = WEB_ROOT + path;
    std::string body = read_file(full_path);

    if (body.empty()) {
        return http_response(404, "Not Found");
    }

    return "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " +
           std::to_string(body.size()) + "\r\n\r\n" + body;
}

void handle_client(int client_fd) {
    char buffer[MAX_REQUEST_SIZE + 1] = {0};
    ssize_t bytes = read(client_fd, buffer, MAX_REQUEST_SIZE);

    if (bytes <= 0) {
        logger.error("Empty or failed read");
        close(client_fd);
        return;
    }

    HttpRequest req = parse_request(buffer);
    std::string response = build_response(req);

    write(client_fd, response.c_str(), response.size());
    logger.info("Request handled");

    close(client_fd);
}

// =====================
// Argument Parsing
// =====================
void parse_args(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc)
            SERVER_PORT = std::stoi(argv[++i]);
        else if (arg == "--threads" && i + 1 < argc)
            THREAD_COUNT = std::stoi(argv[++i]);
        else if (arg == "--root" && i + 1 < argc)
            WEB_ROOT = argv[++i];
    }
}

// =====================
// Main
// =====================
int main(int argc, char* argv[]) {
    parse_args(argc, argv);

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    server_fd_global = server_fd;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(SERVER_PORT);

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, 128);

    logger.info("Server started on port " + std::to_string(SERVER_PORT));
    std::cout << "Server running on port " << SERVER_PORT << "\n";

    ThreadPool pool(THREAD_COUNT);
    int addrlen = sizeof(address);

    while (!shutdown_requested) {
        int client_fd = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
        if (client_fd < 0) break;

        pool.enqueue([client_fd]() {
            handle_client(client_fd);
        });
    }

    logger.info("Shutdown initiated");
    pool.shutdown();
    logger.info("Shutdown complete");

    return 0;
}

