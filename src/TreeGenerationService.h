#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

class TreeGenerationService {
public:
    using CompletionCallback = std::function<void(std::wstring&&)>;
    using ErrorCallback = std::function<void(std::wstring&&)>;
    using ProgressCallback = std::function<void(const std::wstring&)>;

    TreeGenerationService();
    ~TreeGenerationService();

    void Start(const std::wstring& rootPath, int depth, bool expandSymlinks, CompletionCallback onCompleted, ErrorCallback onError, ProgressCallback onProgress = {});
    void Cancel();
    bool IsRunning() const;

private:
    std::thread m_worker;
    std::atomic<bool> m_cancelRequested;
    std::atomic<bool> m_running;
};
