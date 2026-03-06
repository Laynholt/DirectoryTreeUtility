#include "Application.h"

#include "AppInfo.h"
#include "ApplicationInternal.h"
#include "DarkMode.h"
#include "DirectoryTreeBuilder.h"
#include "FileSaveService.h"
#include "TreeGenerationService.h"
#include "UpdateService.h"

#include <commdlg.h>
#include <commctrl.h>
#include <malloc.h>
#include <richedit.h>
#include <psapi.h>

using namespace ApplicationInternal;

void Application::CompactTreeBuffersForNextBuild(int) {
    const size_t previousSize = m_treeContent.size();
    const size_t previousCapacity = m_treeContent.capacity();
    const bool hadLargeStorage = previousCapacity > kTreeLargeCharsThreshold;
    const bool shouldShrinkTreeStorage = hadLargeStorage || previousSize > kTreeLargeCharsThreshold;

    m_treeContent.clear();
    if (shouldShrinkTreeStorage) {
        std::wstring().swap(m_treeContent);
    }

    const bool shouldRecreateCanvas = previousSize > kTreeLargeCharsThreshold;
    if (shouldRecreateCanvas) {
        RecreateTreeCanvasControl();
    } else if (m_hTreeCanvas && IsWindow(m_hTreeCanvas)) {
        SetWindowText(m_hTreeCanvas, L"");
        SendMessage(m_hTreeCanvas, EM_EMPTYUNDOBUFFER, 0, 0);
        UpdateTreeCanvasScrollBarVisibility();
    }

    if (shouldShrinkTreeStorage || shouldRecreateCanvas) {
        TrimProcessMemoryUsage();
    }
}

void Application::RecreateTreeCanvasControl() {
    if (!m_hTreeCanvas || !IsWindow(m_hTreeCanvas)) {
        return;
    }

    RECT canvasRect = {};
    GetWindowRect(m_hTreeCanvas, &canvasRect);
    ScreenToClient(m_hWnd, reinterpret_cast<LPPOINT>(&canvasRect.left));
    ScreenToClient(m_hWnd, reinterpret_cast<LPPOINT>(&canvasRect.right));

    HWND previousCanvas = m_hTreeCanvas;
    HWND newCanvas = CreateWindowEx(
        0,
        L"EDIT",
        L"",
        WS_VISIBLE | WS_CHILD | WS_VSCROLL |
        ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        canvasRect.left,
        canvasRect.top,
        canvasRect.right - canvasRect.left,
        canvasRect.bottom - canvasRect.top,
        m_hWnd,
        reinterpret_cast<HMENU>(ID_TREE_CANVAS),
        m_hInstance,
        nullptr
    );

    if (!newCanvas) {
        SetWindowText(previousCanvas, L"");
        SendMessage(previousCanvas, EM_EMPTYUNDOBUFFER, 0, 0);
        return;
    }

    if (m_hMonoFont) {
        SendMessage(newCanvas, WM_SETFONT, reinterpret_cast<WPARAM>(m_hMonoFont), MAKELPARAM(FALSE, 0));
    }
    SendMessage(newCanvas, EM_SETUNDOLIMIT, 0, 0);
    SendMessage(newCanvas, EM_EMPTYUNDOBUFFER, 0, 0);
    Win32DarkMode::ApplyDarkScrollBarTheme(newCanvas);
    SetWindowSubclass(newCanvas, TreeCanvasSubclassProc, kTreeCanvasSubclassId, reinterpret_cast<DWORD_PTR>(this));

    RemoveWindowSubclass(previousCanvas, TreeCanvasSubclassProc, kTreeCanvasSubclassId);
    DestroyWindow(previousCanvas);
    m_hTreeCanvas = newCanvas;
    UpdateTreeCanvasScrollBarVisibility();
}

