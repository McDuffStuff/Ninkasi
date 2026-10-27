# Turns a binary file into a C++ source file defining, in namespace
# ninkasi::embedded,
#   extern const unsigned char <symbol>[];
#   extern const std::size_t   <symbol>Size;
# so the executable carries the data and needs no files beside it.
function(ninkasi_embed_file input output symbol)
    file(READ "${input}" hex HEX)
    # 16 bytes per line (CMake regexes have no {n}, hence string(REPEAT)).
    string(REPEAT "[0-9a-f]" 32 line_pattern)
    string(REGEX REPLACE "(${line_pattern})" "\\1\n" hex "${hex}")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    get_filename_component(name "${input}" NAME)

    # file(CONFIGURE) only rewrites the output when its content changes.
    file(CONFIGURE OUTPUT "${output}" @ONLY CONTENT
"// Generated from ${name} by cmake/EmbedFile.cmake. Do not edit.
#include <cstddef>

namespace ninkasi::embedded {

extern const unsigned char ${symbol}[] = {
${bytes}
};
extern const std::size_t ${symbol}Size = sizeof(${symbol});

} // namespace ninkasi::embedded
")
    # Re-run CMake if the input changes.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
endfunction()
