#pragma once

#include <string>

// ArchiveIoError is defined in include/openglad/platform/io.h.
// Forward declare to avoid pulling the legacy platform IO header into og::io APIs.
enum class ArchiveIoError;

namespace og::io {

ArchiveIoError zip_contents_with_error(const std::string& indirectory, const std::string& outfile);
ArchiveIoError unzip_into_with_error(const std::string& infile, const std::string& outdirectory);

} // namespace og::io

