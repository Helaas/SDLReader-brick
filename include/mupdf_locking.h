#ifndef MUPDF_LOCKING_H
#define MUPDF_LOCKING_H

#include <mupdf/fitz.h>

/**
 * @brief Returns a pointer to the shared MuPDF locks context.
 *
 * MuPDF's JPX loader relies on client provided locks to serialize
 * access to the global OpenJPEG allocator hooks. All contexts that
 * may run concurrently must therefore share the same lock table.
 */
const fz_locks_context* getSharedMuPdfLocks();

#endif // MUPDF_LOCKING_H
