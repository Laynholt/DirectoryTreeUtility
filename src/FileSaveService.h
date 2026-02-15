#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

enum class TreeFormat;

class FileSaveService {
public:
    using CompletionCallback = std::function<void()>;
    using ErrorCallback = std::function<void(std::wstring&&)>;

    FileSaveService();
    ~FileSaveService();

    bool SaveTextFileSync(const std::wstring& fileName, const std::wstring& content, std::wstring* errorMessage = nullptr) const;
    void SaveTreeAsync(const std::wstring& fileName, const std::wstring& rootPath, int depth, TreeFormat format, bool expandSymlinks, CompletionCallback onCompleted, ErrorCallback onError);
    void Cancel();
    bool IsRunning() const;

private:
    static bool WriteUtf8File(const std::wstring& fileName, const std::wstring& content, std::wstring* errorMessage);

    std::thread m_worker;
    std::atomic<bool> m_cancelRequested;
    std::atomic<bool> m_running;
};
