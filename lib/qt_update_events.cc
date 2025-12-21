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

// PulseCompressionUpdateEvent implementation
PulseCompressionUpdateEvent::PulseCompressionUpdateEvent()
    : QEvent(QEvent::Type(PulseCompressionUpdateEventType)), d_frequency(0.0)
{
}

PulseCompressionUpdateEvent::~PulseCompressionUpdateEvent() {}

void PulseCompressionUpdateEvent::setPulseCompressionData(
    const std::vector<double>& range_axis,
    const std::vector<double>& profile,
    double frequency)
{
    d_range_axis = range_axis;
    d_profile = profile;
    d_frequency = frequency;
}

const std::vector<double>& PulseCompressionUpdateEvent::getRangeAxis() const
{
    return d_range_axis;
}

const std::vector<double>& PulseCompressionUpdateEvent::getProfile() const
{
    return d_profile;
}

double PulseCompressionUpdateEvent::getFrequency() const { return d_frequency; }

// HRRP2DUpdateEvent implementation
HRRP2DUpdateEvent::HRRP2DUpdateEvent()
    : QEvent(QEvent::Type(HRRP2DUpdateEventType)), d_n_coarse(0), d_n_fine(0)
{
}

HRRP2DUpdateEvent::~HRRP2DUpdateEvent() {}

void HRRP2DUpdateEvent::setHRRP2DData(const std::vector<double>& data,
                                       const std::vector<double>& coarse_axis,
                                       const std::vector<double>& fine_axis,
                                       size_t n_coarse,
                                       size_t n_fine)
{
    d_data = data;
    d_coarse_axis = coarse_axis;
    d_fine_axis = fine_axis;
    d_n_coarse = n_coarse;
    d_n_fine = n_fine;
}

const std::vector<double>& HRRP2DUpdateEvent::getData() const { return d_data; }
const std::vector<double>& HRRP2DUpdateEvent::getCoarseAxis() const { return d_coarse_axis; }
const std::vector<double>& HRRP2DUpdateEvent::getFineAxis() const { return d_fine_axis; }
size_t HRRP2DUpdateEvent::getNumCoarse() const { return d_n_coarse; }
size_t HRRP2DUpdateEvent::getNumFine() const { return d_n_fine; }

// FullRangeProfileUpdateEvent implementation
FullRangeProfileUpdateEvent::FullRangeProfileUpdateEvent()
    : QEvent(QEvent::Type(FullRangeProfileUpdateEventType)), d_fine_resolution(0.0)
{
}

FullRangeProfileUpdateEvent::~FullRangeProfileUpdateEvent() {}

void FullRangeProfileUpdateEvent::setFullRangeProfile(
    const std::vector<double>& range_axis,
    const std::vector<double>& profile,
    double fine_resolution)
{
    d_range_axis = range_axis;
    d_profile = profile;
    d_fine_resolution = fine_resolution;
}

const std::vector<double>& FullRangeProfileUpdateEvent::getRangeAxis() const { return d_range_axis; }
const std::vector<double>& FullRangeProfileUpdateEvent::getProfile() const { return d_profile; }
double FullRangeProfileUpdateEvent::getFineResolution() const { return d_fine_resolution; }