#ifndef C74FE057_CBE3_4619_B18E_7A7AE942711F
#define C74FE057_CBE3_4619_B18E_7A7AE942711F

#include <QEvent>
#include <complex>
#include <pmt/pmt.h>
#include <vector>

static constexpr int RadarUpdateEventType = 4096;
static constexpr int RangeProfileUpdateEventType = 4097;
static constexpr int PulseCompressionUpdateEventType = 4098;
static constexpr int HRRP2DUpdateEventType = 4099;
static constexpr int FullRangeProfileUpdateEventType = 4100;

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

/**
 * @brief IFFT后的二维高分辨距离像更新事件
 * 
 * 用于传递完整的2D HRRP数据（粗距离 x 精细距离）
 */
class HRRP2DUpdateEvent : public QEvent
{
public:
    HRRP2DUpdateEvent();
    ~HRRP2DUpdateEvent() override;
    
    /**
     * @brief 设置2D HRRP数据
     * @param data 2D数据 (展平为1D, 行优先: n_coarse x n_fine)
     * @param coarse_axis 粗距离轴 (m)
     * @param fine_axis 精细距离轴 (m)
     * @param n_coarse 粗距离单元数
     * @param n_fine 精细距离单元数
     */
    void setHRRP2DData(const std::vector<double>& data,
                       const std::vector<double>& coarse_axis,
                       const std::vector<double>& fine_axis,
                       size_t n_coarse,
                       size_t n_fine);
    
    const std::vector<double>& getData() const;
    const std::vector<double>& getCoarseAxis() const;
    const std::vector<double>& getFineAxis() const;
    size_t getNumCoarse() const;
    size_t getNumFine() const;
    
    static QEvent::Type Type() { return QEvent::Type(HRRP2DUpdateEventType); }

private:
    std::vector<double> d_data;         // 2D数据 (展平)
    std::vector<double> d_coarse_axis;  // 粗距离轴
    std::vector<double> d_fine_axis;    // 精细距离轴
    size_t d_n_coarse;                  // 粗距离单元数
    size_t d_n_fine;                    // 精细距离单元数
};

/**
 * @brief 拼接后的完整一维距离像更新事件
 * 
 * 将所有粗距离单元的精细距离像拼接成一个大范围的一维距离像
 */
class FullRangeProfileUpdateEvent : public QEvent
{
public:
    FullRangeProfileUpdateEvent();
    ~FullRangeProfileUpdateEvent() override;
    
    /**
     * @brief 设置完整的一维距离像数据
     * @param range_axis 距离轴 (m)
     * @param profile 距离像幅度
     * @param fine_resolution 精细分辨率 (m)
     */
    void setFullRangeProfile(const std::vector<double>& range_axis,
                             const std::vector<double>& profile,
                             double fine_resolution);
    
    const std::vector<double>& getRangeAxis() const;
    const std::vector<double>& getProfile() const;
    double getFineResolution() const;
    
    static QEvent::Type Type() { return QEvent::Type(FullRangeProfileUpdateEventType); }

private:
    std::vector<double> d_range_axis;
    std::vector<double> d_profile;
    double d_fine_resolution;
};

#endif /* C74FE057_CBE3_4619_B18E_7A7AE942711F */
