#pragma once

#include "document.h" // Include the base Document interface
#include <memory>      // For std::unique_ptr

// Forward declarations from MuPDF
// Removed forward declarations for fz_rect and fz_matrix to avoid redefinition conflicts.
// These types will be fully defined when mupdf/fitz.h is included in the .cpp file.
typedef struct fz_context fz_context;
typedef struct fz_document fz_document;
typedef struct fz_page fz_page;
typedef struct fz_pixmap fz_pixmap;
// typedef struct fz_rect fz_rect;    // REMOVED
// typedef struct fz_matrix fz_matrix; // REMOVED

// Custom deleter for fz_context.
// This struct defines the function call operator that will be used by std::unique_ptr
// to correctly release the fz_context resource when the unique_ptr goes out of scope.
struct FzContextDeleter {
    void operator()(fz_context* ctx) const; // Definition in .cpp
};

// --- PdfDocument Class (MuPDF Implementation) ---
// Concrete implementation of the Document interface for PDF files using MuPDF.
class PdfDocument : public Document {
public:
    // Constructor initializes member pointers to nullptr.
    PdfDocument();

    // Destructor overrides the base class destructor.
    // It's responsible for explicitly dropping the fz_document.
    ~PdfDocument() override;

    // Implements the open method from the Document interface.
    bool open(const std::string& filename) override;

    // Implements the getPageCount method from the Document interface.
    int getPageCount() const override;

    // Implements the renderPage method from the Document interface.
    std::vector<uint8_t> renderPage(int pageNum, int& outWidth, int& outHeight, int scale) override;

private:
    // Smart pointer for MuPDF context, using a custom deleter for proper resource management.
    std::unique_ptr<fz_context, FzContextDeleter> m_ctx;
    // Raw pointer for MuPDF document. Managed manually in destructor because
    // fz_drop_document requires the fz_context, which unique_ptr would drop first.
    fz_document* m_doc;
};
