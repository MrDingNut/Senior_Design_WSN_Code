import os
import sys
import serial
import serial.tools.list_ports
import json
import csv
import threading
import tkinter as tk
from tkinter import ttk
from datetime import datetime, timedelta
from collections import deque
import matplotlib.pyplot as plt
import matplotlib.animation as animation
import matplotlib.dates as mdates
from matplotlib.widgets import Button

# --- Configuration ---
BAUD_RATE = 115200

# Keywords that identify ESP32 USB-to-serial chips
ESP32_KEYWORDS = ['cp210', 'ch340', 'ch341', 'ftdi', 'usb serial', 'uart', 'esp32', 'silicon labs']

# (json_key, display_label, unit, line_color)
PARAMS = [
    ('moisture', 'Moisture',           '%',     'tab:blue'),
    ('ph',       'pH',                 '',      'tab:green'),
    ('ec',       'Elec. Conductivity', 'mS/cm', 'tab:orange'),
    ('temp',     'Temperature',        '°C',    'tab:red'),
    ('n',        'Nitrogen (N)',        'mg/kg', 'tab:purple'),
    ('p',        'Phosphorus (P)',      'mg/kg', 'tab:brown'),
    ('k',        'Potassium (K)',       'mg/kg', 'tab:pink'),
]

# (label, timedelta or None for all time)
TIME_RANGES = [
    ('24h',      timedelta(hours=24)),
    ('3 Days',   timedelta(days=3)),
    ('1 Week',   timedelta(days=7)),
    ('30 Days',  timedelta(days=30)),
    ('6 Months', timedelta(days=182)),
    ('1 Year',   timedelta(days=365)),
    ('All Time', None),
]

# --- COM port detection ---
def _port_matches(port):
    desc = (port.description or '').lower()
    mfr  = (port.manufacturer or '').lower()
    return any(kw in desc or kw in mfr for kw in ESP32_KEYWORDS)

def _pick_port_dialog(ports, message):
    """Show a small tkinter window to let the user pick a COM port. Returns device string or None."""
    selected = [None]

    root = tk.Tk()
    root.title('Soil Monitor — Select Port')
    root.resizable(False, False)
    root.lift()
    root.attributes('-topmost', True)

    tk.Label(root, text=message, padx=16, pady=10, font=('Segoe UI', 10)).pack(anchor='w')

    var = tk.StringVar(value=ports[0].device)
    for p in ports:
        label = f"{p.device}  —  {p.description}"
        tk.Radiobutton(root, text=label, variable=var, value=p.device,
                       anchor='w', padx=16, font=('Segoe UI', 9)).pack(fill='x')

    def on_connect():
        selected[0] = var.get()
        root.destroy()

    def on_cancel():
        root.destroy()

    btn_frame = tk.Frame(root, pady=10)
    btn_frame.pack()
    tk.Button(btn_frame, text='Connect', command=on_connect,
              width=12, font=('Segoe UI', 9)).pack(side='left', padx=6)
    tk.Button(btn_frame, text='Cancel', command=on_cancel,
              width=12, font=('Segoe UI', 9)).pack(side='left', padx=6)

    root.protocol('WM_DELETE_WINDOW', on_cancel)
    root.mainloop()
    return selected[0]

def _error_dialog(message):
    root = tk.Tk()
    root.withdraw()
    tk.messagebox.showerror('Soil Monitor — Error', message)
    root.destroy()

def find_esp32_port():
    """Detect the COM port the ESP32 is connected to. Returns device string or exits."""
    import tkinter.messagebox  # ensure available inside frozen .exe

    all_ports = list(serial.tools.list_ports.comports())
    matches   = [p for p in all_ports if _port_matches(p)]

    if len(matches) == 1:
        print(f"Auto-detected ESP32 on {matches[0].device} ({matches[0].description})")
        return matches[0].device

    if len(matches) > 1:
        port = _pick_port_dialog(matches,
                                 f"Multiple ESP32 devices found — please choose one:")
    elif all_ports:
        port = _pick_port_dialog(all_ports,
                                 "No ESP32 detected automatically.\nPlease select the correct port:")
    else:
        _error_dialog("No COM ports found.\n\nMake sure your ESP32 is plugged in, then restart.")
        sys.exit(1)

    if port is None:
        sys.exit(0)  # User cancelled
    return port

SERIAL_PORT = find_esp32_port()

# --- Shared state ---
times          = deque()  # No maxlen — store all readings
data           = {p[0]: deque() for p in PARAMS}
lock           = threading.Lock()
selected_range = TIME_RANGES[-1]  # Default: All Time

# --- CSV setup ---
csv_filename = "soil_data.csv"
file_is_new  = not os.path.exists(csv_filename)

# Pre-load all historical data from CSV into the deques
if not file_is_new:
    with open(csv_filename, 'r', newline='') as f:
        reader = csv.DictReader(f)
        rows = list(reader)
    for row in rows:
        try:
            ts = datetime.fromisoformat(row['timestamp'])
        except (KeyError, ValueError):
            continue
        times.append(ts)
        for key, _, _, _ in PARAMS:
            try:
                data[key].append(float(row[key]))
            except (KeyError, ValueError):
                data[key].append(0)
    print(f"Loaded {len(times)} historical readings from {csv_filename}")

