#include "TreeGenerationService.h"

#include "DirectoryTreeBuilder.h"

#include <cstring>
#include <exception>

TreeGenerationService::TreeGenerationService()
    : m_cancelRequested(false)
    , m_running(false) {
}

TreeGenerationService::~TreeGenerationService() {
    Cancel();
}

void TreeGenerationService::Start(const std::wstring& rootPath, int depth, CompletionCallback onCompleted, ErrorCallback onError, ProgressCallback onProgress) {
    Cancel();

    m_cancelRequested.store(false);
    m_running.store(true);

    m_worker = std::thread([this, rootPath, depth, onCompleted = std::move(onCompleted), onError = std::move(onError), onProgress = std::move(onProgress)]() mutable {
        try {
            DirectoryTreeBuilder builder;
            std::wstring result = builder.BuildTree(
                rootPath,
                depth,
                TreeFormat::TEXT,
                [this]() { return m_cancelRequested.load(); },
                onProgress
            );

            if (!m_cancelRequested.load() && onCompleted) {
                onCompleted(std::move(result));
            }
        }
        catch (const std::exception& e) {
            if (!m_cancelRequested.load() && onError) {
                std::wstring error = L"Ошибка: ";
                error += std::wstring(e.what(), e.what() + strlen(e.what()));
                onError(std::move(error));
            }
        }

        m_running.store(false);
    });
}

void TreeGenerationService::Cancel() {
    m_cancelRequested.store(true);

    if (m_worker.joinable()) {
        m_worker.join();
    }

    m_running.store(false);
}

bool TreeGenerationService::IsRunning() const {
    return m_running.load();
}
