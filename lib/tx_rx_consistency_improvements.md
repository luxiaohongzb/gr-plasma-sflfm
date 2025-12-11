# 收发一致性改进方案

## 核心问题总结

频率步进雷达的收发一致性主要涉及：

1. **频率同步**: TX和RX必须在同一频率上工作
2. **时序同步**: TX和RX必须在正确的时间窗口内工作  
3. **数据关联**: 接收数据必须能正确关联到对应的发射参数
4. **元数据一致性**: TX和RX的元数据必须匹配

## 当前代码的主要问题

### 问题1: 频率变量的线程安全问题

**当前代码**:
```cpp
double tx_freq, rx_freq;  // 普通变量，非线程安全

// 主线程修改
tx_freq = f;
rx_freq = f;

// TX线程读取（可能同时发生）
next_meta = pmt::dict_add(..., pmt::from_double(tx_freq));

// RX线程读取（可能同时发生）
double dev_rx = usrp->get_rx_freq();  // 从设备读取，但tx_freq可能已改变
```

**改进方案**:
```cpp
// 方案A: 使用原子类型（适用于简单场景）
std::atomic<double> tx_freq, rx_freq;

// 方案B: 使用互斥锁（推荐，更灵活）
std::mutex freq_mutex;
double tx_freq, rx_freq;

void set_frequencies(double f) {
    std::lock_guard<std::mutex> lk(freq_mutex);
    tx_freq = f;
    rx_freq = f;
    usrp->set_tx_freq(f);
    usrp->set_rx_freq(f);
}

double get_tx_freq() {
    std::lock_guard<std::mutex> lk(freq_mutex);
    return tx_freq;
}
```

### 问题2: 频率快照机制（推荐方案）

**核心思想**: 在频率跳变时创建一个不可变的快照，所有线程都从这个快照读取。

```cpp
// 在头文件中添加
struct FreqSnapshot {
    double tx_freq;
    double rx_freq;
    uint64_t hop_seq;           // 频率跳变序列号
    double set_time;            // 频率设置时间
    double resume_time;          // 恢复时间
    bool lo_locked;              // LO是否锁定
    
    FreqSnapshot(double tx, double rx, uint64_t seq, double set_t, double resume_t)
        : tx_freq(tx), rx_freq(rx), hop_seq(seq), 
          set_time(set_t), resume_time(resume_t), lo_locked(false) {}
};

std::shared_ptr<FreqSnapshot> current_freq_snapshot;
std::mutex snapshot_mutex;

// 在频率跳变时
void hop_to_frequency(double f) {
    // 设置频率
    usrp->set_command_time(uhd::time_spec_t(t_cmd));
    usrp->set_tx_freq(f);
    usrp->set_rx_freq(f);
    usrp->clear_command_time();
    
    // 等待LO稳定
    wait_for_lo_stable();
    
    // 验证频率
    double actual_tx = usrp->get_tx_freq();
    double actual_rx = usrp->get_rx_freq();
    
    // 创建快照
    double now = usrp->get_time_now().get_real_secs();
    double resume_t = now + 0.05;
    uint64_t seq = tx_burst_seq.load() + 1;
    
    {
        std::lock_guard<std::mutex> lk(snapshot_mutex);
        current_freq_snapshot = std::make_shared<FreqSnapshot>(
            actual_tx, actual_rx, seq, now, resume_t);
    }
    
    // 更新resume_time
    resume_time = resume_t;
    tx_burst_seq.fetch_add(1);
}

// TX线程使用快照
auto snapshot = get_current_snapshot();
next_meta = pmt::dict_add(next_meta, 
    pmt::intern(tx_freq_key), 
    pmt::from_double(snapshot->tx_freq));
next_meta = pmt::dict_add(next_meta,
    pmt::intern("hop_seq"),
    pmt::from_uint64(snapshot->hop_seq));

// RX线程使用快照
auto snapshot = get_current_snapshot();
meta = pmt::dict_add(meta,
    pmt::intern(rx_freq_key),
    pmt::from_double(snapshot->rx_freq));
meta = pmt::dict_add(meta,
    pmt::intern("hop_seq"),
    pmt::from_uint64(snapshot->hop_seq));
```

### 问题3: 改进时序同步

**当前问题**: `resume_time` 计算不准确，没有考虑实际等待时间。

```cpp
// 改进后的时序同步
void hop_to_frequency(double f) {
    double t_start = usrp->get_time_now().get_real_secs();
    double t_cmd = t_start + 0.02;
    
    // 设置频率
    usrp->set_command_time(uhd::time_spec_t(t_cmd));
    usrp->set_tx_freq(f);
    usrp->set_rx_freq(f);
    usrp->clear_command_time();
    
    // 等待LO稳定（实际等待时间）
    double t_before_wait = usrp->get_time_now().get_real_secs();
    wait_for_lo_stable();
    double t_after_wait = usrp->get_time_now().get_real_secs();
    double actual_wait = t_after_wait - t_before_wait;
    
    // 计算resume_time，考虑实际等待时间
    double t_now = usrp->get_time_now().get_real_secs();
    resume_time = t_now + 0.05;  // 额外50ms缓冲
    
    // 记录时序信息到快照
    snapshot->set_time = t_start;
    snapshot->resume_time = resume_time;
    snapshot->lo_stabilize_duration = actual_wait;
}
```