csv_file   = open(csv_filename, 'a', newline='')
csv_writer = csv.writer(csv_file)
if file_is_new:
    csv_writer.writerow(['timestamp'] + [p[0] for p in PARAMS])
print(f"Logging to {csv_filename}")

# --- Serial reader ---
def serial_reader():
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        print(f"Connected to {SERIAL_PORT}")
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}")
        return

    while True:
        try:
            line = ser.readline().decode('utf-8').strip()
        except Exception:
            continue

        if not line:
            continue

        try:
            reading = json.loads(line)
        except json.JSONDecodeError:
            continue  # Ignore non-JSON lines (boot messages, etc.)

        ts = datetime.now()
        with lock:
            times.append(ts)
            for key, _, _, _ in PARAMS:
                data[key].append(reading.get(key, 0))

        csv_writer.writerow([ts.isoformat()] + [reading.get(p[0], '') for p in PARAMS])
        csv_file.flush()
        print(f"[{ts.strftime('%H:%M:%S')}] {reading}")

thread = threading.Thread(target=serial_reader, daemon=True)
thread.start()

# --- Build the UI ---
fig, axes = plt.subplots(4, 2, figsize=(13, 11))
fig.suptitle('Soil Sensor Monitor', fontsize=14, fontweight='bold')
fig.subplots_adjust(bottom=0.1)  # Reserve space at bottom for buttons

axes_flat = axes.flatten()
axes_flat[-1].set_visible(False)  # 7 params, 8 subplots — hide the last one

plot_lines = []
for i, (key, label, unit, color) in enumerate(PARAMS):
    ax = axes_flat[i]
    line, = ax.plot([], [], color=color, linewidth=1.5)
    title = f'{label} ({unit})' if unit else label
    ax.set_title(title, fontsize=9)
    ax.tick_params(axis='x', rotation=30, labelsize=7)
    ax.tick_params(axis='y', labelsize=8)
    plot_lines.append((ax, line, key))

plt.tight_layout(rect=[0, 0.08, 1, 1])

# --- Time range buttons ---
COLOR_ACTIVE   = '#4a90d9'
COLOR_INACTIVE = '#e0e0e0'

n_buttons  = len(TIME_RANGES)
btn_width  = 0.11
btn_height = 0.04
btn_y      = 0.02
total_w    = n_buttons * btn_width + (n_buttons - 1) * 0.01
start_x    = (1.0 - total_w) / 2

range_buttons = []
for i, (label, delta) in enumerate(TIME_RANGES):
    x = start_x + i * (btn_width + 0.01)
    ax_btn = fig.add_axes([x, btn_y, btn_width, btn_height])
    color  = COLOR_ACTIVE if (label, delta) == selected_range else COLOR_INACTIVE
    btn    = Button(ax_btn, label, color=color, hovercolor='#a8c8f0')
    range_buttons.append((btn, label, delta))

def make_range_callback(chosen_label, chosen_delta):
    def callback(_event):
        global selected_range
        selected_range = (chosen_label, chosen_delta)
        for btn, lbl, _ in range_buttons:
            btn.ax.set_facecolor(COLOR_ACTIVE if lbl == chosen_label else COLOR_INACTIVE)
            btn.color         = COLOR_ACTIVE if lbl == chosen_label else COLOR_INACTIVE
            btn.label.set_color('white' if lbl == chosen_label else 'black')
        fig.canvas.draw_idle()
    return callback

for btn, label, delta in range_buttons:
    btn.on_clicked(make_range_callback(label, delta))
    if (label, delta) == selected_range:
        btn.label.set_color('white')

# --- Animation ---
def get_time_formatter(delta):
    if delta is None or delta > timedelta(days=30):
        return mdates.DateFormatter('%Y-%m-%d')
    elif delta > timedelta(days=1):
        return mdates.DateFormatter('%m/%d %H:%M')
    else:
        return mdates.DateFormatter('%H:%M:%S')

def update(_frame):
    with lock:
        if len(times) < 2:
            return
        t_all = list(times)
        v_all = {key: list(data[key]) for key, _, _, _ in PARAMS}

    _, delta = selected_range
    if delta is not None:
        cutoff  = datetime.now() - delta
        indices = [i for i, t in enumerate(t_all) if t >= cutoff]
        if len(indices) < 2:
            return
        t    = [t_all[i] for i in indices]
        vals = {key: [v_all[key][i] for i in indices] for key, _, _, _ in PARAMS}
    else:
        t    = t_all
        vals = v_all

    fmt = get_time_formatter(delta)
    for ax, line, key in plot_lines:
        line.set_data(t, vals[key])
        ax.xaxis.set_major_formatter(fmt)
        ax.relim()
        ax.autoscale_view()

ani = animation.FuncAnimation(fig, update, interval=500, cache_frame_data=False)

try:
    plt.show()
finally:
    csv_file.close()
