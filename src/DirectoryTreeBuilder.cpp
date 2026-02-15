#include "DirectoryTreeBuilder.h"
#include <algorithm>
#include <cwctype>
#include <system_error>

const wchar_t* DirectoryTreeBuilder::TREE_BRANCH = L"├── ";
const wchar_t* DirectoryTreeBuilder::TREE_LAST = L"└── ";
const wchar_t* DirectoryTreeBuilder::TREE_VERTICAL = L"│   ";
const wchar_t* DirectoryTreeBuilder::TREE_SPACE = L"    ";

DirectoryTreeBuilder::DirectoryTreeBuilder() {
}

DirectoryTreeBuilder::~DirectoryTreeBuilder() {
}

std::wstring DirectoryTreeBuilder::BuildTree(const std::wstring& rootPath, int maxDepth, TreeFormat format,
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

        if (format == TreeFormat::TEXT) {
            int processedCount = 0;
            std::unordered_set<std::wstring> visitedPaths;
            std::wstring result;
            result.reserve(8192);

            std::wstring rootName{path.filename().wstring()};
            if (rootName.empty()) {
                rootName = path.wstring();
            }

            result = std::move(rootName);
            result += L"/\r\n";

            struct SortableEntry {
                std::filesystem::directory_entry entry;
                bool isDirectory;
                std::wstring lowerName;
            };

            std::error_code ec;
            std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
            std::filesystem::directory_iterator iterator(path, options, ec);
            if (ec) {
                return result;
            }

            std::vector<SortableEntry> entries;
            for (const auto& entry : iterator) {
                if (shouldCancel && shouldCancel()) {
                    return L"Операция отменена";
                }

                std::error_code typeEc;
                const bool isEntryDirectory = entry.is_directory(typeEc) && !typeEc;
                std::wstring lowerName = entry.path().filename().wstring();
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                               [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });

                entries.push_back(SortableEntry{entry, isEntryDirectory, std::move(lowerName)});
            }

            std::sort(entries.begin(), entries.end(),
                      [](const SortableEntry& a, const SortableEntry& b) {
                          if (a.isDirectory != b.isDirectory) {
                              return a.isDirectory > b.isDirectory;
                          }
                          return a.lowerName < b.lowerName;
                      });

            for (size_t i = 0; i < entries.size(); ++i) {
                if (shouldCancel && shouldCancel()) {
                    return L"Операция отменена";
                }

                const bool isLast = (i == entries.size() - 1);
                if (!RenderTreeFromPath(entries[i].entry.path(), L"", isLast, 1, maxDepth,
                                        result, shouldCancel, progressCallback, processedCount, visitedPaths)) {
                    return L"Операция отменена";
                }
            }

            return result;
        }

        int processedCount = 0;
        std::unordered_set<std::wstring> visitedPaths;
        TreeNode root = BuildNodeTree(
            path, 0, maxDepth,
            shouldCancel, progressCallback, processedCount, visitedPaths
        );

        if (shouldCancel && shouldCancel()) {
            return L"Операция отменена";
        }

        if (format == TreeFormat::JSON) {
            return RenderTreeAsJson(root);
        }
        return RenderTreeAsXml(root);
    }
    catch (const std::exception&) {
        return L"Ошибка при построении дерева директорий";
    }
}

