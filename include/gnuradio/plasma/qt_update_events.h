#ifndef C74FE057_CBE3_4619_B18E_7A7AE942711F
#define C74FE057_CBE3_4619_B18E_7A7AE942711F

#include <QEvent>
#include <complex>
#include <pmt/pmt.h>
#include <vector>

static constexpr int RadarUpdateEventType = 4096;
static constexpr int RangeProfileUpdateEventType = 4097;
static constexpr int PulseCompressionUpdateEventType = 4098;

class RangeDopplerUpdateEvent : public QEvent
{
public:
    RangeDopplerUpdateEvent(const double* data,
                            size_t rows,
                            size_t cols,
                            pmt::pmt_t meta);
    ~RangeDopplerUpdateEvent() override;
    double* data();
    const size_t cols();
    const size_t rows();
    const pmt::pmt_t meta();
    static QEvent::Type Type() { return QEvent::Type(RadarUpdateEventType); }

private:
    double* d_data;
    size_t d_rows;
    size_t d_cols;
    pmt::pmt_t d_meta;
};

class RangeProfileUpdateEvent : public QEvent
{
public:
    RangeProfileUpdateEvent();
    ~RangeProfileUpdateEvent() override;
    
    void setNumSamples(size_t n);
    void setRangeProfile(const std::vector<double>& range_axis, 
                        const std::vector<double>& profile);
    
    const std::vector<double>& getRangeAxis() const;
    const std::vector<double>& getProfile() const;
    size_t getNumSamples() const;
    
    static QEvent::Type Type() { return QEvent::Type(RangeProfileUpdateEventType); }

private:
    std::vector<double> d_range_axis;
    std::vector<double> d_profile;
    size_t d_num_samples;
};

class PulseCompressionUpdateEvent : public QEvent
{
public:
    PulseCompressionUpdateEvent();
    ~PulseCompressionUpdateEvent() override;
    
    void setPulseCompressionData(const std::vector<double>& range_axis,
                                 const std::vector<double>& profile,
                                 double frequency);
    
    const std::vector<double>& getRangeAxis() const;
    const std::vector<double>& getProfile() const;
    double getFrequency() const;
    
    static QEvent::Type Type() { return QEvent::Type(PulseCompressionUpdateEventType); }

private:
    std::vector<double> d_range_axis;
    std::vector<double> d_profile;
    double d_frequency;
};

#endif /* C74FE057_CBE3_4619_B18E_7A7AE942711F */
