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

        int processedCount = 0;
        TreeNode root = BuildNodeTree(
            path, 0, maxDepth,
            shouldCancel, progressCallback, processedCount
        );
        
        if (shouldCancel && shouldCancel()) {
            return L"Операция отменена";
        }

        switch (format) {
            case TreeFormat::JSON:
                return RenderTreeAsJson(root);
            case TreeFormat::XML:
                return RenderTreeAsXml(root);
            case TreeFormat::TEXT:
            default:
                {
                    std::wstring result;
                    result.reserve(8192); // Pre-allocate reasonable size
                    
                    std::wstring rootName{path.filename().wstring()};
                    if (rootName.empty()) {
                        rootName = path.wstring();
                    }
                    result = std::move(rootName);
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
        }
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
                     
                     std::wstring aName{a.path().filename().wstring()};
                     std::wstring bName{b.path().filename().wstring()};
                     std::transform(aName.begin(), aName.end(), aName.begin(), ::towlower);
                     std::transform(bName.begin(), bName.end(), bName.begin(), ::towlower);
                     
                     return aName < bName;
                 });
        
        for (const auto& entry : entries) {
            // Check for cancellation before processing each entry
            if (shouldCancel && shouldCancel()) {
                return node;
            }
            
            node.children.emplace_back(BuildNodeTree(entry.path(), currentDepth + 1, maxDepth, 
                                                         shouldCancel, progressCallback, processedCount));
            
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
    
    std::wstring newPrefix{prefix + (isLast ? TREE_SPACE : TREE_VERTICAL)};
    
    for (size_t i = 0; i < node.children.size(); ++i) {
        bool childIsLast = (i == node.children.size() - 1);
        result += RenderTree(node.children[i], newPrefix, childIsLast);
    }
    
    return result;
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