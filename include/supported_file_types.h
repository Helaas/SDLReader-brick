#ifndef SUPPORTED_FILE_TYPES_H
#define SUPPORTED_FILE_TYPES_H

#include <string>

namespace SupportedFileTypes
{
enum class DocumentKind
{
    Unsupported,
    Text,
    MuPdf
};

std::string getLowercaseExtension(const std::string& path);

bool isTextDocumentFile(const std::string& path);
bool isStandaloneImageFile(const std::string& path);
bool isMuPdfDocumentFile(const std::string& path);

DocumentKind classifyDocumentPath(const std::string& path);

bool isSupportedDocumentPath(const std::string& path);
bool isSupportedFileBrowserPath(const std::string& path, bool includeStandaloneImages);

std::string getSupportedFormatHelpText();
std::string getSupportedExtensionList();
} // namespace SupportedFileTypes

#endif // SUPPORTED_FILE_TYPES_H
