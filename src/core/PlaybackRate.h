#pragma once

#include <gst/gst.h>

namespace imtube {
namespace rate {

// The stream fed from yt-dlp's stdout is not seekable, so the playback rate can
// only be changed by pushing a fresh TIME SEGMENT onto the decoded branches:
// the running time keeps increasing and only the mapping to stream time changes.
//
// Builds such a segment for "keep playing from stream time 'pos' at 'rate'",
// following on from 'current', which records how much running time has elapsed
// so far (in current.base). Returns false when the change is impossible for the
// given position (e.g. pos lies before the current segment start).
bool change_segment(const GstSegment& current, guint64 pos, double rate, GstSegment& out);

} // namespace rate
} // namespace imtube
