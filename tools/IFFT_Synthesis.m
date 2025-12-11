% IFFT_Synthesis.m
% 基于 real_data.m 的步进频 IFFT 合成高分辨距离成像 (使用真实数据)
% 
% 流程：
% 1. 读取实测二进制数据 (sweep_xxx.bin)
% 2. 脉冲压缩 (快时间维) -> 粗距离像
% 3. IFFT 合成 (慢时间/频率维) -> 高分辨距离像

close all; clear; clc;

%% 1. 参数设置 (需与实测数据参数一致)
c = 3e8;
GHz = 1e9; MHz = 1e6; us = 1e-6; KHz = 1e3;

% --- 关键雷达参数 ---
Bandwidth = 30*MHz;          
PulseWidth = 10*us;          
Fs = 60*MHz;                 
PRF = 1*KHz;
n_pulse_cpi = 11;            % 步进频点数
df = 30*MHz;                 % 频率步进量 (假设等于带宽，根据实际情况修改)

RF_freq = 2.4*GHz;
Ts = 1/Fs;

% --- 数据文件路径 ---
data_file_path = './test5/swee1p_20251211_151545_c3.bin';

%% 2. 获取回波数据
fprintf('正在读取实测数据: %s ...\n', data_file_path);

% 调用数据读取函数 (定义在脚本底部)
[Epr, freq_list] = read_sweep_collector_data(data_file_path, Fs, PRF, n_pulse_cpi);

% --- 如果读取失败，自动切换仿真数据 ---
if isempty(Epr)
    warning('无法读取实测数据，切换到仿真模式用于演示');
    
    % 仿真参数
    Range_list = [300.6, 300.8, 303];
    Velocity_list = [0, 0, 0];
    
    % 生成参考 LFM
    n_samp_pulse = round(PulseWidth*Fs);
    t_pulse = (0:n_samp_pulse-1)*Ts;
    SweepSartFreq = -Bandwidth/2;
    LFM_sim = exp(1j*2*pi*(SweepSartFreq*t_pulse + Bandwidth/(2*PulseWidth)*t_pulse.^2));
    
    [Epr, ~] = local_gen_echo(Range_list, Velocity_list, LFM_sim, Fs, c, RF_freq, Bandwidth, PulseWidth, PRF, n_pulse_cpi);
end

% 计算参数
n_pri = round(Fs / PRF);
n_samp_pulse = round(PulseWidth * Fs);

% 重排为 [快时间 x 慢时间] 矩阵
% 确保数据长度足够
expected_len = n_pri * n_pulse_cpi;
if length(Epr) < expected_len
    Epr = [Epr, zeros(1, expected_len - length(Epr))];
elseif length(Epr) > expected_len
    Epr = Epr(1:expected_len);
end

mEpr = reshape(Epr, n_pri, n_pulse_cpi); 
[N_fast, N_slow] = size(mEpr);

% plot(real(mEpr(:,1)))

%% 3. 第一步：脉冲压缩 (快时间维)
fprintf('执行脉冲压缩...\n');

% 构造参考信号 (理想 LFM)
t_pulse = (0:n_samp_pulse-1)*Ts;
SweepSartFreq = -Bandwidth/2; 
LFM_ref = exp(1j*2*pi*(SweepSartFreq*t_pulse + Bandwidth/(2*PulseWidth)*t_pulse.^2));

% 频域匹配滤波
N_fft_fast = 2^nextpow2(N_fast + n_samp_pulse); 
Ref_Spec = fft(LFM_ref, N_fft_fast);

mPC = zeros(N_fft_fast, N_slow);

for k = 1:N_slow
    Echo_Spec = fft(mEpr(:, k), N_fft_fast);
    % 匹配滤波: Echo * conj(Ref)
    PC_Spec = Echo_Spec .* conj(Ref_Spec).'; 
    mPC(:, k) = ifft(PC_Spec);
end

% 距离轴 (粗分辨率)
% 修正距离轴零点：脉压后峰值通常在 PulseWidth 处，需校准
r_coarse_axis = (0:N_fft_fast-1) * c / (2 * Fs);
res_coarse = c / (2 * Bandwidth);

figure('Name', '粗距离像 (Pulse Compression)', 'Position', [100, 100, 1000, 600]);
mesh(1:N_slow, r_coarse_axis, abs(mPC));
view(0, 90); 
title('粗距离像 (各频率点)');
xlabel('频率步进序号'); ylabel('粗距离 (m)');
ylim([0, 600]); % 根据实际目标距离调整显示范围
colorbar;

%% 4. 第二步：IFFT 合成 (高分辨成像)
fprintf('执行 IFFT 合成...\n');

% IFFT 点数 (高倍补零以获得平滑波形)
N_fft_syn = 256; 

% 核心 IFFT 操作 (对慢时间维)
HRRP_Matrix = ifft(mPC, N_fft_syn, 2); 

% 可视化二维 HRRP
% 这里的 X 轴是精细距离单元，Y 轴是粗距离单元
figure('Name', '二维高分辨结果', 'Position', [150, 150, 1000, 600]);
imagesc(1:N_fft_syn, r_coarse_axis, abs(HRRP_Matrix));
title('粗距离-精细距离 二维图');
xlabel('精细距离索引 (IFFT bins)'); ylabel('粗距离 (m)');
ylim([0, 600]); % 调整范围
colorbar;

