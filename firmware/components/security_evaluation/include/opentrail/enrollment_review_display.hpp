#pragma once
// Bounded render-only cells shared by candidate enrollment and the target OLED.
// These bytes carry no confirmation, identity or session authority.
#include <array>
#include <cstddef>

namespace opentrail::security_evaluation {
struct EnrollmentReviewLayout {
    static constexpr std::size_t rows=8,columns=21;
    std::array<std::array<char,columns+1>,rows> text{};
};

// The target has an exact glyph for each admitted character. Reject unterminated
// rows and hidden trailing content instead of clipping or substituting blanks.
inline bool enrollment_review_cells_valid(const EnrollmentReviewLayout& layout) {
    for (const auto& row : layout.text) {
        bool ended=false;
        for (const char c : row) {
            if (c == 0) { ended=true; continue; }
            if (ended || !((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                            c == ' ' || c == '-' || c == ':')) return false;
        }
        if (!ended) return false;
    }
    return true;
}
} // namespace opentrail::security_evaluation
