// ======================================================================
// \title  StringUtilsDiff.cpp
// \brief  Differential oracle for the Rust-backed Fw::StringUtils
//
// fprime-rs Phase 14. Links BOTH implementations into one binary -- the C++
// original out of a switch-OFF libFw_Types.a, and the Rust one out of
// libfprime_rust.a -- and compares them exhaustively over a small alphabet.
// They have different symbol names, so no renaming is needed; they simply
// coexist.
//
// This exists because the framework's own suite is the oracle for these
// shims, and for StringUtils that oracle has holes. It pins
// substring_find("", "") and not substring_find_last("", ""), which is
// exactly where the two searches disagree -- and exactly where the first
// draft of the Rust was wrong. Hand-chosen cases test what someone thought
// of; this tests the space.
//
// Run it with Rust/test/differential/run.sh. It is deliberately not wired
// into CI: it needs two build caches configured in opposite switch
// positions, and the drift check plus the unit suites already guard against
// regression. Its value is when a new body moves to Rust.
// ======================================================================
#include <Fw/Types/StringUtils.hpp>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
FwSizeType fprime_rs_string_length(const char*, FwSizeType);
FwSizeType fprime_rs_string_last_n_offset(const char*, FwSizeType, FwSizeType);
FwSizeType fprime_rs_string_copy(char*, const char*, FwSizeType);
FwSignedSizeType fprime_rs_substring_find(const char*, FwSizeType, const char*, FwSizeType);
FwSignedSizeType fprime_rs_substring_find_last(const char*, FwSizeType, const char*, FwSizeType);
}

static int failures = 0;
static long checks = 0;

