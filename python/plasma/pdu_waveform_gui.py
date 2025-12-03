#!/usr/bin/env python3
# Simple PDU waveform GUI sink for GNU Radio
# Requires: PyQt5, pyqtgraph, numpy

import sys
import threading
import collections
import numpy as np
from PyQt5 import QtWidgets, QtCore
import pyqtgraph as pg

try:
    from gnuradio import gr
    import pmt
except Exception:
    gr = None
    pmt = None


class PduWaveformSink(gr.basic_block if gr else object):
    """GNU Radio message sink block that collects incoming PDUs into a thread-safe queue.

    Use by connecting a message port (PDU) to the block's `in` port. The GUI polls
    the queue and displays received waveforms.
    """

    def __init__(self, queue, freq_meta_key='rx_freq'):
        if gr:
            gr.basic_block.__init__(self,
                                    name='pdu_waveform_sink',
                                    in_sig=None,
                                    out_sig=None)
        self.queue = queue
        self.freq_meta_key = freq_meta_key
        if gr:
            self.message_port_register_in(pmt.intern('in'))
            self.set_msg_handler(pmt.intern('in'), self.handle_msg)

    def handle_msg(self, msg):
        # msg is a PMT pair (meta, vector)
        try:
            meta = pmt.car(msg)
            vec = pmt.cdr(msg)
        except Exception:
            return
        if not pmt.is_c32vector(vec):
            return
        n = pmt.length(vec)
        data = np.frombuffer(pmt.c32vector_elements(vec), dtype=np.complex64, count=n)
        rx_freq = None
        try:
            key = pmt.intern(self.freq_meta_key)
            f = pmt.dict_ref(meta, key, pmt.PMT_NIL)
            if f != pmt.PMT_NIL:
                rx_freq = float(pmt.to_double(f))
        except Exception:
            rx_freq = None
        # push into queue
        with threading.Lock():
            self.queue.append((rx_freq, data))


class WaveformWindow(QtWidgets.QMainWindow):
    def __init__(self, queue, show_fft=False, parent=None):
        super().__init__(parent)
        self.queue = queue
        self.show_fft = show_fft
        self.init_ui()
        self.timer = QtCore.QTimer()
        self.timer.timeout.connect(self.poll_queue)
        self.timer.start(100)  # ms

    def init_ui(self):
        self.setWindowTitle('PDU Waveform Viewer')
        w = QtWidgets.QWidget()
        self.setCentralWidget(w)
        layout = QtWidgets.QVBoxLayout()
        w.setLayout(layout)
        self.plot = pg.PlotWidget(title='Time-domain (I & Q)')
        self.plot.showGrid(x=True, y=True)
        self.curve_i = self.plot.plot(pen='r', name='I')
        self.curve_q = self.plot.plot(pen='b', name='Q')
        layout.addWidget(self.plot)
        if self.show_fft:
            self.fft_plot = pg.PlotWidget(title='Magnitude Spectrum')
            self.fft_curve = self.fft_plot.plot(pen='g')
            layout.addWidget(self.fft_plot)
        self.status = QtWidgets.QLabel('Waiting for PDUs...')
        layout.addWidget(self.status)
        self.resize(800, 600)

    def poll_queue(self):
        if not self.queue:
            return
        # pop the most recent PDU
        rx_freq, data = None, None
        try:
            while self.queue:
                rx_freq, data = self.queue.popleft()
        except Exception:
            return
        if data is None:
            return
        n = data.size
        t = np.arange(n)
        self.curve_i.setData(t, np.real(data))
        self.curve_q.setData(t, np.imag(data))
        status = f'Received {n} samples'
        if rx_freq is not None:
            status += f' @ {rx_freq/1e6:.6f} MHz'
        self.status.setText(status)
        if self.show_fft:
            # compute FFT
            window = np.hanning(n)
            spec = np.fft.fftshift(np.fft.fft(data * window))
            freqs = np.fft.fftshift(np.fft.fftfreq(n, d=1.0))
            self.fft_curve.setData(freqs, 20*np.log10(np.abs(spec)+1e-12))


def main():
    import argparse
    parser = argparse.ArgumentParser(description='PDU Waveform GUI Sink Example')
    parser.add_argument('--show-fft', action='store_true', help='Show FFT plot')
    args = parser.parse_args()

    if gr is None:
        print('GNU Radio Python libraries not found; the GUI can still be used as a viewer.')

    queue = collections.deque()

    app = QtWidgets.QApplication(sys.argv)
    win = WaveformWindow(queue, show_fft=args.show_fft)
    win.show()

    # If used as a standalone viewer, you can push numpy arrays into queue manually
    # Example: queue.append((None, np.random.randn(1024) + 1j*np.random.randn(1024)))

    sys.exit(app.exec_())


if __name__ == '__main__':
    main()