bool DirectoryTreeBuilder::RenderTreeFromPath(const std::filesystem::path& path,
                                              const std::wstring& prefix,
                                              bool isLast,
                                              int currentDepth,
                                              int maxDepth,
                                              std::wstring& out,
                                              std::function<bool()> shouldCancel,
                                              std::function<void(const std::wstring&)> progressCallback,
                                              int& processedCount,
                                              std::unordered_set<std::wstring>& visitedPaths) {
    if (shouldCancel && shouldCancel()) {
        return false;
    }

    std::error_code ec;
    const bool isSymlink = std::filesystem::is_symlink(path, ec) && !ec;
    ec.clear();
    const bool isDirectory = std::filesystem::is_directory(path, ec) && !ec;
    const std::wstring nodeName = path.filename().wstring();

    out += prefix;
    out += isLast ? TREE_LAST : TREE_BRANCH;
    out += nodeName;
    if (isDirectory) {
        out += L"/";
    }
    out += L"\r\n";

    ++processedCount;
    if (progressCallback && processedCount % 10 == 0) {
        std::wstring progress{L"Обработано элементов: "};
        progress.reserve(progress.length() + 10);
        progress += std::to_wstring(processedCount);
        progressCallback(progress);
    }

    if (isSymlink || !isDirectory || (maxDepth >= 0 && currentDepth >= maxDepth)) {
        return true;
    }

    std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        ec.clear();
        normalizedPath = std::filesystem::absolute(path, ec);
        if (ec) {
            normalizedPath = path;
        }
    }
    const std::wstring pathKey = normalizedPath.lexically_normal().wstring();
    if (visitedPaths.find(pathKey) != visitedPaths.end()) {
        return true;
    }
    visitedPaths.insert(pathKey);

    struct SortableEntry {
        std::filesystem::directory_entry entry;
        bool isDirectory;
        std::wstring lowerName;
    };

    std::vector<SortableEntry> entries;
    std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::directory_iterator iterator(path, options, ec);
    if (ec) {
        visitedPaths.erase(pathKey);
        return true;
    }

    for (const auto& entry : iterator) {
        if (shouldCancel && shouldCancel()) {
            visitedPaths.erase(pathKey);
            return false;
        }

        std::error_code typeEc;
        const bool isEntryDirectory = entry.is_directory(typeEc) && !typeEc;
        std::wstring lowerName = entry.path().filename().wstring();
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                       [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });

        entries.push_back(SortableEntry{entry, isEntryDirectory, std::move(lowerName)});
    }

    std::sort(entries.begin(), entries.end(),
              [](const SortableEntry& a, const SortableEntry& b) {
                  if (a.isDirectory != b.isDirectory) {
                      return a.isDirectory > b.isDirectory;
                  }
                  return a.lowerName < b.lowerName;
              });

    const std::wstring childPrefix = prefix + (isLast ? TREE_SPACE : TREE_VERTICAL);
    for (size_t i = 0; i < entries.size(); ++i) {
        if (shouldCancel && shouldCancel()) {
            visitedPaths.erase(pathKey);
            return false;
        }

        const bool childIsLast = (i == entries.size() - 1);
        if (!RenderTreeFromPath(entries[i].entry.path(), childPrefix, childIsLast,
                                currentDepth + 1, maxDepth, out, shouldCancel,
                                progressCallback, processedCount, visitedPaths)) {
            visitedPaths.erase(pathKey);
            return false;
        }
    }

    visitedPaths.erase(pathKey);
    return true;
}

