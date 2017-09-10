//------------------------------------------------------------------------------
// SourceManager.h
// Source file management.
//
// File is under the MIT license; see LICENSE for details.
//------------------------------------------------------------------------------
#pragma once

#include <deque>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "util/BumpAllocator.h"
#include "util/Path.h"
#include "util/SmallVector.h"

#include "SourceLocation.h"

namespace slang {

/// Represents a source buffer; that is, the actual text of the source
/// code along with an identifier for the buffer which potentially
/// encodes its include stack.
struct SourceBuffer {
    string_view data;
    BufferID id;

    explicit operator bool() const { return id.valid(); }
};

/// SourceManager - Handles loading and tracking source files.
///
/// The source manager abstracts away the differences between
/// locations in files and locations generated by macro expansion.
/// See SourceLocation for more details.
///
/// TODO: The methods in this class should be thread safe.
class SourceManager {
public:
    SourceManager();
    SourceManager(const SourceManager&) = delete;
    SourceManager& operator=(const SourceManager&) = delete;

    /// Convert the given relative path into an absolute path.
    std::string makeAbsolutePath(string_view path) const;

    /// Adds a system include directory.
    void addSystemDirectory(string_view path);

    /// Adds a user include directory.
    void addUserDirectory(string_view path);

    /// Gets the source line number for a given source location.
    uint32_t getLineNumber(SourceLocation location) const;

    /// Gets the source file name for a given source location
    string_view getFileName(SourceLocation location) const;

    /// Gets the column line number for a given source location.
    /// @a location must be a file location.
    uint32_t getColumnNumber(SourceLocation location) const;

    /// Gets a location that indicates from where the given buffer was included.
    /// @a location must be a file location.
    SourceLocation getIncludedFrom(BufferID buffer) const;

    /// Determines whether the given location exists in a source file.
    bool isFileLoc(SourceLocation location) const;

    /// Determines whether the given location points to a macro expansion.
    bool isMacroLoc(SourceLocation location) const;

    /// Determines whether the given location is inside an include file.
    bool isIncludedFileLoc(SourceLocation location) const;

    /// Determines whether the @param left location comes before the @param right location
    /// within the "compilation unit space", which is a hypothetical source space where
    /// all macros and include files have been expanded out into a flat file.
    bool isBeforeInCompilationUnit(SourceLocation left, SourceLocation right) const;

    /// Gets the expansion location of a given macro location.
    SourceLocation getExpansionLoc(SourceLocation location) const;

    /// Gets the expansion range of a given macro location.
    SourceRange getExpansionRange(SourceLocation location) const;

    /// Gets the original source location of a given macro location.
    SourceLocation getOriginalLoc(SourceLocation location) const;

    /// If the given location is a macro location, fully expands it out to its actual
    /// file expansion location. Otherwise just returns the location itself.
    SourceLocation getFullyExpandedLoc(SourceLocation location) const;

    /// Gets the actual source text for a given file buffer.
    string_view getSourceText(BufferID buffer) const;

    /// Creates a macro expansion location; used by the preprocessor.
    SourceLocation createExpansionLoc(SourceLocation originalLoc, SourceLocation expansionStart,
                                      SourceLocation expansionEnd);

    /// Instead of loading source from a file, copy it from text already in memory.
    SourceBuffer assignText(string_view text, SourceLocation includedFrom = SourceLocation());

    /// Instead of loading source from a file, copy it from text already in memory.
    /// Pretend it came from a file located at @a path.
    SourceBuffer assignText(string_view path, string_view text, SourceLocation includedFrom = SourceLocation());

    /// Pretend that the given text has been appended to the specified buffer.
    /// This is mostly for testing purposes.
    SourceBuffer appendText(BufferID buffer, string_view text);

    /// Instead of loading source from a file, move it from text already in memory.
    /// Pretend it came from a file located at @a path.
    SourceBuffer assignBuffer(string_view path, Vector<char>&& buffer, SourceLocation includedFrom = SourceLocation());

    /// Read in a source file from disk.
    SourceBuffer readSource(string_view path);

