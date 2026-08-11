#include "core/PlaybackRate.h"

namespace imtube {
namespace rate {

bool change_segment(const GstSegment& current, guint64 pos, double rate, GstSegment& out)
{
    if (rate <= 0.0 || current.format != GST_FORMAT_TIME)
        return false;
    if (pos == GST_CLOCK_TIME_NONE || pos < current.start)
        return false;

    // Translate the new position to the total running time elapsed so far.
    // This is what gst_segment_do_seek() computes, done by hand because that
    // helper is unreliable when the stop time is unknown (-1), which is the
    // normal case for an unseekable stream. (For forward playback the stop
    // time is irrelevant, and gst_segment_to_running_time only checks it when
    // the rate is negative.)
    const guint64 base = gst_segment_to_running_time(&current, GST_FORMAT_TIME, pos);
    if (base == GST_CLOCK_TIME_NONE)
        return false;

    out = current;
    out.rate = rate;
    out.applied_rate = 1.0;
    out.flags = GST_SEGMENT_FLAG_NONE;
    out.start = pos;
    out.stop = current.stop; // usually GST_CLOCK_TIME_NONE (unknown duration)
    out.time = pos;
    out.position = pos;
    out.base = base;
    out.offset = 0;
    return true;
}

} // namespace rate
} // namespace imtube
