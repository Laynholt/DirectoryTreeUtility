#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <functional>
#include <unordered_set>

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

struct BuildTreeResult {
    bool success;
    std::wstring content;
    std::wstring errorMessage;
};

class DirectoryTreeBuilder {
public:
    DirectoryTreeBuilder();
    ~DirectoryTreeBuilder();

    BuildTreeResult BuildTree(const std::wstring& rootPath, int maxDepth, TreeFormat format,
                              bool expandSymlinks = false,
                              std::function<bool()> shouldCancel = nullptr,
                              std::function<void(const std::wstring&)> progressCallback = nullptr);

private:
    bool RenderTreeFromPath(const std::filesystem::path& path,
                            const std::wstring& prefix,
                            bool isLast,
                            int currentDepth,
                            int maxDepth,
                            bool expandSymlinks,
                            std::wstring& out,
                            std::function<bool()> shouldCancel,
                            std::function<void(const std::wstring&)> progressCallback,
                            int& processedCount,
                            std::unordered_set<std::wstring>& visitedPaths);

    TreeNode BuildNodeTree(const std::filesystem::path& path, int currentDepth, int maxDepth,
                                 bool expandSymlinks,
                                 std::function<bool()> shouldCancel,
                                 std::function<void(const std::wstring&)> progressCallback,
                                 int& processedCount,
                                 std::unordered_set<std::wstring>& visitedPaths);
    void RenderTreeToBuffer(const TreeNode& node, const std::wstring& prefix, bool isLast, std::wstring& out);
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
