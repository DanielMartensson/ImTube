#include "core/PlaybackRate.h"
#include "core/SubtitleParser.h"
#include "core/YtDlpHelper.h"

#include <gst/gst.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                            \
    do                                                                         \
    {                                                                          \
        if (!(cond))                                                           \
        {                                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                  \
    do                                                                         \
    {                                                                          \
        const double _a = (a), _b = (b);                                       \
        if (std::fabs(_a - _b) > (eps))                                        \
        {                                                                      \
            std::printf("FAIL %s:%d: %.6f != %.6f\n", __FILE__, __LINE__, _a, _b); \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

// ---------------------------------------------------------------------------
// SubtitleParser
// ---------------------------------------------------------------------------

void test_subtitle_parse_vtt()
{
    using namespace imtube::subtitle;
    const std::string vtt =
        "WEBVTT\n"
        "Kind: captions\n"
        "Language: en\n"
        "\n"
        "00:00:01.360 --> 00:00:03.040 align:start position:0%\n"
        "<v Roger>Never gonna give you up</v>\n"
        "\n"
        "00:00:03.040 --> 00:00:04.720 align:start position:0%\n"
        "Never gonna let you down\n"
        "\n"
        "00:01:00.000 --> 00:01:02.000\n"
        "A multi\n"
        "line cue\n"
        "\n";

    const std::vector<Cue> cues = parse_text(vtt);
    CHECK(cues.size() == 3);
    if (cues.size() >= 3)
    {
        CHECK_NEAR(cues[0].start, 1.360, 1e-6);
        CHECK_NEAR(cues[0].end, 3.040, 1e-6);
        CHECK(cues[0].text == "Never gonna give you up");
        CHECK_NEAR(cues[1].start, 3.040, 1e-6);
        CHECK(cues[1].text == "Never gonna let you down");
        CHECK_NEAR(cues[2].start, 60.000, 1e-6);
        CHECK_NEAR(cues[2].end, 62.000, 1e-6);
        CHECK(cues[2].text == "A multi\nline cue");
    }
}

void test_subtitle_parse_srt()
{
    using namespace imtube::subtitle;
    // SRT uses commas for the decimal separator and HTML-ish font tags.
    const std::string srt =
        "1\n"
        "00:00:01,500 --> 00:00:04,000\n"
        "Hello, world!\n"
        "\n"
        "2\n"
        "00:00:05,000 --> 00:00:06,000\n"
        "<font color=\"#00ff00\">Green</font> text\n"
        "\n";

    const std::vector<Cue> cues = parse_text(srt);
    CHECK(cues.size() == 2);
    if (cues.size() >= 2)
    {
        CHECK_NEAR(cues[0].start, 1.5, 1e-6);
        CHECK_NEAR(cues[0].end, 4.0, 1e-6);
        CHECK(cues[0].text == "Hello, world!");
        CHECK_NEAR(cues[1].start, 5.0, 1e-6);
        CHECK(cues[1].text == "Green text");
    }
}

void test_subtitle_parse_edge_cases()
{
    using namespace imtube::subtitle;

    CHECK(parse_text("").empty());
    CHECK(parse_text("random junk\nwithout timestamps\n").empty());

    // CRLF input is tolerated.
    const std::vector<Cue> crlf = parse_text(
        "00:00:01.000 --> 00:00:02.000\r\n"
        "line\r\n"
        "\r\n");
    CHECK(crlf.size() == 1 && crlf[0].text == "line");

    // A cue whose end precedes its start is dropped.
    const std::vector<Cue> bad = parse_text(
        "00:00:05.000 --> 00:00:02.000\n"
        "backwards\n");
    CHECK(bad.empty());

    // Only a timestamp line is parsed as a cue header, not stray "--->" text.
    const std::vector<Cue> stray = parse_text(
        "arrow --> in prose\n"
        "00:00:01.000 --> 00:00:02.000\n"
        "ok\n");
    CHECK(stray.size() == 1 && stray[0].text == "ok");
}

void test_subtitle_cue_at()
{
    using namespace imtube::subtitle;
    const std::vector<Cue> cues = {
        { 1.0, 2.0, "one" },
        { 2.0, 3.0, "two" },
    };

    CHECK(cue_at(cues, 0.5) == nullptr);
    CHECK(cue_at(cues, 1.0) != nullptr); // boundaries are inclusive on start
    CHECK(cue_at(cues, 1.0)->text == "one");
    CHECK(cue_at(cues, 1.999)->text == "one");
    CHECK(cue_at(cues, 2.0)->text == "two");
    CHECK(cue_at(cues, 3.0) == nullptr); // ... and exclusive on end
}

// ---------------------------------------------------------------------------
// YtDlpHelper
// ---------------------------------------------------------------------------

void test_version_at_least()
{
    const auto vat = imtube::YtDlpHelper::version_at_least;

    CHECK(vat("2026.07.04", "2025.01.01"));
    CHECK(vat("2025.01.01", "2025.01.01"));
    CHECK(vat("2025.01.01.7", "2025.01.01"));
    CHECK(vat("2025.1.1", "2025.01.01"));
    CHECK(vat("2026.07.04", "2025.01.01.5"));
    CHECK(!vat("2024.12.31", "2025.01.01"));
    CHECK(!vat("2025.01.01", "2025.01.02"));
    CHECK(!vat("garbage", "2025.01.01"));
}

void test_format_helpers()
{
    CHECK(imtube::format_view_count(500) == "500 views");
    CHECK(imtube::format_view_count(1500) == "1.5K views");
    CHECK(imtube::format_view_count(2500000) == "2.5M views");

    CHECK(imtube::format_duration(65) == "1:05");
    CHECK(imtube::format_duration(3905) == "1:05:05");
    CHECK(imtube::format_duration(0) == "");
}

// ---------------------------------------------------------------------------
// Playback rate change (the segment math behind set_playback_speed)
// ---------------------------------------------------------------------------

void test_rate_change_segment()
{
    using imtube::rate::change_segment;

    GstSegment current;
    gst_segment_init(&current, GST_FORMAT_TIME);

    GstSegment seg;
    // From a fresh segment, a rate change at t=10s must anchor the running
    // time at 10s.
    CHECK(change_segment(current, 10 * GST_SECOND, 2.0, seg));
    CHECK_NEAR(seg.rate, 2.0, 1e-9);
    CHECK(seg.start == 10 * GST_SECOND);
    CHECK(seg.time == 10 * GST_SECOND);
    CHECK(seg.base == 10 * GST_SECOND);

    // The new segment maps its own start position back to the same running
    // time, i.e. playback is continuous across the change.
    CHECK(gst_segment_to_running_time(&seg, GST_FORMAT_TIME, seg.start) == seg.base);
    CHECK(gst_segment_to_stream_time(&seg, GST_FORMAT_TIME, seg.base) == seg.time);

    // Second change at stream time 20s: 10s were played at 2x, so the running
    // time is 10 + 10/2 = 15s.
    GstSegment seg2;
    CHECK(change_segment(seg, 20 * GST_SECOND, 0.5, seg2));
    CHECK(seg2.base == 15 * GST_SECOND);
    CHECK(seg2.rate == 0.5);

    // Decreasing the rate must keep the running time monotonic: at stream time
    // 25s under 0.5x (5s of stream at half speed), running time = 15 + 5/0.5 = 25s.
    GstSegment seg3;
    CHECK(change_segment(seg2, 25 * GST_SECOND, 1.0, seg3));
    CHECK(seg3.base == 25 * GST_SECOND);
    CHECK(seg3.rate == 1.0);
    CHECK(gst_segment_to_running_time(&seg3, GST_FORMAT_TIME, seg3.start) == seg3.base);

    // Invalid inputs are rejected.
    GstSegment bad;
    CHECK(!change_segment(current, 0, 0.0, bad));          // zero rate
    CHECK(!change_segment(current, 0, -2.0, bad));         // reverse rate
    CHECK(!change_segment(current, GST_CLOCK_TIME_NONE, 1.0, bad)); // unknown pos
    CHECK(!change_segment(seg, 5 * GST_SECOND, 1.0, bad)); // pos before segment start
}

} // namespace

int main()
{
    gst_init(nullptr, nullptr);

    test_subtitle_parse_vtt();
    test_subtitle_parse_srt();
    test_subtitle_parse_edge_cases();
    test_subtitle_cue_at();
    test_version_at_least();
    test_format_helpers();
    test_rate_change_segment();

    if (g_failures == 0)
    {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) failed.\n", g_failures);
    return 1;
}
