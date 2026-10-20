#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <chrono>
#include <vector>

namespace urwtranslation {
// Read-only evidence captured for the current presentation only. A missing or
// conflicting identity leaves the source grammar ambiguous; never guess a name.
struct NpcSourceIdentity { std::string label, branch; };
struct CatalogStats {
    size_t entries = 0, enabledExact = 0, enabledTemplates = 0, disabled = 0;
    size_t documents = 0, enabledDocuments = 0;
    size_t compactUnits = 0;
    uint64_t generation = 0;
    std::string version;
    std::string encoding;
    std::string language;
};

struct DocumentEntry {
    std::string id, file, topic, headerHex, source, translation;
    bool enabled = false;
};

// UTF-8 in and out. Conversion to the bitmap font's codepage belongs to the hook.
// The game strings and the original executable are never modified by this class.
class TranslationCatalog {
public:
    using ProgressCallback = void (*)(const char* stage, size_t completed, size_t total, void* user);
    TranslationCatalog();
    // Optional diagnostic callback. Called synchronously under the catalogue lock;
    // must not re-enter this instance. Set before Initialize, clear after diagnosis.
    void SetProgressCallback(ProgressCallback callback, void* user = nullptr);
    bool Initialize(const std::wstring& root, const std::wstring& folder = L"translate");
    // Reload returns true when a valid catalogue is available, including unchanged.
    // A failed update returns false but leaves the previous catalogue usable.
    bool Reload(bool force = false);
    bool Translate(const std::string& utf8, uint32_t callerPreferredVA,
                   uint32_t targetPreferredVA, std::string& translated,
                   std::string& ruleId, bool exactOnly = false,
                   const std::vector<NpcSourceIdentity>* identities = nullptr) const;
    std::string LastError() const;
    // Per-thread reason from the most recent compact lookup; empty on success.
    std::string CompactDiagnostic() const;
    // Offline inspection of a named source domain. No hook calls this method;
    // it does not introduce a new unscoped display translation path.
    bool InspectDomain(const std::string& utf8, const std::string& domain,
                       const std::string& form, std::string& translated,
                       std::string& ruleId) const;
    CatalogStats GetStats() const;
    unsigned GetCodePage() const;
    // Returns an owned copy, including disabled documents (render their source).
    // Documents are never included in ordinary exact/template matching.
    std::vector<DocumentEntry> GetDocuments() const;
private:
    struct Snapshot;
    mutable std::mutex mutex_;
    std::shared_ptr<const Snapshot> snapshot_;
    std::wstring path_;
    std::string error_;
    uint64_t stamp_ = 0, size_ = 0, generation_ = 0;
    bool observed_ = false, lastReloadOkay_ = false;
    std::chrono::steady_clock::time_point checked_;
    ProgressCallback progress_ = nullptr;
    void* progressUser_ = nullptr;
    void Progress(const char* stage, size_t completed = 0, size_t total = 0) noexcept;
};
}
