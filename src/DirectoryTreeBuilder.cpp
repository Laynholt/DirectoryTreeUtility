#include "DirectoryTreeBuilder.h"
#include <algorithm>

const wchar_t* DirectoryTreeBuilder::TREE_BRANCH = L"├── ";
const wchar_t* DirectoryTreeBuilder::TREE_LAST = L"└── ";
const wchar_t* DirectoryTreeBuilder::TREE_VERTICAL = L"│   ";
const wchar_t* DirectoryTreeBuilder::TREE_SPACE = L"    ";

DirectoryTreeBuilder::DirectoryTreeBuilder() {
}

DirectoryTreeBuilder::~DirectoryTreeBuilder() {
}

std::wstring DirectoryTreeBuilder::BuildTree(const std::wstring& rootPath, int maxDepth, 
                                                  std::function<bool()> shouldCancel,
                                                  std::function<void(const std::wstring&)> progressCallback) {
    try {
        std::filesystem::path path(rootPath);
        if (!std::filesystem::exists(path)) {
            return L"Путь не существует: " + rootPath;
        }

        if (shouldCancel && shouldCancel()) {
            return L"Операция отменена";
        }

        int processedCount = 0;
        TreeNode root = BuildNodeTree(path, 0, maxDepth, shouldCancel, progressCallback, processedCount);
        
        if (shouldCancel && shouldCancel()) {
            return L"Операция отменена";
        }

        std::wstring result = path.filename().wstring();
        if (result.empty()) {
            result = path.wstring();
        }
        result += L"/\r\n";
        
        for (size_t i = 0; i < root.children.size(); ++i) {
            if (shouldCancel && shouldCancel()) {
                return L"Операция отменена";
            }
            
            bool isLast = (i == root.children.size() - 1);
            result += RenderTree(root.children[i], L"", isLast);
        }
        
        return result;
    }
    catch (const std::exception&) {
        return L"Ошибка при построении дерева директорий";
    }
}

TreeNode DirectoryTreeBuilder::BuildNodeTree(const std::filesystem::path& path, int currentDepth, int maxDepth,
                                                   std::function<bool()> shouldCancel,
                                                   std::function<void(const std::wstring&)> progressCallback,
                                                   int& processedCount) {
    TreeNode node(path.filename().wstring(), std::filesystem::is_directory(path));
    
    // Check for cancellation
    if (shouldCancel && shouldCancel()) {
        return node;
    }
    
    if (!node.isDirectory || (maxDepth >= 0 && currentDepth >= maxDepth)) {
        return node;
    }
    
    try {
        std::vector<std::filesystem::directory_entry> entries;
        
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            // Check for cancellation during directory iteration
            if (shouldCancel && shouldCancel()) {
                return node;
            }
            entries.push_back(entry);
        }
        
        std::sort(entries.begin(), entries.end(), 
                 [](const std::filesystem::directory_entry& a, const std::filesystem::directory_entry& b) {
                     bool aIsDir = a.is_directory();
                     bool bIsDir = b.is_directory();
                     
                     if (aIsDir != bIsDir) {
                         return aIsDir > bIsDir;
                     }
                     
                     std::wstring aName = a.path().filename().wstring();
                     std::wstring bName = b.path().filename().wstring();
                     std::transform(aName.begin(), aName.end(), aName.begin(), ::towlower);
                     std::transform(bName.begin(), bName.end(), bName.begin(), ::towlower);
                     
                     return aName < bName;
                 });
        
        for (const auto& entry : entries) {
            // Check for cancellation before processing each entry
            if (shouldCancel && shouldCancel()) {
                return node;
            }
            
            TreeNode child = BuildNodeTree(entry.path(), currentDepth + 1, maxDepth, 
                                               shouldCancel, progressCallback, processedCount);
            node.children.push_back(std::move(child));
            
            // Update progress
            ++processedCount;
            if (progressCallback && processedCount % 10 == 0) { // Report progress every 10 items
                std::wstring progress = L"Обработано элементов: " + std::to_wstring(processedCount);
                progressCallback(progress);
            }
        }
    }
    catch (const std::exception&) {
        // Handle filesystem exceptions silently
    }
    
    return node;
}

std::wstring DirectoryTreeBuilder::RenderTree(const TreeNode& node, const std::wstring& prefix, bool isLast) {
    std::wstring result;
    
    result += prefix;
    result += isLast ? TREE_LAST : TREE_BRANCH;
    result += node.name;
    if (node.isDirectory && !node.children.empty()) {
        result += L"/";
    }
    result += L"\r\n";
    
    std::wstring newPrefix = prefix + (isLast ? TREE_SPACE : TREE_VERTICAL);
    
    for (size_t i = 0; i < node.children.size(); ++i) {
        bool childIsLast = (i == node.children.size() - 1);
        result += RenderTree(node.children[i], newPrefix, childIsLast);
    }
    
    return result;
}