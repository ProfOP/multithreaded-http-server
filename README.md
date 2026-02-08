# Multithreaded HTTP Server in C++

A production-grade, multithreaded HTTP/1.1 server implemented in C++ using POSIX sockets and a fixed-size thread pool.  
The project focuses on concurrency, robustness, graceful shutdown, and performance evaluation rather than framework usage.

---

## 🚀 Features

- Multithreaded request handling using a thread pool (producer–consumer model)
- HTTP/1.1 GET request support
- Thread-safe logging with timestamps and thread IDs
- Graceful shutdown using POSIX signal handling (SIGINT / SIGTERM)
- Runtime configuration via command-line arguments
- Input validation and protection against malformed requests
- Directory traversal protection
- Performance benchmarking using Apache Bench

---

## 🧠 Architecture Overview

            +----------------------+
            |      Main Thread     |
            |----------------------|
            | socket() / bind()    |
            | listen()             |
            | accept()             |
            +----------+-----------+
                       |
                       v
            +----------------------+
            |     Task Queue       |
            | (mutex + cond var)   |
            +----------+-----------+
                       |
     +-----------------+-----------------+
     |                 |                 |
     v                 v                 v
+----------------+ +----------------+ +----------------+
| Worker Thread | | Worker Thread | | Worker Thread |
| handle_client | | handle_client | | handle_client |
+----------------+ +----------------+ +----------------+
|
v
HTTP parsing → file lookup → response write



---

## 🧵 Concurrency Model

- A fixed-size thread pool is created at startup.
- The main thread accepts incoming TCP connections.
- Each accepted client socket is enqueued as a task.
- Worker threads wait on a condition variable, dequeue tasks, and process requests concurrently.
- This avoids the overhead of creating a thread per request and improves scalability.

---

## 🔐 Request Handling & Hardening

The server is defensively implemented to handle invalid or malicious input safely:

- Only `GET` requests are supported (`405 Method Not Allowed` otherwise)
- Malformed or incomplete requests return `400 Bad Request`
- Directory traversal attempts (`../`) return `403 Forbidden`
- Non-existent resources return `404 Not Found`
- Oversized or empty reads are safely rejected
- The server remains stable under invalid input

---