%% 5. 提取一维高分辨剖面 (Focusing)
% 自动寻找最强目标的粗距离门
[max_val, max_idx] = max(max(abs(mPC), [], 2));

if max_val > 1e-6 % 确保有有效目标
    hrrp_1d = HRRP_Matrix(max_idx, :);
    
    % 精细距离轴
    % 最大不模糊精细范围 = c / (2 * df)
    R_fine_window = c / (2 * df);
    r_fine_axis = linspace(0, R_fine_window, N_fft_syn);
    
    figure('Name', '一维高分辨距离像', 'Position', [200, 200, 800, 400]);
    plot(r_fine_axis, abs(hrrp_1d), 'b-o', 'LineWidth', 1.5);
    grid on;
    title(sprintf('粗距离门 %.2f m 处的精细结构', r_coarse_axis(max_idx)));
    xlabel('精细距离偏移 (m)'); ylabel('幅度');
    
    fprintf('检测到目标在粗距离: %.2f m\n', r_coarse_axis(max_idx));
    fprintf('合成带宽: %.2f MHz, 理论分辨率: %.4f m\n', ...
        n_pulse_cpi * df / 1e6, c / (2 * n_pulse_cpi * df));
else
    warning('未检测到明显目标，跳过一维绘图');
end


%% --- 辅助函数：读取实测数据 (复制自 real_data.m) ---
function [Epr, freq_list] = read_sweep_collector_data(filename, Fs, PRF, n_pulse_cpi)
    Epr = []; freq_list = [];
    if ~exist(filename, 'file'), return; end
    
    fid = fopen(filename, 'rb');
    if fid == -1, return; end
    
    try
        n_freqs = fread(fid, 1, 'uint64');
        if isempty(n_freqs) || n_freqs == 0, fclose(fid); return; end
        
        n_pri = round(Fs / PRF);
        n_cpi = n_pri * n_pulse_cpi;
        freq_data_cell = cell(n_freqs, 2);
        valid_count = 0;
        
        for i = 1:n_freqs
            freq = fread(fid, 1, 'double');
            n_samples = fread(fid, 1, 'uint64');
            samples_raw = fread(fid, n_samples * 2, 'float32');
            
            if length(samples_raw) == n_samples * 2
                valid_count = valid_count + 1;
                freq_data_cell{valid_count, 1} = freq;
                freq_data_cell{valid_count, 2} = complex(samples_raw(1:2:end), samples_raw(2:2:end));
                freq_list = [freq_list, freq];
            end
        end
        fclose(fid);
        
        if valid_count == 0, return; end
        
        % 排序与重组
        [freq_list, sort_idx] = sort(freq_list);
        freq_data_cell = freq_data_cell(sort_idx, :);
        
        Epr = zeros(1, n_cpi);
        num_pulses_to_use = min(valid_count, n_pulse_cpi);
        
        for pulse_idx = 1:num_pulses_to_use
            pulse_data = freq_data_cell{pulse_idx, 2};
            if iscolumn(pulse_data), pulse_data = pulse_data.'; end
            
            % 截断或补零到 PRI 长度
            if length(pulse_data) > n_pri
                pulse_data = pulse_data(1:n_pri);
            elseif length(pulse_data) < n_pri
                pulse_data = [pulse_data, zeros(1, n_pri - length(pulse_data))];
            end
            
            start_idx = 1 + (pulse_idx - 1) * n_pri;
            Epr(start_idx : start_idx + length(pulse_data) - 1) = pulse_data;
        end
        
    catch
        fclose(fid);
        Epr = [];
    end
end

% --- 仿真回波生成 (备用) ---
function [Epr, n_pri] = local_gen_echo(Range_list, Velocity_list, LFM, Fs, c, RF_freq, Bandwidth, PulseWidth, PRF, n_pulse_cpi)
    n_samp_pulse = round(PulseWidth * Fs); 
    n_pri = round(Fs / PRF);               
    n_cpi = n_pri * n_pulse_cpi;           
    Epr = zeros(1, n_cpi);                 
    num_targets = length(Range_list);
    for k = 1:num_targets
        R0 = Range_list(k);      
        v  = Velocity_list(k);   
        Ept = zeros(1, n_cpi);
        for n = 0:n_pulse_cpi-1
            t_n = n / PRF;               
            R_n = R0 + v * t_n;          
            tao_n = 2 * R_n / c;         
            f_n = RF_freq + Bandwidth * n;  
            LFM_n = LFM .* exp(1j * 2 * pi * f_n * (-tao_n));
            start_idx = 1 + n*n_pri;
            end_idx   = start_idx + n_samp_pulse - 1;
            if end_idx > n_cpi, break; end
            Ept(start_idx:end_idx) = LFM_n;
        end
        tao0 = 2 * R0 / c;
        n_tao0 = round(tao0 * Fs);
        Ept_delay = zeros(1, n_cpi);
        if n_tao0 < n_cpi
            Ept_delay(n_tao0+1 : n_cpi) = Ept(1 : n_cpi - n_tao0);
        end
        Epr = Epr + Ept_delay;
    end
end