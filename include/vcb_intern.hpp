// VCB — Vayu Compiler Backend
// vcb_intern.hpp — fast string interner for symbolic value names.
//
// Design:
//   * Open addressing, linear probing, power-of-two capacity.
//   * FNV-1a hash (fast, good distribution for short identifiers).
//   * Strings stored in a linear arena; IDs are stable forever.
//   * Single allocation per unique string; no per-lookup allocations.
//
// Target: ~10ns per intern() on x86-64 for identifiers under 16 bytes.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace vcb {

    class Interner {
    public:
        static constexpr uint32_t INVALID = 0xFFFFFFFFu;

        Interner() { slots_.assign(64, INVALID); }

        uint32_t intern(std::string_view s) {
            if (s.empty()) return INVALID;
            uint32_t h = fnv1a(s) & mask();
            while (slots_[h] != INVALID) {
                if (strings_[slots_[h]] == s) return slots_[h];
                h = (h + 1) & mask();
            }
            uint32_t id = static_cast<uint32_t>(strings_.size());
            strings_.emplace_back(s);
            slots_[h] = id;
            if ((strings_.size() << 1) > slots_.size()) grow();
            return id;
        }

        const std::string& get(uint32_t id) const { return strings_[id]; }
        uint32_t size() const { return static_cast<uint32_t>(strings_.size()); }

    private:
        std::vector<uint32_t>    slots_;
        std::vector<std::string> strings_;

        uint32_t mask() const { return static_cast<uint32_t>(slots_.size()) - 1; }

        static uint32_t fnv1a(std::string_view s) {
            uint32_t h = 2166136261u;
            for (char c : s) {
                h ^= static_cast<uint8_t>(c);
                h *= 16777619u;
            }
            return h;
        }

        void grow() {
            std::vector<uint32_t> old = std::move(slots_);
            slots_.assign(old.size() << 1, INVALID);
            for (uint32_t id : old) {
                if (id == INVALID) continue;
                uint32_t h = fnv1a(strings_[id]) & mask();
                while (slots_[h] != INVALID) h = (h + 1) & mask();
                slots_[h] = id;
            }
        }
    };

} // namespace vcb