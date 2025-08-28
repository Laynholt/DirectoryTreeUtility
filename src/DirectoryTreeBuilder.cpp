#include "DirectoryTreeBuilder.h"
#include <algorithm>
#include <iostream>

const wchar_t* DirectoryTreeBuilder::TREE_BRANCH = L"├── ";
const wchar_t* DirectoryTreeBuilder::TREE_LAST = L"└── ";
const wchar_t* DirectoryTreeBuilder::TREE_VERTICAL = L"│   ";
const wchar_t* DirectoryTreeBuilder::TREE_SPACE = L"    ";

DirectoryTreeBuilder::DirectoryTreeBuilder() {
}

DirectoryTreeBuilder::~DirectoryTreeBuilder() {
}

std::wstring DirectoryTreeBuilder::BuildTree(const std::wstring& rootPath, int maxDepth) {
    try {
        std::filesystem::path path(rootPath);
        if (!std::filesystem::exists(path)) {
            return L"Путь не существует: " + rootPath;
        }

        TreeNode root = BuildNodeTree(path, 0, maxDepth);
        
        std::wstring result = path.filename().wstring();
        if (result.empty()) {
            result = path.wstring();
        }
        result += L"/\n";
        
        for (size_t i = 0; i < root.children.size(); ++i) {
            bool isLast = (i == root.children.size() - 1);
            result += RenderTree(root.children[i], L"", isLast);
        }
        
        return result;
    }
    catch (const std::exception&) {
        return L"Ошибка при построении дерева директорий";
    }
}

std::wstring DirectoryTreeBuilder::BuildTreeForSelected(const std::vector<std::wstring>& selectedPaths, int maxDepth) {
    std::wstring result;
    
    for (const auto& path : selectedPaths) {
        if (!result.empty()) {
            result += L"\n\n";
        }
        result += BuildTree(path, maxDepth);
    }
    
    return result;
}

TreeNode DirectoryTreeBuilder::BuildNodeTree(const std::filesystem::path& path, int currentDepth, int maxDepth) {
    TreeNode node(path.filename().wstring(), std::filesystem::is_directory(path));
    
    if (!node.isDirectory || (maxDepth >= 0 && currentDepth >= maxDepth)) {
        return node;
    }
    
    try {
        std::vector<std::filesystem::directory_entry> entries;
        
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            if (ShouldIncludePath(entry.path())) {
                entries.push_back(entry);
            }
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
            TreeNode child = BuildNodeTree(entry.path(), currentDepth + 1, maxDepth);
            node.children.push_back(std::move(child));
        }
    }
    catch (const std::exception&) {
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
    result += L"\n";
    
    std::wstring newPrefix = prefix + (isLast ? TREE_SPACE : TREE_VERTICAL);
    
    for (size_t i = 0; i < node.children.size(); ++i) {
        bool childIsLast = (i == node.children.size() - 1);
        result += RenderTree(node.children[i], newPrefix, childIsLast);
    }
    
    return result;
}

bool DirectoryTreeBuilder::ShouldIncludePath(const std::filesystem::path& path) {
    std::wstring filename = path.filename().wstring();
    
    if (filename.empty() || filename[0] == L'.') {
        if (filename == L"." || filename == L"..") {
            return false;
        }
        return true;
    }
    
    try {
        if (std::filesystem::is_directory(path)) {
            return true;
        }
        
        if (std::filesystem::is_regular_file(path)) {
            return true;
        }
    }
    catch (const std::exception&) {
        return false;
    }
    
    return false;
}

std::wstring DirectoryTreeBuilder::GetTreeSymbol(bool isLast, bool hasChildren) {
    if (isLast) {
        return hasChildren ? TREE_LAST : TREE_LAST;
    } else {
        return hasChildren ? TREE_BRANCH : TREE_BRANCH;
    }
}