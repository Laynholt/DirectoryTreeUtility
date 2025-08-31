#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <functional>
#include <atomic>

enum class TreeFormat {
    TEXT,
    JSON,
    XML
};

struct TreeNode {
    std::wstring name;
    bool isDirectory;
    std::vector<TreeNode> children;
    
    TreeNode(const std::wstring& nodeName, bool isDir) 
        : name(nodeName), isDirectory(isDir) {}
    
    TreeNode(std::wstring&& nodeName, bool isDir) 
        : name(std::move(nodeName)), isDirectory(isDir) {}
};

class DirectoryTreeBuilder {
public:
    DirectoryTreeBuilder();
    ~DirectoryTreeBuilder();

    std::wstring BuildTree(const std::wstring& rootPath, int maxDepth, TreeFormat format,
                               std::function<bool()> shouldCancel = nullptr,
                               std::function<void(const std::wstring&)> progressCallback = nullptr);

private:
    TreeNode BuildNodeTree(const std::filesystem::path& path, int currentDepth, int maxDepth,
                                std::function<bool()> shouldCancel,
                                std::function<void(const std::wstring&)> progressCallback,
                                int& processedCount);
    std::wstring RenderTree(const TreeNode& root, const std::wstring& prefix = L"", bool isLast = true);
    std::wstring RenderTreeAsJson(const TreeNode& root, int indent = 0);
    std::wstring RenderTreeAsXml(const TreeNode& root, int indent = 0);
    
    std::wstring EscapeJsonString(const std::wstring& str);
    std::wstring EscapeXmlString(const std::wstring& str);
    std::wstring GetIndent(int level);

    static const wchar_t* TREE_BRANCH;
    static const wchar_t* TREE_LAST;
    static const wchar_t* TREE_VERTICAL;
    static const wchar_t* TREE_SPACE;
};