TreeNode DirectoryTreeBuilder::BuildNodeTree(const std::filesystem::path& path, int currentDepth, int maxDepth,
                                                   std::function<bool()> shouldCancel,
                                                   std::function<void(const std::wstring&)> progressCallback,
                                                   int& processedCount,
                                                   std::unordered_set<std::wstring>& visitedPaths) {
    std::error_code ec;
    const bool isDirectory = std::filesystem::is_directory(path, ec) && !ec;

    std::wstring nodeName = path.filename().wstring();
    if (nodeName.empty()) {
        nodeName = path.wstring();
    }

    TreeNode node(std::move(nodeName), isDirectory);
    
    // Check for cancellation
    if (shouldCancel && shouldCancel()) {
        return node;
    }
    
    if (!node.isDirectory || (maxDepth >= 0 && currentDepth >= maxDepth)) {
        return node;
    }

    // Avoid recursive loops through symlinks/junctions and repeated reparse targets.
    std::filesystem::path normalizedPath = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        ec.clear();
        normalizedPath = std::filesystem::absolute(path, ec);
        if (ec) {
            normalizedPath = path;
        }
    }
    const std::wstring pathKey = normalizedPath.lexically_normal().wstring();
    if (visitedPaths.find(pathKey) != visitedPaths.end()) {
        return node;
    }
    visitedPaths.insert(pathKey);
    
    try {
        struct SortableEntry {
            std::filesystem::directory_entry entry;
            bool isDirectory;
            std::wstring lowerName;
        };

        std::vector<SortableEntry> entries;
        std::filesystem::directory_options options = std::filesystem::directory_options::skip_permission_denied;
        std::filesystem::directory_iterator iterator(path, options, ec);
        if (ec) {
            visitedPaths.erase(pathKey);
            return node;
        }

        for (const auto& entry : iterator) {
            // Check for cancellation during directory iteration
            if (shouldCancel && shouldCancel()) {
                visitedPaths.erase(pathKey);
                return node;
            }

            std::error_code typeEc;
            const bool isEntryDirectory = entry.is_directory(typeEc) && !typeEc;
            std::wstring lowerName = entry.path().filename().wstring();
            std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                           [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });

            entries.push_back(SortableEntry{entry, isEntryDirectory, std::move(lowerName)});
        }
        
        std::sort(entries.begin(), entries.end(), 
                 [](const SortableEntry& a, const SortableEntry& b) {
                     if (a.isDirectory != b.isDirectory) {
                         return a.isDirectory > b.isDirectory;
                     }

                     return a.lowerName < b.lowerName;
                 });
        
        node.children.reserve(entries.size());

        for (const auto& sortableEntry : entries) {
            // Check for cancellation before processing each entry
            if (shouldCancel && shouldCancel()) {
                visitedPaths.erase(pathKey);
                return node;
            }

            std::error_code symlinkEc;
            if (sortableEntry.entry.is_symlink(symlinkEc) && !symlinkEc) {
                std::error_code targetTypeEc;
                const bool targetIsDirectory = sortableEntry.entry.is_directory(targetTypeEc) && !targetTypeEc;
                node.children.emplace_back(sortableEntry.entry.path().filename().wstring(), targetIsDirectory);
                ++processedCount;
                if (progressCallback && processedCount % 10 == 0) {
                    std::wstring progress{L"Обработано элементов: "};
                    progress.reserve(progress.length() + 10);
                    progress += std::to_wstring(processedCount);
                    progressCallback(progress);
                }
                continue;
            }
            
            node.children.emplace_back(BuildNodeTree(sortableEntry.entry.path(), currentDepth + 1, maxDepth, 
                                                         shouldCancel, progressCallback, processedCount, visitedPaths));
            
            // Update progress
            ++processedCount;
            if (progressCallback && processedCount % 10 == 0) { // Report progress every 10 items
                std::wstring progress{L"Обработано элементов: "};
                progress.reserve(progress.length() + 10); // Reserve space for number
                progress += std::to_wstring(processedCount);
                progressCallback(progress);
            }
        }
    }
    catch (const std::exception&) {
        // Handle filesystem exceptions silently
    }

    visitedPaths.erase(pathKey);
    
    return node;
}

std::wstring DirectoryTreeBuilder::RenderTree(const TreeNode& node, const std::wstring& prefix, bool isLast) {
    std::wstring result;
    RenderTreeToBuffer(node, prefix, isLast, result);
    return result;
}

void DirectoryTreeBuilder::RenderTreeToBuffer(const TreeNode& node, const std::wstring& prefix, bool isLast, std::wstring& out) {
    out += prefix;
    out += isLast ? TREE_LAST : TREE_BRANCH;
    out += node.name;
    if (node.isDirectory) {
        out += L"/";
    }
    out += L"\r\n";

    std::wstring newPrefix{prefix + (isLast ? TREE_SPACE : TREE_VERTICAL)};
    for (size_t i = 0; i < node.children.size(); ++i) {
        bool childIsLast = (i == node.children.size() - 1);
        RenderTreeToBuffer(node.children[i], newPrefix, childIsLast, out);
    }
}

