import argparse
import numpy as np
import pyzmq as zmq
import pmt
import matplotlib.pyplot as plt

def get_pdu(parts):
    for part in reversed(parts):
        try:
            obj = pmt.deserialize_str(part)
            if pmt.is_pair(obj):
                return pmt.car(obj), pmt.cdr(obj)
        except Exception:
            continue
    return None, None

def to_complex(data_pmt):
    vals = pmt.c32vector_elements(data_pmt)
    return np.asarray(vals, dtype=np.complex64)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--endpoint", default="tcp://127.0.0.1:5555")
    ap.add_argument("--mode", choices=["mag","i","q"], default="mag")
    ap.add_argument("--points", type=int, default=4096)
    ap.add_argument("--topic", default="")
    ap.add_argument("--rcvhwm", type=int, default=10)
    args = ap.parse_args()

    ctx = zmq.Context.instance()
    sock = ctx.socket(zmq.SUB)
    sock.setsockopt(zmq.RCVHWM, args.rcvhwm)
    sock.setsockopt_string(zmq.SUBSCRIBE, args.topic)
    sock.connect(args.endpoint)

    plt.ion()
    fig, ax = plt.subplots(figsize=(10,5))
    line, = ax.plot(np.zeros(args.points))
    ax.grid(True)

    while True:
        parts = sock.recv_multipart()
        meta, data = get_pdu(parts)
        if data is None:
            continue
        c = to_complex(data)
        y = np.abs(c) if args.mode == "mag" else (np.real(c) if args.mode == "i" else np.imag(c))
        if y.size >= args.points:
            yv = y[-args.points:]
        else:
            yv = np.pad(y, (args.points - y.size, 0))
        line.set_ydata(yv)
        ax.relim()
        ax.autoscale_view()
        fig.canvas.draw()
        fig.canvas.flush_events()

if __name__ == "__main__":
    main()