    /// Read in a header file from disk.
    SourceBuffer readHeader(string_view path, SourceLocation includedFrom, bool isSystemPath);

    /// Adds a line directive at the given location.
    void addLineDirective(SourceLocation location, uint32_t lineNum, string_view name, uint8_t level);

private:
    BumpAllocator alloc;
    uint32_t unnamedBufferCount = 0;

    // Stores actual file contents and metadata; only one per loaded file
    struct FileData {
        Vector<char> mem;                   // file contents
        std::string name;                   // name of the file
        std::vector<uint32_t> lineOffsets;  // char offset for each line

        struct LineDirectiveInfo {
            uint32_t lineInFile;        // Actual file line where directive occurred
            uint32_t lineOfDirective;   // Line number set by directive
            std::string name;           // File name set by directive
            uint8_t level;              // level of directive

            LineDirectiveInfo(uint32_t lif, uint32_t lod, string_view fname, uint8_t _level) :
                lineInFile(lif), lineOfDirective(lod), name(fname.begin(), fname.end()),
                level(_level) {}
        };

        // Returns a pointer to the LineDirectiveInfo for the nearest enclosing
        // line directive of the given raw line number, or nullptr if there is none
        const LineDirectiveInfo* getPreviousLineDirective(uint32_t rawLineNumber) const;

        struct LineDirectiveComparator {
            bool operator()(const LineDirectiveInfo& info1, const LineDirectiveInfo& info2) {
                return info1.lineInFile < info2.lineInFile;
            }
        };

        std::vector<LineDirectiveInfo> lineDirectives; // info about `line in file
        const Path* directory;                         // directory that the file exists in

        FileData(const Path* directory, const std::string& name, SmallVector<char>&& data) :
            mem(std::move(data)),
            name(name),
            directory(directory) {}
    };

    // Stores a pointer to file data along with information about where we included it.
    // There can potentially be many of these for a given file.
    struct FileInfo {
        FileData* data;
        SourceLocation includedFrom;

        FileInfo() {}
        FileInfo(FileData* data, SourceLocation includedFrom) :
            data(data), includedFrom(includedFrom) {}
    };

    // Instead of a file, this lets a BufferID point to a macro expansion location.
    // This is actually used two different ways; if this is a normal token from a
    // macro expansion, originalLocation will point to the token inside the macro
    // definition, and expansionLocation will point to the range of the macro usage
    // the expansion site. Alternatively, if this token came from an argument,
    // originalLocation will point to the argument at the expansion site and
    // expansionLocation will point to the parameter inside the macro body.
    struct ExpansionInfo {
        SourceLocation originalLoc;
        SourceLocation expansionStart;
        SourceLocation expansionEnd;

        ExpansionInfo() {}
        ExpansionInfo(SourceLocation originalLoc, SourceLocation expansionStart, SourceLocation expansionEnd) :
            originalLoc(originalLoc), expansionStart(expansionStart), expansionEnd(expansionEnd) {}
    };

    // index from BufferID to buffer metadata
    std::deque<std::variant<FileInfo, ExpansionInfo>> bufferEntries;

    // cache for file lookups; this holds on to the actual file data
    std::unordered_map<std::string, std::unique_ptr<FileData>> lookupCache;

    // directories for system and user includes
    std::vector<Path> systemDirectories;
    std::vector<Path> userDirectories;

    // uniquified backing memory for directories
    std::set<Path> directories;

    FileData* getFileData(BufferID buffer) const;
    SourceBuffer createBufferEntry(FileData* fd, SourceLocation includedFrom);

    SourceBuffer openCached(const Path& fullPath, SourceLocation includedFrom);
    SourceBuffer cacheBuffer(std::string&& canonicalPath, const Path& path, SourceLocation includedFrom, Vector<char>&& buffer);

    static void computeLineOffsets(const Vector<char>& buffer, std::vector<uint32_t>& offsets);
    static bool readFile(const Path& path, Vector<char>& buffer);

    // Get raw line number of a file location, ignoring any line directives
    uint32_t getRawLineNumber(SourceLocation location) const;
};

}