void Application::TrimProcessMemoryUsage() {
    _heapmin();
    HeapCompact(GetProcessHeap(), 0);

    HANDLE process = GetCurrentProcess();
    SetProcessWorkingSetSize(process, static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
    EmptyWorkingSet(process);
}

void Application::GenerateTree() {
    CancelGeneration();
    SetFocus(m_hWnd);
    GenerateTreeAsync();
}

void Application::GenerateTreeAsync() {
    wchar_t depthBuffer[32];
    GetWindowText(m_hDepthEdit, depthBuffer, 32);
    const int depth = _wtoi(depthBuffer);

    m_previousTreeSizeBeforeBuild = m_treeContent.size();
    m_previousTreeCapacityBeforeBuild = m_treeContent.capacity();
    CompactTreeBuffersForNextBuild(depth);

    m_isGenerating = true;
    m_animationStep = 0;
    SetTimer(m_hWnd, PROGRESS_TIMER_ID, 500, nullptr);
    EnableWindow(m_hGenerateBtn, FALSE);

    SetWindowText(m_hTreeCanvas, L"Генерируется дерево");
    SendMessage(m_hTreeCanvas, EM_EMPTYUNDOBUFFER, 0, 0);
    UpdateTreeCanvasScrollBarVisibility();

    const std::wstring currentPath = GetCurrentWorkingPath();
    if (!m_treeGenerationService) {
        OnTreeGenerationError(L"Сервис генерации не инициализирован");
        return;
    }

    m_treeGenerationService->Start(
        currentPath,
        depth,
        IsExpandSymlinksEnabled(),
        [this](std::wstring&& result) {
            std::wstring* completedResult = new std::wstring(std::move(result));
            if (!PostMessage(m_hWnd, WM_TREE_COMPLETED, 0, reinterpret_cast<LPARAM>(completedResult))) {
                delete completedResult;
            }
        },
        [this](std::wstring&& error) {
            std::wstring* errorMessage = new std::wstring(std::move(error));
            if (!PostMessage(m_hWnd, WM_TREE_ERROR, 0, reinterpret_cast<LPARAM>(errorMessage))) {
                delete errorMessage;
            }
        }
    );
}

void Application::CancelGeneration() {
    if (!m_isGenerating) {
        return;
    }

    if (m_treeGenerationService) {
        m_treeGenerationService->Cancel();
    }

    KillTimer(m_hWnd, PROGRESS_TIMER_ID);
    EnableWindow(m_hGenerateBtn, TRUE);
    m_isGenerating = false;
}

void Application::OnTreeGenerationCompleted(const std::wstring& result) {
    KillTimer(m_hWnd, PROGRESS_TIMER_ID);

    m_treeContent = result;
    if (m_treeContent.capacity() > kTreeLargeCharsThreshold &&
        m_treeContent.size() < (m_treeContent.capacity() / kTreeShrinkFactor)) {
        std::wstring compact(m_treeContent);
        m_treeContent.swap(compact);
    }

    const bool droppedByAbsoluteThreshold =
        m_previousTreeSizeBeforeBuild > m_treeContent.size() &&
        (m_previousTreeSizeBeforeBuild - m_treeContent.size()) > kTreeLargeCharsThreshold;
    const bool droppedByFactor =
        m_previousTreeSizeBeforeBuild > kTreeLargeCharsThreshold &&
        m_treeContent.size() < (m_previousTreeSizeBeforeBuild / kTreeShrinkFactor);
    const bool droppedFromLargeTree = droppedByAbsoluteThreshold || droppedByFactor;
    if (droppedFromLargeTree || m_previousTreeCapacityBeforeBuild > kTreeLargeCharsThreshold) {
        RecreateTreeCanvasControl();
    }

    SetWindowText(m_hTreeCanvas, m_treeContent.c_str());
    SendMessage(m_hTreeCanvas, EM_EMPTYUNDOBUFFER, 0, 0);
    UpdateTreeCanvasScrollBarVisibility();
    if (droppedFromLargeTree) {
        TrimProcessMemoryUsage();
    }

    ShowStatusMessage(L"Дерево директорий построено");
    EnableWindow(m_hGenerateBtn, TRUE);
    m_hasGeneratedTree = true;
    m_isGenerating = false;
}

void Application::OnTreeGenerationError(const std::wstring& error) {
    KillTimer(m_hWnd, PROGRESS_TIMER_ID);
    SetWindowText(m_hTreeCanvas, error.c_str());
    SendMessage(m_hTreeCanvas, EM_EMPTYUNDOBUFFER, 0, 0);
    UpdateTreeCanvasScrollBarVisibility();
    TrimProcessMemoryUsage();
    ShowStatusMessage(L"Ошибка при построении дерева");
    EnableWindow(m_hGenerateBtn, TRUE);
    m_isGenerating = false;
}

void Application::UpdateProgressAnimation() {
    if (!m_isGenerating) {
        return;
    }

    m_animationStep = (m_animationStep + 1) % 4;

    std::wstring message = L"Генерируется дерево";
    for (int i = 0; i < m_animationStep; ++i) {
        message += L".";
    }
    SetWindowText(m_hTreeCanvas, message.c_str());
    UpdateTreeCanvasScrollBarVisibility();

    std::wstring statusMessage = L"Построение дерева";
    for (int i = 0; i < m_animationStep; ++i) {
        statusMessage += L".";
    }
    ShowPersistentStatusMessage(statusMessage);
}

void Application::CopyToClipboard() {
    if (m_treeContent.empty()) {
        return;
    }

    if (!OpenClipboard(m_hWnd)) {
        return;
    }

    EmptyClipboard();
    const size_t size = (m_treeContent.length() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (hMem) {
        void* pMem = GlobalLock(hMem);
        if (pMem) {
            wcscpy_s(static_cast<wchar_t*>(pMem), m_treeContent.length() + 1, m_treeContent.c_str());
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    CloseClipboard();
    ShowStatusMessage(L"Скопировано в буфер обмена");
}

void Application::SaveToFile() {
    if (m_treeContent.empty()) {
        return;
    }

    OPENFILENAME ofn = {};
    wchar_t szFile[260] = {};

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0JSON Files (*.json)\0*.json\0XML Files (*.xml)\0*.xml\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (!GetSaveFileName(&ofn)) {
        return;
    }

    std::wstring fileName(szFile);
    TreeFormat format = TreeFormat::TEXT;
    std::wstring expectedExt = L".txt";

    switch (ofn.nFilterIndex) {
    case 2:
        format = TreeFormat::JSON;
        expectedExt = L".json";
        break;
    case 3:
        format = TreeFormat::XML;
        expectedExt = L".xml";
        break;
    default:
        break;
    }

    if (fileName.length() < expectedExt.length() ||
        fileName.substr(fileName.length() - expectedExt.length()) != expectedExt) {
        fileName += expectedExt;
    }

    if (format == TreeFormat::TEXT) {
        SaveFileSync(std::move(fileName), m_treeContent);
    } else {
        SaveFileAsync(std::move(fileName), format);
    }
}

void Application::SaveFileSync(std::wstring&& fileName, const std::wstring& content) {
    std::wstring errorMessage;
    if (m_fileSaveService && m_fileSaveService->SaveTextFileSync(fileName, content, &errorMessage)) {
        ShowStatusMessage(L"Файл сохранен");
        return;
    }

    if (errorMessage.empty()) {
        errorMessage = L"Сервис сохранения не инициализирован";
    }
    ShowStatusMessage(errorMessage);
}

void Application::SaveFileAsync(std::wstring&& fileName, TreeFormat format) {
    if (m_isSaving.load()) {
        return;
    }

    m_isSaving = true;
    ShowPersistentStatusMessage(L"Сохранение файла...");

    wchar_t depthBuffer[32];
    GetWindowText(m_hDepthEdit, depthBuffer, 32);
    const int depth = _wtoi(depthBuffer);
    const std::wstring currentPath = GetCurrentWorkingPath();

    if (!m_fileSaveService) {
        OnSaveError(L"Сервис сохранения не инициализирован");
        return;
    }

    m_fileSaveService->SaveTreeAsync(
        fileName,
        currentPath,
        depth,
        format,
        IsExpandSymlinksEnabled(),
        [this]() {
            PostMessage(m_hWnd, WM_SAVE_COMPLETED, 0, 0);
        },
        [this](std::wstring&& error) {
            std::wstring* saveError = new std::wstring(std::move(error));
            if (!PostMessage(m_hWnd, WM_SAVE_ERROR, 0, reinterpret_cast<LPARAM>(saveError))) {
                delete saveError;
            }
        }
    );
}

void Application::OnSaveCompleted() {
    m_isSaving = false;
    ShowStatusMessage(L"Файл сохранен");
}

void Application::OnSaveError(const std::wstring& error) {
    m_isSaving = false;
    ShowStatusMessage(error);
}

void Application::CheckForUpdates() {
    const wchar_t* checkUpdatesTitle = L"Проверка обновлений";
    const wchar_t* updateTitle = L"Обновление";

    if (!m_updateService) {
        m_updateService = std::make_unique<UpdateService>();
    }
    if (!m_updateService) {
        ShowStyledMessageDialog(checkUpdatesTitle, L"Сервис обновлений не инициализирован", L"Закрыть");
        return;
    }

    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    ShowPersistentStatusMessage(L"Проверка обновлений...");
    const UpdateCheckResult checkResult = m_updateService->CheckForUpdates(AppInfo::kVersion);
    SetCursor(LoadCursor(nullptr, IDC_ARROW));

    if (!checkResult.success) {
        std::wstring message = L"Не удалось проверить обновления.\r\n\r\n";
        message += checkResult.errorMessage;
        ShowStyledMessageDialog(checkUpdatesTitle, message, L"Закрыть");
        ShowStatusMessage(L"Ошибка проверки обновлений");
        return;
    }

    const std::wstring latestVersion = checkResult.latestVersion.empty() ? checkResult.latestTag : checkResult.latestVersion;
    if (!checkResult.updateAvailable) {
        std::wstring message = L"Установлена последняя версия приложения.";
        if (!latestVersion.empty()) {
            message += L"\r\n\r\nТекущая версия: ";
            message += AppInfo::kVersion;
            message += L"\r\nПоследний релиз: ";
            message += latestVersion;
        }
        ShowStyledMessageDialog(checkUpdatesTitle, message, L"Закрыть");
        ShowStatusMessage(L"Установлена последняя версия");
        return;
    }

    std::wstring message = L"Доступна новая версия: ";
    message += latestVersion.empty() ? checkResult.latestTag : latestVersion;
    message += L"\r\nТекущая версия: ";
    message += AppInfo::kVersion;
    message += L"\r\n\r\nСкачать и установить обновление сейчас?";

    const int userDecision = ShowStyledMessageDialog(
        checkUpdatesTitle,
        message,
        L"Скачать",
        L"Отмена"
    );

    if (userDecision != IDYES) {
        return;
    }

    wchar_t tempPath[MAX_PATH] = {};
    const DWORD tempPathLength = GetTempPathW(MAX_PATH, tempPath);
    if (tempPathLength == 0 || tempPathLength >= MAX_PATH) {
        ShowStyledMessageDialog(updateTitle, L"Не удалось определить временную директорию", L"Закрыть");
        ShowStatusMessage(L"Ошибка загрузки обновления");
        return;
    }

    std::wstring downloadedExePath = tempPath;
    if (!downloadedExePath.empty() && downloadedExePath.back() != L'\\') {
        downloadedExePath.push_back(L'\\');
    }
    downloadedExePath += AppInfo::kTempUpdateExecutableName;

    std::wstring downloadError;
    SetCursor(LoadCursor(nullptr, IDC_WAIT));
    ShowPersistentStatusMessage(L"Загрузка обновления...");
    const bool downloaded = m_updateService->DownloadReleaseExecutable(checkResult.latestTag, downloadedExePath, downloadError);
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    if (!downloaded) {
        std::wstring errorMessage = L"Не удалось скачать обновление.\r\n\r\n";
        errorMessage += downloadError;
        ShowStyledMessageDialog(updateTitle, errorMessage, L"Закрыть");
        ShowStatusMessage(L"Ошибка загрузки обновления");
        return;
    }

    wchar_t currentExePath[MAX_PATH] = {};
    const DWORD currentExePathLength = GetModuleFileNameW(nullptr, currentExePath, MAX_PATH);
    if (currentExePathLength == 0 || currentExePathLength >= MAX_PATH) {
        DeleteFileW(downloadedExePath.c_str());
        ShowStyledMessageDialog(updateTitle, L"Не удалось определить путь текущего приложения", L"Закрыть");
        ShowStatusMessage(L"Ошибка обновления");
        return;
    }

    std::wstring launchError;
    if (!m_updateService->LaunchUpdaterProcess(GetCurrentProcessId(), downloadedExePath, currentExePath, launchError)) {
        DeleteFileW(downloadedExePath.c_str());
        std::wstring errorMessage = L"Не удалось запустить установку обновления.\r\n\r\n";
        errorMessage += launchError;
        ShowStyledMessageDialog(updateTitle, errorMessage, L"Закрыть");
        ShowStatusMessage(L"Ошибка обновления");
        return;
    }

    ShowPersistentStatusMessage(L"Обновление готово. Перезапуск...");
    RequestExit();
}
