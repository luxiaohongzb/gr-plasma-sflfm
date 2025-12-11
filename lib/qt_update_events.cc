#include <gnuradio/plasma/qt_update_events.h>
#include <pmt/pmt.h>
#include <iostream>

RangeDopplerUpdateEvent::RangeDopplerUpdateEvent(const double* data,
                                                 size_t rows,
                                                 size_t cols,
                                                 pmt::pmt_t meta)
    : QEvent(QEvent::Type(RadarUpdateEventType))
{
    d_rows = rows;
    d_cols = cols;
    d_data = new double[rows * cols];
    memcpy(d_data, data, rows * cols * sizeof(double));
    d_meta = meta;
}

RangeDopplerUpdateEvent::~RangeDopplerUpdateEvent() { delete[] d_data; }

const size_t RangeDopplerUpdateEvent::cols() { return d_cols; }

const size_t RangeDopplerUpdateEvent::rows() { return d_rows; }

double* RangeDopplerUpdateEvent::data() { return d_data; }

const pmt::pmt_t RangeDopplerUpdateEvent::meta() { return d_meta; }

// RangeProfileUpdateEvent implementation
RangeProfileUpdateEvent::RangeProfileUpdateEvent()
    : QEvent(QEvent::Type(RangeProfileUpdateEventType)), d_num_samples(0)
{
}

RangeProfileUpdateEvent::~RangeProfileUpdateEvent() {}

void RangeProfileUpdateEvent::setNumSamples(size_t n) { d_num_samples = n; }

void RangeProfileUpdateEvent::setRangeProfile(const std::vector<double>& range_axis,
                                              const std::vector<double>& profile)
{
    d_range_axis = range_axis;
    d_profile = profile;
}

const std::vector<double>& RangeProfileUpdateEvent::getRangeAxis() const
{
    return d_range_axis;
}

const std::vector<double>& RangeProfileUpdateEvent::getProfile() const
{
    return d_profile;
}

size_t RangeProfileUpdateEvent::getNumSamples() const { return d_num_samples; }