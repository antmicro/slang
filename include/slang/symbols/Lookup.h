//------------------------------------------------------------------------------
// Lookup.h
// Contains symbol lookup-related definitions.
//
// File is under the MIT license; see LICENSE for details.
//------------------------------------------------------------------------------
#pragma once

#include "slang/diagnostics/Diagnostics.h"
#include "slang/text/SourceLocation.h"
#include "slang/util/Util.h"

namespace slang {

class Scope;
class Symbol;
class SystemSubroutine;
struct ElementSelectSyntax;

enum class SymbolIndex : uint32_t;

/// Additional modifiers for a lookup operation.
enum class LookupFlags {
    /// No special modifiers.
    None = 0,

    /// The lookup is occurring in a constant context. This adds an additional
    /// restriction that the symbols cannot be referenced by hierarchical path.
    Constant = 1,

    /// A lookup for a type name, as opposed to a value. These names cannot be hierarchical
    /// but can be package or class scoped.
    Type = 2,

    /// Usually lookups require that the found symbol be declared before the lookup
    /// location. This flag removes that restriction.
    AllowDeclaredAfter = 4,

    /// Don't search through wildcard imports to satisfy the lookup.
    DisallowWildcardImport = 8
};
BITMASK_DEFINE_MAX_ELEMENT(LookupFlags, DisallowWildcardImport);

/// This type denotes the ordering of symbols within a particular scope, for the purposes of
/// determining whether a found symbol is visible compared to the given location.
/// For example, variables cannot be referenced before they are declared.
class LookupLocation {
public:
    LookupLocation() = default;
    LookupLocation(const Scope* scope_, uint32_t index) : scope(scope_), index(index) {}

    const Scope* getScope() const { return scope; }
    SymbolIndex getIndex() const { return SymbolIndex(index); }

    /// Places a location just before the given symbol in its parent scope.
    static LookupLocation before(const Symbol& symbol);

    /// Places a location just after the given symbol in its parent scope.
    static LookupLocation after(const Symbol& symbol);

    /// A special location that should always compare after any other.
    static const LookupLocation max;

    /// A special location that should always compare before any other.
    static const LookupLocation min;

    bool operator==(const LookupLocation& other) const {
        return scope == other.scope && index == other.index;
    }

    bool operator!=(const LookupLocation& other) const { return !(*this == other); }
    bool operator<(const LookupLocation& other) const;

private:
    const Scope* scope = nullptr;
    uint32_t index = 0;
};

struct LookupResult {
    const Symbol* found = nullptr;
    const SystemSubroutine* systemSubroutine = nullptr;
    bool wasImported = false;
    bool isHierarchical = false;
    bool sawBadImport = false;

    struct MemberSelector {
        string_view name;
        SourceLocation dotLocation;
        SourceRange nameRange;
    };

    using Selector = std::variant<const ElementSelectSyntax*, MemberSelector>;
    SmallVectorSized<Selector, 4> selectors;

    Diagnostic& addDiag(const Scope& scope, DiagCode code, SourceLocation location);
    Diagnostic& addDiag(const Scope& scope, DiagCode code, SourceRange sourceRange);

    const Diagnostics& getDiagnostics() const { return diagnostics; }

    bool hasError() const;
    void clear();
    void copyFrom(const LookupResult& other);

private:
    Diagnostics diagnostics;
};

} // namespace slang