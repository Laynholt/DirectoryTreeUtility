#include "FileSaveService.h"

#include "DirectoryTreeBuilder.h"

#include <windows.h>

#include <cstring>
#include <exception>

FileSaveService::FileSaveService()
    : m_cancelRequested(false)
    , m_running(false) {
}

FileSaveService::~FileSaveService() {
    Cancel();
}

bool FileSaveService::SaveTextFileSync(const std::wstring& fileName, const std::wstring& content, std::wstring* errorMessage) const {
    return WriteUtf8File(fileName, content, errorMessage);
}

void FileSaveService::SaveTreeAsync(const std::wstring& fileName, const std::wstring& rootPath, int depth, TreeFormat format, CompletionCallback onCompleted, ErrorCallback onError) {
    Cancel();

    m_cancelRequested.store(false);
    m_running.store(true);

    m_worker = std::thread([this, fileName, rootPath, depth, format, onCompleted = std::move(onCompleted), onError = std::move(onError)]() mutable {
        try {
            DirectoryTreeBuilder builder;
            std::wstring content = builder.BuildTree(rootPath, depth, format);
            if (m_cancelRequested.load()) {
                m_running.store(false);
                return;
            }

            std::wstring errorMessage;
            if (WriteUtf8File(fileName, content, &errorMessage)) {
                if (!m_cancelRequested.load() && onCompleted) {
                    onCompleted();
                }
            } else {
                if (!m_cancelRequested.load() && onError) {
                    onError(std::move(errorMessage));
                }
            }
        }
        catch (const std::exception& e) {
            if (!m_cancelRequested.load() && onError) {
                std::wstring error = L"Ошибка сохранения: ";
                error += std::wstring(e.what(), e.what() + strlen(e.what()));
                onError(std::move(error));
            }
        }

        m_running.store(false);
    });
}

void FileSaveService::Cancel() {
    m_cancelRequested.store(true);

    if (m_worker.joinable()) {
        m_worker.join();
    }

    m_running.store(false);
}

bool FileSaveService::IsRunning() const {
    return m_running.load();
}

bool FileSaveService::WriteUtf8File(const std::wstring& fileName, const std::wstring& content, std::wstring* errorMessage) {
    HANDLE hFile = CreateFile(fileName.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (errorMessage) {
            *errorMessage = L"Ошибка создания файла";
        }
        return false;
    }

    const auto closeHandle = [&hFile]() {
        if (hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(hFile);
            hFile = INVALID_HANDLE_VALUE;
        }
    };

    const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8Length <= 0) {
        closeHandle();
        if (errorMessage) {
            *errorMessage = L"Ошибка конвертации текста в UTF-8";
        }
        return false;
    }

    std::string utf8Content(static_cast<size_t>(utf8Length), '\0');
    const int convertedLength = WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, utf8Content.data(), utf8Length, nullptr, nullptr);
    if (convertedLength != utf8Length) {
        closeHandle();
        if (errorMessage) {
            *errorMessage = L"Ошибка конвертации текста в UTF-8";
        }
        return false;
    }

    const DWORD bytesToWrite = static_cast<DWORD>(utf8Content.size() - 1);
    DWORD bytesWritten = 0;
    const BOOL writeOk = WriteFile(hFile, utf8Content.data(), bytesToWrite, &bytesWritten, nullptr);
    closeHandle();
    if (!writeOk || bytesWritten != bytesToWrite) {
        if (errorMessage) {
            *errorMessage = L"Ошибка записи файла";
        }
        return false;
    }

    return true;
}
