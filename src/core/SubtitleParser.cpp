#include "core/SubtitleParser.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace imtube {
namespace subtitle {

namespace {

// Strip HTML tags (<...>) from a subtitle line. VTT auto-subs mark emphasis
// with <i> / <c> and similar, which must not be shown to the user.
std::string strip_vtt_tags(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    bool in_tag = false;
    for (char c : s)
    {
        if (c == '<')
            in_tag = true;
        else if (c == '>')
            in_tag = false;
        else if (!in_tag)
            out += c;
    }
    return out;
}

// Parse a timestamp component ("HH:MM:SS.mmm", "MM:SS.mmm" or "SS.mmm").
// VTT/SRT use a comma as the decimal separator on the right side of "--->",
// so both "," and "." are accepted.
bool parse_ts(const std::string& s, double& out)
{
    std::vector<double> parts;
    size_t pos = 0;
    while (pos < s.size())
    {
        size_t colon = s.find(':', pos);
        std::string part = (colon == std::string::npos) ? s.substr(pos) : s.substr(pos, colon - pos);
        for (char& c : part)
            if (c == ',')
                c = '.';
        char* end = nullptr;
        const double v = strtod(part.c_str(), &end);
        if (end == part.c_str() || *end != '\0')
            return false;
        parts.push_back(v);
        if (colon == std::string::npos)
            break;
        pos = colon + 1;
    }
    if (parts.empty() || parts.size() > 3)
        return false;
    if (parts.size() == 3)
        out = parts[0] * 3600.0 + parts[1] * 60.0 + parts[2];
    else if (parts.size() == 2)
        out = parts[0] * 60.0 + parts[1];
    else
        out = parts[0];
    return true;
}

void trim(std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b]))
        b++;
    while (e > b && std::isspace((unsigned char)s[e - 1]))
        e--;
    s = s.substr(b, e - b);
}

} // namespace

std::vector<Cue> parse_text(const std::string& data)
{
    std::vector<Cue> cues;

    std::string normalized = data;
    for (char& c : normalized)
        if (c == '\r')
            c = ' ';

    std::istringstream stream(normalized);
    std::string line;
    double start = -1.0, end = -1.0;
    std::string cue_text;

    auto flush = [&]() {
        if (start >= 0.0 && end >= start && !cue_text.empty())
            cues.push_back({ start, end, cue_text });
        start = end = -1.0;
        cue_text.clear();
    };

    while (std::getline(stream, line))
    {
        trim(line);
        if (line.empty())
        {
            flush();
            continue;
        }

        const size_t arrow = line.find("-->");
        if (arrow != std::string::npos)
        {
            flush();
            double a = 0.0, b = 0.0;
            std::string lhs = line.substr(0, arrow);
            trim(lhs); // the arrow may be preceded by whitespace
            const std::string rhs = line.substr(arrow + 3);
            if (!parse_ts(lhs, a))
                continue;
            // The right side may carry cue settings after whitespace:
            // "... --> 00:00:05.000 align:start position:10%".
            std::istringstream iss(rhs);
            std::string tok;
            if (!(iss >> tok) || !parse_ts(tok, b))
                continue;
            start = a;
            end = b;
        }
        else if (start >= 0.0)
        {
            if (!cue_text.empty())
                cue_text += "\n";
            cue_text += strip_vtt_tags(line);
        }
        // Header ("WEBVTT"), SRT index lines ("1") and NOTE blocks are skipped.
    }
    flush();
    return cues;
}

std::vector<Cue> parse_file(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (f == nullptr)
        return {};
    std::string data;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        data.append(buf, n);
    fclose(f);
    return parse_text(data);
}

const Cue* cue_at(const std::vector<Cue>& cues, double t)
{
    for (const Cue& c : cues)
    {
        if (t >= c.start && t < c.end)
            return &c;
    }
    return nullptr;
}

} // namespace subtitle
} // namespace imtube