// A buffer whose last byte abuts an unmapped page.
//
// ASan is not enough here: it only checks loads in *instrumented* code, and
// the Rust archive is built without instrumentation, so a one-byte over-read
// inside Rust is invisible to it. A PROT_NONE guard page is checked by the
// MMU, which does not care which compiler emitted the load. Two of the
// mutations this harness is meant to catch -- scanning to the bound
// inclusive, and handing string_copy one byte more than was measured --
// produce the *correct answer* while reading out of bounds, so nothing but a
// bounds trap distinguishes them.
struct Guarded {
    char* p;
    size_t n;
    char* base;
    size_t span;
    explicit Guarded(const char* data, size_t len) : n(len) {
        const size_t page = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
        span = page * 2;
        base = static_cast<char*>(::mmap(nullptr, span, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (base == MAP_FAILED) {
            std::perror("mmap");
            std::abort();
        }
        // The second page is unreadable, and the buffer ends flush against it.
        if (::mprotect(base + page, page, PROT_NONE) != 0) {
            std::perror("mprotect");
            std::abort();
        }
        p = base + page - n;
        std::memcpy(p, data, n);
    }
    explicit Guarded(const std::string& s) : Guarded(s.data(), s.size()) {}
    ~Guarded() { ::munmap(base, span); }
    Guarded(const Guarded&) = delete;
    Guarded& operator=(const Guarded&) = delete;
};

static void expect(bool ok, const char* what, const std::string& detail) {
    checks++;
    if (!ok) {
        failures++;
        if (failures <= 20) {
            std::printf("MISMATCH %s: %s\n", what, detail.c_str());
        }
    }
}

// Every string over {a, b, \0} up to `maxLen`. The NUL is in the alphabet on
// purpose: string_copy stops at one, and an interior NUL is where a bound and
// a terminator disagree.
static std::vector<std::string> corpus(size_t maxLen, bool withNul) {
    std::vector<std::string> all{""};
    std::string alphabet = withNul ? std::string("ab", 2) + std::string(1, '\0') : "ab";
    std::vector<std::string> frontier{""};
    for (size_t len = 0; len < maxLen; len++) {
        std::vector<std::string> next;
        for (const std::string& s : frontier) {
            for (char c : alphabet) {
                next.push_back(s + c);
            }
        }
        all.insert(all.end(), next.begin(), next.end());
        frontier = next;
    }
    return all;
}

int main() {
    // --- the two searches: explicit sizes, no terminator involved ----------
    const std::vector<std::string> sources = corpus(4, false);
    const std::vector<std::string> needles = corpus(3, false);
    for (const std::string& src : sources) {
        for (const std::string& sub : needles) {
            const Guarded srcX(src);
            const Guarded subX(sub);
            const FwSizeType ss = static_cast<FwSizeType>(src.size());
            const FwSizeType ns = static_cast<FwSizeType>(sub.size());
            const FwSignedSizeType cppFwd = Fw::StringUtils::substring_find(srcX.p, ss, subX.p, ns);
            const FwSignedSizeType rsFwd = fprime_rs_substring_find(srcX.p, ss, subX.p, ns);
            expect(cppFwd == rsFwd, "substring_find",
                   "\"" + src + "\" / \"" + sub + "\" cpp=" + std::to_string(cppFwd) +
                       " rust=" + std::to_string(rsFwd));
            const FwSignedSizeType cppRev = Fw::StringUtils::substring_find_last(srcX.p, ss, subX.p, ns);
            const FwSignedSizeType rsRev = fprime_rs_substring_find_last(srcX.p, ss, subX.p, ns);
            expect(cppRev == rsRev, "substring_find_last",
                   "\"" + src + "\" / \"" + sub + "\" cpp=" + std::to_string(cppRev) +
                       " rust=" + std::to_string(rsRev));
        }
    }

    // --- length, last_n and copy: terminators and bounds -------------------
    for (const std::string& raw : corpus(4, true)) {
        // A real buffer, terminated, so both sides may legitimately scan to
        // the bound; interior NULs come from the alphabet.
        const std::string buf = raw + '\0';
        for (FwSizeType bound = 0; bound <= buf.size(); bound++) {
            // Sized to the bound, not to the string: a caller passing a bound
            // it is willing to read *up to* is the normal case, and reading
            // to it is exactly what both sides are allowed to do and no more.
            const Guarded bufX(buf.substr(0, bound));
            const FwSizeType cppLen = Fw::StringUtils::string_length(bufX.p, bound);
            const FwSizeType rsLen = fprime_rs_string_length(bufX.p, bound);
            expect(cppLen == rsLen, "string_length",
                   "\"" + raw + "\" bound=" + std::to_string(bound) + " cpp=" + std::to_string(cppLen) +
                       " rust=" + std::to_string(rsLen));

            for (FwSizeType n = 0; n <= buf.size() + 1; n++) {
                const char* cppAt = Fw::StringUtils::string_last_n(bufX.p, n, bound);
                const FwSizeType rsOff = fprime_rs_string_last_n_offset(bufX.p, n, bound);
                expect(static_cast<FwSizeType>(cppAt - bufX.p) == rsOff, "string_last_n",
                       "\"" + raw + "\" n=" + std::to_string(n) + " bound=" + std::to_string(bound));
            }
        }

        for (FwSizeType num = 0; num <= 8; num++) {
            // Poisoned destinations, so a byte neither side writes still
            // compares equal and a byte only one side writes does not.
            // Destinations sized to exactly `num`, so a write past the
            // documented extent traps rather than landing in slack.
            std::string poison(static_cast<size_t>(num), 'Z');
            Guarded cppG(poison);
            Guarded rsG(poison);
            char* cppDst = cppG.p;
            char* rsDst = rsG.p;
            // The source is sized to its own content plus terminator: `num`
            // bounds the destination, and reading that far into a shorter
            // source is the over-read this is here to catch.
            const Guarded srcX(buf);
            (void)Fw::StringUtils::string_copy(cppDst, srcX.p, num);
            (void)fprime_rs_string_copy(rsDst, srcX.p, num);
            expect(std::memcmp(cppDst, rsDst, num) == 0, "string_copy",
                   "\"" + raw + "\" num=" + std::to_string(num));
        }
    }

    std::printf("%ld comparisons, %d mismatches\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
