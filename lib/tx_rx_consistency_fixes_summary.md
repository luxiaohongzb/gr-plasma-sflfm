# 收发一致性修复总结

## 修复内容

本次修复针对频率步进雷达模块中的收发一致性问题进行了全面改进。

### 1. 添加频率快照机制 ✅

**问题**: TX和RX线程读取频率时存在竞态条件，导致元数据中的频率与实际频率不一致。

**解决方案**:
- 添加了 `FreqSnapshot` 结构体，包含：
  - `tx_freq`, `rx_freq`: 实际频率值
  - `hop_seq`: 频率跳变序列号
  - `set_time`, `resume_time`: 时序信息
  - `lo_stabilize_duration`: LO稳定实际耗时
  - `lo_locked`: LO锁定状态

- 实现了 `get_current_snapshot()` 函数，所有线程都从快照读取频率信息
- 使用 `std::shared_ptr` 和互斥锁保护快照的访问

**文件修改**:
- `lib/usrp_radar_impl.h`: 添加 `FreqSnapshot` 结构体和相关成员变量
- `lib/usrp_radar_impl.cc`: 实现快照管理函数

### 2. 频率变量线程安全保护 ✅

**问题**: `tx_freq` 和 `rx_freq` 是普通变量，多线程访问不安全。

**解决方案**:
- 添加 `freq_mutex` 互斥锁保护频率变量
- 实现 `set_frequencies()` 函数，统一管理频率设置
- TX和RX线程通过快照读取频率，避免直接访问变量

**文件修改**:
- `lib/usrp_radar_impl.h`: 添加 `freq_mutex` 和 `set_frequencies()` 声明
- `lib/usrp_radar_impl.cc`: 实现 `set_frequencies()` 函数

### 3. 启用LO锁定检测 ✅

**问题**: LO锁定检测代码被注释掉，无法验证频率是否真正稳定。

**解决方案**:
- 实现 `wait_for_lo_stable()` 函数
- 检测TX和RX的LO锁定状态
- 如果LO未锁定，记录警告信息
- 返回实际的LO稳定状态到快照中

**文件修改**:
- `lib/usrp_radar_impl.cc`: 实现 `wait_for_lo_stable()` 函数

### 4. 改进时序同步 ✅

**问题**: `resume_time` 计算不准确，没有考虑实际LO稳定等待时间。

**解决方案**:
- 在 `hop_to_frequency()` 函数中测量实际LO稳定时间
- 基于实际等待时间计算 `resume_time`
- 将时序信息记录到快照中

**文件修改**:
- `lib/usrp_radar_impl.cc`: 实现 `hop_to_frequency()` 函数，改进时序计算

### 5. 改进TX和RX元数据关联 ✅

**问题**: TX和RX元数据中的频率可能来自不同时间点，无法准确关联。

**解决方案**:
- TX线程从快照读取频率和序列号添加到元数据
- RX线程从快照读取频率和序列号添加到元数据
- 添加 `hop_seq` 序列号用于数据关联
- 添加频率一致性验证

**文件修改**:
- `lib/usrp_radar_impl.cc`: 
  - TX线程: 使用快照设置元数据
  - RX线程: 使用快照设置元数据，添加一致性验证

### 6. 修复线程安全问题 ✅

**问题**: `tx_burst_seq_sent`, `n_tx_total`, `tx_buff_size` 不是原子类型，存在竞态条件。

**解决方案**:
- 将 `tx_burst_seq_sent` 改为 `std::atomic<uint64_t>`
- 将 `n_tx_total` 改为 `std::atomic<size_t>`
- 将 `tx_buff_size` 改为 `std::atomic<size_t>`
- 所有访问都使用原子操作（`load()`, `store()`, `fetch_add()`）

**文件修改**:
- `lib/usrp_radar_impl.h`: 修改成员变量类型
- `lib/usrp_radar_impl.cc`: 修改所有访问方式

### 7. 初始化元数据键 ✅

**问题**: `tx_freq_key`, `rx_freq_key`, `sample_start_key` 在使用前未初始化。

**解决方案**:
- 在构造函数中初始化默认值

**文件修改**:
- `lib/usrp_radar_impl.cc`: 构造函数中添加初始化

## 关键改进点

### 频率一致性保证

1. **频率设置**: 使用 `set_frequencies()` 统一设置，确保TX和RX同时设置
2. **频率读取**: 所有线程从快照读取，确保读取的是同一时刻的频率
3. **频率验证**: 设置后验证实际频率，记录到快照中

### 时序一致性保证

1. **精确计时**: 测量实际LO稳定时间
2. **同步时间**: TX和RX使用相同的 `resume_time`
3. **时序记录**: 将时序信息记录到快照中

### 数据关联保证

1. **序列号**: 每个频率跳变都有唯一的 `hop_seq`
2. **元数据**: TX和RX元数据都包含相同的序列号和频率信息
3. **一致性验证**: 检查TX/RX频率是否匹配

## 使用建议

1. **频率跳变**: 使用 `hop_to_frequency()` 函数进行频率跳变，它会自动处理所有一致性保证
2. **元数据读取**: TX和RX线程会自动从快照读取频率信息
3. **调试**: 启用 `verbose` 模式可以看到详细的频率和时序信息

## 测试建议

1. **频率一致性测试**: 检查TX和RX元数据中的频率是否匹配
2. **时序一致性测试**: 验证TX和RX是否在正确的时间窗口内工作
3. **数据关联测试**: 验证接收数据能否正确关联到对应的发射频率和序列号

## 注意事项

1. 快照在频率跳变时创建，如果频率未跳变，快照可能为 `nullptr`
2. 代码中已添加了快照为空的fallback处理
3. LO锁定检测可能在某些USRP设备上不可用，代码会优雅降级