### 问题4: 启用LO锁定检测

**当前代码**: LO锁定检测被注释掉了。

```cpp
// 改进后的LO锁定检测
bool wait_for_lo_stable() {
    auto deadline = std::chrono::steady_clock::now() + 
                   std::chrono::duration<double>(lo_stabilize_time);
    
    bool tx_locked = false, rx_locked = false;
    int check_count = 0;
    const int max_checks = static_cast<int>(lo_stabilize_time * 200); // 每5ms检查一次
    
    while (std::chrono::steady_clock::now() < deadline && check_count < max_checks) {
        // 检查TX LO
        try {
            tx_locked = usrp->get_tx_sensor("lo_locked").to_bool();
        } catch (...) {
            tx_locked = false;
        }
        
        // 检查RX LO
        try {
            rx_locked = usrp->get_rx_sensor("lo_locked").to_bool();
        } catch (...) {
            rx_locked = false;
        }
        
        if (tx_locked && rx_locked) {
            if (verbose) {
                std::cout << "[usrp_radar] LO locked after " 
                         << (check_count * 5) << " ms" << std::endl;
            }
            return true;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        check_count++;
    }
    
    if (verbose && (!tx_locked || !rx_locked)) {
        std::cerr << "[usrp_radar] WARNING: LO not locked after timeout. "
                  << "TX locked: " << tx_locked 
                  << ", RX locked: " << rx_locked << std::endl;
    }
    
    return tx_locked && rx_locked;
}
```

### 问题5: 改进元数据关联

**添加更多关联信息**:

```cpp
// TX元数据
void prepare_tx_metadata() {
    auto snapshot = get_current_snapshot();
    
    next_meta = pmt::dict_add(next_meta, 
        pmt::intern(tx_freq_key), 
        pmt::from_double(snapshot->tx_freq));
    next_meta = pmt::dict_add(next_meta,
        pmt::intern("hop_seq"),
        pmt::from_uint64(snapshot->hop_seq));
    next_meta = pmt::dict_add(next_meta,
        pmt::intern("tx_timestamp"),
        pmt::from_double(snapshot->set_time));
    next_meta = pmt::dict_add(next_meta,
        pmt::intern(sample_start_key),
        pmt::from_long(n_tx_total));
}

// RX元数据
void prepare_rx_metadata() {
    auto snapshot = get_current_snapshot();
    double rx_timestamp = usrp->get_time_now().get_real_secs();
    
    pmt::pmt_t meta = this->next_meta;
    
    // 添加RX频率（使用快照中的值，确保一致性）
    meta = pmt::dict_add(meta,
        pmt::intern(rx_freq_key),
        pmt::from_double(snapshot->rx_freq));
    
    // 添加关联信息
    meta = pmt::dict_add(meta,
        pmt::intern("hop_seq"),
        pmt::from_uint64(snapshot->hop_seq));
    
    meta = pmt::dict_add(meta,
        pmt::intern("rx_timestamp"),
        pmt::from_double(rx_timestamp));
    
    // 验证TX和RX频率是否匹配
    if (std::abs(snapshot->tx_freq - snapshot->rx_freq) > 1.0) {
        if (verbose) {
            std::cerr << "[usrp_radar] WARNING: TX/RX frequency mismatch: "
                     << "TX=" << snapshot->tx_freq 
                     << " RX=" << snapshot->rx_freq << std::endl;
        }
    }
    
    message_port_pub(PMT_OUT, pmt::cons(meta, rx_data_pmt));
}
```

## 完整的改进流程

1. **频率跳变时**:
   - 设置频率（使用command_time确保同步）
   - 等待LO稳定（验证锁定状态）
   - 验证实际频率
   - 创建频率快照
   - 更新resume_time

2. **TX线程**:
   - 从快照读取频率信息
   - 将频率和序列号添加到元数据
   - 在resume_time发射

3. **RX线程**:
   - 从快照读取频率信息
   - 在resume_time开始接收
   - 将频率和序列号添加到元数据
   - 验证TX/RX频率一致性

## 测试验证

```cpp
// 测试函数：验证收发一致性
void verify_tx_rx_consistency() {
    // 1. 检查频率一致性
    auto snapshot = get_current_snapshot();
    double tx_freq = snapshot->tx_freq;
    double rx_freq = snapshot->rx_freq;
    assert(std::abs(tx_freq - rx_freq) < 1.0);  // 允许1Hz误差
    
    // 2. 检查时序一致性
    double now = usrp->get_time_now().get_real_secs();
    assert(std::abs(now - snapshot->resume_time) < 0.1);  // 100ms误差
    
    // 3. 检查LO锁定
    assert(snapshot->lo_locked);
}
```

## 实施优先级

1. **第一阶段**: 实现频率快照机制和互斥锁保护
2. **第二阶段**: 启用LO锁定检测和改进时序同步
3. **第三阶段**: 添加详细的元数据关联和验证

