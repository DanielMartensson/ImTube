#pragma once

#include <string>
#include <vector>

namespace imtube {
namespace subtitle {

// One timed subtitle cue. Timestamps are in seconds.
struct Cue {
    double start = 0.0;
    double end = 0.0;
    std::string text;
};

// Parses VTT or SRT subtitle data into cues. Both formats share the same
// "HH:MM:SS.mmm --> HH:MM:SS.mmm" block layout: a timestamp line followed by
// one or more text lines, then a blank line. VTT header/notes and SRT index
// lines are skipped, VTT cue settings (e.g. "align:start") and HTML tags
// (<i>) are stripped. Malformed blocks are ignored. Returns cues in file order.
std::vector<Cue> parse_text(const std::string& data);

// Reads the file and parses it with parse_text(). Returns an empty vector when
// the file cannot be read.
std::vector<Cue> parse_file(const std::string& path);

// Returns the cue covering time t (seconds), or nullptr when none matches.
const Cue* cue_at(const std::vector<Cue>& cues, double t);

} // namespace subtitle
} // namespace imtube
