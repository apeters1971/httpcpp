
#pragma once

#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <cstring>  // For memcpy
#define RING_BUFFER_SIZE 4*1024*1024  // Must be a power of 2

class RingBuffer {
private:
    std::vector<char> buffer;
    std::atomic<size_t> head;
    std::atomic<size_t> tail;
    std::atomic<bool> eof_flag;  // NEW: Indicates EOF status
    std::mutex read_mutex;
    std::mutex write_mutex;
    std::condition_variable not_empty;
    std::condition_variable not_full;
    size_t available_space;
    size_t available_data;

public:
    RingBuffer() : buffer(RING_BUFFER_SIZE), head(0), tail(0), available_space(RING_BUFFER_SIZE), available_data(0), eof_flag(false) {}

    // Prevent copying
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    // Allow move semantics
    RingBuffer(RingBuffer&& other) noexcept 
        : buffer(std::move(other.buffer)), 
          head(other.head.load()), 
          tail(other.tail.load()),
          eof_flag(other.eof_flag.load()) {}

    RingBuffer& operator=(RingBuffer&& other) noexcept {
        if (this != &other) {
            buffer = std::move(other.buffer);
            head.store(other.head.load());
            tail.store(other.tail.load());
            eof_flag.store(other.eof_flag.load());
        }
        return *this;
    }

    // **Set EOF flag: Once set, readers will return 0 if no data remains**
    void set_eof() {
        eof_flag.store(true, std::memory_order_release);
        not_empty.notify_all();  // Wake up any waiting readers
    }

    // **Blocking Write: Always succeeds, waits if buffer is full**
    size_t write(const void* data, size_t len) {
        std::unique_lock<std::mutex> lock(write_mutex);

        // Wait until there is enough space
        not_full.wait(lock, [&] { return available_space >= len || eof_flag.load(std::memory_order_acquire); });

        if (eof_flag.load(std::memory_order_acquire)) {
            return 0;  // Do not allow writes after EOF is set
        }

        size_t current_tail = tail.load(std::memory_order_relaxed);
        size_t first_part = std::min(len, RING_BUFFER_SIZE - current_tail);
        size_t second_part = len - first_part;

        // Copy in two parts (if wrap-around occurs)
        memcpy(&buffer[current_tail], data, first_part);
        if (second_part > 0) {
            memcpy(&buffer[0], static_cast<const char*>(data) + first_part, second_part);
        }

        tail.store((current_tail + len) % RING_BUFFER_SIZE, std::memory_order_release);
        available_space -= len;
        available_data += len;

        // Notify readers that data is available
        not_empty.notify_one();
	return len;
    }

    // **Blocking Read: Returns 0 if EOF is set and no more data**
    size_t read(void* out_data, size_t len) {
        std::unique_lock<std::mutex> lock(read_mutex);

        // Wait until data is available or EOF is set
        not_empty.wait(lock, [&] { return available_data > 0 || eof_flag.load(std::memory_order_acquire); });

        // If EOF is set and no data left, return 0
        if (available_data == 0 && eof_flag.load(std::memory_order_acquire)) {
            return 0;
        }

        size_t current_head = head.load(std::memory_order_relaxed);
        size_t read_len = std::min(len, available_data);
        size_t first_part = std::min(read_len, RING_BUFFER_SIZE - current_head);
        size_t second_part = read_len - first_part;

        // Copy in two parts (if wrap-around occurs)
        memcpy(out_data, &buffer[current_head], first_part);
        if (second_part > 0) {
            memcpy(static_cast<char*>(out_data) + first_part, &buffer[0], second_part);
        }

        head.store((current_head + read_len) % RING_BUFFER_SIZE, std::memory_order_release);
        available_data -= read_len;
        available_space += read_len;

        // Notify writers that space is available
        not_full.notify_one();

        return read_len;  // Return the number of bytes actually read
    }
};

class RingBufferFDManager {
private:
    static std::unordered_map<int, std::shared_ptr<RingBuffer>> ringBuffers;
    static std::atomic<int> fd_counter;
    static std::mutex mutex;

public:
    static int create() {
        std::lock_guard<std::mutex> lock(mutex);
        int new_fd = fd_counter.fetch_add(1, std::memory_order_relaxed);
        ringBuffers[new_fd] = std::make_shared<RingBuffer>();  // Store shared_ptr
        return new_fd;
    }

    static void write(int fd, const void* data, size_t len) {
        std::shared_ptr<RingBuffer> buffer;

        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = ringBuffers.find(fd);
            if (it == ringBuffers.end()) {
                std::cerr << "[Error] Invalid fd: " << fd << std::endl;
                return;
            }
            buffer = it->second;  // Copy shared_ptr outside the mutex
        }

        buffer->write(data, len);
    }

    static size_t read(int fd, void* out_data, size_t len) {
        std::shared_ptr<RingBuffer> buffer;

        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = ringBuffers.find(fd);
            if (it == ringBuffers.end()) {
                std::cerr << "[Error] Invalid fd: " << fd << std::endl;
                return 0;
            }
            buffer = it->second;  // Copy shared_ptr outside the mutex
        }

        return buffer->read(out_data, len);
    }

    static void destroy(int fd) {
        std::lock_guard<std::mutex> lock(mutex);
        ringBuffers.erase(fd);  // Removes shared_ptr (auto-deletes when ref count = 0)
    }

    static void set_eof(int fd) {
        std::shared_ptr<RingBuffer> buffer;

        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = ringBuffers.find(fd);
            if (it == ringBuffers.end()) {
                std::cerr << "[Error] Invalid fd: " << fd << std::endl;
                return;
            }
            buffer = it->second;  // Copy shared_ptr outside the mutex
        }

        buffer->set_eof();
    }
};
