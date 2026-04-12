#include "supported_file_types.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>

namespace
{
constexpr std::array<const char*, 1> kTextExtensions = {".txt"};
constexpr std::array<const char*, 5> kDocumentExtensions = {".pdf", ".cbz", ".cbr", ".rar", ".zip"};
constexpr std::array<const char*, 2> kReflowableExtensions = {".epub", ".mobi"};
constexpr std::array<const char*, 8> kImageExtensions = {".png", ".jpg", ".jpeg", ".gif", ".bmp", ".tif", ".tiff", ".webp"};

template <size_t N>
bool matchesExtension(const std::string& ext, const std::array<const char*, N>& candidates)
{
    return std::any_of(candidates.begin(), candidates.end(),
                       [&ext](const char* candidate)
                       { return ext == candidate; });
}
} // namespace

namespace SupportedFileTypes
{
std::string getLowercaseExtension(const std::string& path)
{
    const size_t dotPos = path.find_last_of('.');
    if (dotPos == std::string::npos)
    {
        return {};
    }

    std::string ext = path.substr(dotPos);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char ch)
                   { return static_cast<char>(std::tolower(ch)); });
    return ext;
}

bool isTextDocumentFile(const std::string& path)
{
    return matchesExtension(getLowercaseExtension(path), kTextExtensions);
}

bool isStandaloneImageFile(const std::string& path)
{
    return matchesExtension(getLowercaseExtension(path), kImageExtensions);
}

bool isMuPdfDocumentFile(const std::string& path)
{
    const std::string ext = getLowercaseExtension(path);
    return matchesExtension(ext, kDocumentExtensions) ||
           matchesExtension(ext, kReflowableExtensions) ||
           matchesExtension(ext, kImageExtensions);
}

DocumentKind classifyDocumentPath(const std::string& path)
{
    if (isTextDocumentFile(path))
    {
        return DocumentKind::Text;
    }
    if (isMuPdfDocumentFile(path))
    {
        return DocumentKind::MuPdf;
    }
    return DocumentKind::Unsupported;
}

bool isSupportedDocumentPath(const std::string& path)
{
    return classifyDocumentPath(path) != DocumentKind::Unsupported;
}

bool isSupportedFileBrowserPath(const std::string& path, bool includeStandaloneImages)
{
    if (isTextDocumentFile(path))
    {
        return true;
    }

    const std::string ext = getLowercaseExtension(path);
    if (matchesExtension(ext, kDocumentExtensions) || matchesExtension(ext, kReflowableExtensions))
    {
        return true;
    }

    return includeStandaloneImages && matchesExtension(ext, kImageExtensions);
}

std::string getSupportedFormatHelpText()
{
    return "PDF (.pdf), Comic Book Archives (.cbz, .cbr, .rar, .zip), EPUB (.epub), MOBI (.mobi), Plain Text (.txt), Images (.png, .jpg, .jpeg, .gif, .bmp, .tif, .tiff, .webp)";
}

std::string getSupportedExtensionList()
{
    std::ostringstream oss;
    oss << ".pdf, .cbz, .cbr, .rar, .zip, .epub, .mobi, .txt, .png, .jpg, .jpeg, .gif, .bmp, .tif, .tiff, .webp";
    return oss.str();
}
} // namespace SupportedFileTypes