std::wstring DirectoryTreeBuilder::RenderTreeAsJson(const TreeNode& root, int indent) {
    std::wstring result;
    result.reserve(1024); // JSON needs more space due to structure overhead
    
    const std::wstring indentStr = GetIndent(indent);
    
    result += indentStr;
    result += L"{\r\n";
    result += indentStr;
    result += L"  \"name\": \"";
    result += EscapeJsonString(root.name);
    result += L"\",\r\n";
    result += indentStr;
    result += L"  \"type\": \"";
    result += (root.isDirectory ? L"directory" : L"file");
    result += L"\"";
    
    if (!root.children.empty()) {
        result += L",\r\n";
        result += indentStr;
        result += L"  \"children\": [\r\n";
        
        for (size_t i = 0; i < root.children.size(); ++i) {
            result += RenderTreeAsJson(root.children[i], indent + 2);
            if (i < root.children.size() - 1) {
                result += L",";
            }
            result += L"\r\n";
        }
        
        result += indentStr;
        result += L"  ]\r\n";
    } else {
        result += L"\r\n";
    }
    
    result += indentStr;
    result += L"}";
    return result;
}

std::wstring DirectoryTreeBuilder::RenderTreeAsXml(const TreeNode& root, int indent) {
    std::wstring result;
    result.reserve(512); // XML needs more space for tags and attributes
    
    if (indent == 0) {
        result += L"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n";
    }
    
    const std::wstring indentStr = GetIndent(indent);
    const std::wstring elementName = root.isDirectory ? L"directory" : L"file";
    
    result += indentStr;
    result += L"<";
    result += elementName;
    result += L" name=\"";
    result += EscapeXmlString(root.name);
    result += L"\"";
    
    if (root.children.empty()) {
        result += L"/>";
    } else {
        result += L">\r\n";
        
        for (const auto& child : root.children) {
            result += RenderTreeAsXml(child, indent + 1);
            result += L"\r\n";
        }
        
        result += indentStr;
        result += L"</";
        result += elementName;
        result += L">";
    }
    
    return result;
}

std::wstring DirectoryTreeBuilder::EscapeJsonString(const std::wstring& str) {
    std::wstring result;
    result.reserve(str.length() * 2);
    
    for (wchar_t c : str) {
        switch (c) {
            case L'"': result += L"\\\""; break;
            case L'\\': result += L"\\\\"; break;
            case L'/': result += L"\\/"; break;
            case L'\b': result += L"\\b"; break;
            case L'\f': result += L"\\f"; break;
            case L'\n': result += L"\\n"; break;
            case L'\r': result += L"\\r"; break;
            case L'\t': result += L"\\t"; break;
            default:
                if (c < 32) {
                    wchar_t unicodeEscape[10];
                    swprintf_s(unicodeEscape, L"\\u%04X", static_cast<unsigned int>(c));
                    result += unicodeEscape;
                } else {
                    result += c;
                }
                break;
        }
    }
    
    return result;
}

std::wstring DirectoryTreeBuilder::EscapeXmlString(const std::wstring& str) {
    std::wstring result;
    result.reserve(str.length() * 2);
    
    for (wchar_t c : str) {
        switch (c) {
            case L'&': result += L"&amp;"; break;
            case L'<': result += L"&lt;"; break;
            case L'>': result += L"&gt;"; break;
            case L'"': result += L"&quot;"; break;
            case L'\'': result += L"&apos;"; break;
            default: result += c; break;
        }
    }
    
    return result;
}

std::wstring DirectoryTreeBuilder::GetIndent(int level) {
    return std::wstring(level * 2, L' ');
}
