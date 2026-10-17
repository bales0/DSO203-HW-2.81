#ifndef WILDCAT_VIEW_WINDOW_GEOMETRY_H
#define WILDCAT_VIEW_WINDOW_GEOMETRY_H
#include "CaptureWindow.h"

/* Q16 coordinates support sample indices 0..65535. These arithmetic bounds
 * are independent of physical FIFO capacity and of LCD width. */
#define VIEW_WINDOW_MAX_SAMPLES 65536u
#define VIEW_WINDOW_MAX_COLUMNS 65536u
#define VIEW_WINDOW_START_CLAMPED 1u
#define VIEW_WINDOW_SPAN_CLIPPED 2u

typedef struct {
    uint32_t capture_count, render_width, step_q16;
    uint32_t window_start_q16, window_end_q16;
    uint32_t visible_columns, flags;
} ViewWindowGeometry;
typedef struct {
    uint32_t position_q16, index, next_index, fraction_q16;
} ViewWindowSample;
typedef struct {
    uint32_t first, last; /* inclusive absolute overview pixel coordinates */
} ViewWindowOverview;

/* render_width is the actual number of waveform columns, supplied after
 * auditing Draw_Row's boundaries; it is NOT automatically MAX_X-MIN_X.
 * Requested span is (render_width-1)*step_q16. Start is clamped to fit it.
 * If even start=0 cannot fit, keep step/timebase and shorten visible_columns;
 * columns beyond that count are absent, never synthetic zero-voltage data.
 * Empty/invalid requests return 0 and clear the output (also for helpers).
 */
int ViewWindow_Build(uint32_t count,uint32_t render_width,uint32_t step_q16,
    int64_t requested_start_q16,ViewWindowGeometry *);
int ViewWindow_FromCapture(const CaptureWindow *,uint32_t render_width,
    uint32_t step_q16,ViewWindowGeometry *);
/* Place an in-record trigger/anchor at anchor_column, then apply signed
 * XPOS in temporal-sample Q16 units. Positive displacement browses later.
 * Extreme signed requests saturate before the usual start clamp.
 */
int ViewWindow_Anchor(uint32_t count,uint32_t render_width,uint32_t step_q16,
    uint32_t anchor_q16,uint32_t anchor_column,int64_t displacement_q16,
    ViewWindowGeometry *);
/* next_index equals index at integer positions; i+1 is required ONLY when
 * fraction_q16 != 0. Every successful access lies inside capture_count. */
int ViewWindow_At(const ViewWindowGeometry *,uint32_t column,ViewWindowSample *);
int ViewWindow_Delta(const ViewWindowGeometry *,uint32_t column1,uint32_t column2,
    int64_t *delta_q16);
/* A visible point occupies one sample cell: coverage is
 * [start_q16,end_q16+ONE) / (capture_count*ONE). Floor the left endpoint and
 * ceil the right endpoint, then return inclusive pixels inside the supplied
 * overview. A valid one-point window occupies at least one pixel; no-data
 * has no rectangle. This makes count==width, step==ONE cover the full bar.
 */
int ViewWindow_Overview(const ViewWindowGeometry *,uint32_t overview_x,
    uint32_t overview_width,ViewWindowOverview *);
#endif
