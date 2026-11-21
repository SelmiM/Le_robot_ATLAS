#!/usr/bin/env python3
"""
Run a mission of robot/Robot.h on the computer, beside a simulated wall.

    python3 tools/sim.py                          mission 3, 20 s, 260 mm from the wall
    python3 tools/sim.py --out sim/bang           name the result
    python3 tools/sim.py --compare sim/bang.csv   draw an earlier run in grey behind
    python3 tools/sim.py -D WALL_GAIN_NUM=3       override a #define without editing
    python3 tools/sim.py --door 2000:2800         a doorway in the wall, x in mm

robot/Robot.h is compiled as it is, with an ordinary C++ compiler. The
hardware headers (Motors.h, TofSensor.h, Clock.h, ...) are replaced by the
ones in tools/sim/stubs/, which move a model robot along a wall on its left
instead of driving pins. Config.h, and Config.local.h if present, are used
as they are: SIDE_SENSOR_ENABLED must be true, as on the robot.

The run writes two files:

    sim/run.csv   one row every 10 ms: position, heading, wheel commands,
                  side reading
    sim/run.svg   the path seen from above, the side reading and the wheel
                  commands against time; VS Code shows it, and
                  redraws it after each run

The model's numbers (speeds, dead zone, wheel spacing, sensor position) are
invented and listed in tools/sim/stubs/SimWorld.h. The robot behaves like
this one, with different amplitudes and periods.
"""

import argparse
import csv
import math
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SIM = REPO / "tools" / "sim"
FLAGS = ["-std=c++17", "-O2", "-Wall"]


def build(robot_dir, defines, workspace):
    """Copy the robot's headers and the stubs together, compile, return the binary."""
    origin = {}                             # copied name -> where it came from
    for header in robot_dir.glob("*.h"):
        shutil.copy(header, workspace)
        origin[header.name] = header
    for stub in (SIM / "stubs").glob("*.h"):
        shutil.copy(stub, workspace)        # replaces the hardware headers
        origin[stub.name] = stub
    shutil.copy(SIM / "main.cpp", workspace)
    origin["main.cpp"] = SIM / "main.cpp"

    compiler = shutil.which("g++") or shutil.which("clang++")
    if compiler is None:
        sys.exit("No C++ compiler found. On Debian or Ubuntu: sudo apt-get install g++")

    binary = workspace / "sim"
    colour = ["-fdiagnostics-color=always"] if sys.stderr.isatty() else []
    command = [compiler, *FLAGS, *colour, *[f"-D{d}" for d in defines],
               "-I", str(workspace), "-o", str(binary), str(workspace / "main.cpp")]
    compiled = subprocess.run(command, capture_output=True, text=True)

    # Point messages at the real files rather than at the temporary copies.
    def where(match):
        source = origin.get(match.group(1))
        if source is None:
            return match.group(0)
        try:
            return str(source.relative_to(Path.cwd()))
        except ValueError:
            return str(source)
    sys.stderr.write(re.sub(re.escape(str(workspace)) + r"/([\w.]+)", where, compiled.stderr))

    if compiled.returncode != 0:
        sys.exit(f"{origin['Robot.h']} did not compile (errors above)")
    return binary


def read_trace(path):
    with open(path, newline="") as f:
        return [{k: float(v) for k, v in row.items()} for row in csv.DictReader(f)]


# ---- Drawing ----------------------------------------------------------------

WIDTH = 920
LEFT, RIGHT = 70, 20
PLOT_W = WIDTH - LEFT - RIGHT
# Each colour can be overridden by a CSS variable of the page the SVG sits in.
# They are set in the SVG's stylesheet, through classes, which every renderer
# reads; var() in an attribute is not universally supported.
COLOURS = {name: f"var(--sim-{name}, {value})" for name, value in {
    "run": "#1f6fd1", "left": "#d1561f", "right": "#1f8f5a", "grey": "#9aa0a6",
    "grid": "#e3e5e8", "axis": "#5f6368", "wall": "#202124", "missing": "#c5221f",
    "text": "#3c4043", "bg": "#ffffff"}.items()}


def nice_step(span, ticks=6):
    raw = span / ticks
    magnitude = 10 ** math.floor(math.log10(raw)) if raw > 0 else 1
    for scale in (1, 2, 5, 10):
        if scale * magnitude >= raw:
            return scale * magnitude
    return 10 * magnitude


def ticks(lo, hi):
    step = nice_step(hi - lo)
    first = (lo // step) * step
    values, i = [], 0
    while first + i * step <= hi + 1e-9:
        v = round(first + i * step, 9)
        if v >= lo - 1e-9:
            values.append(v)
        i += 1
    return values


class Panel:
    """A plot area with linear x and y scales; y may point down."""

    def __init__(self, svg, top, height, xr, yr, title, xlabel, ylabel, y_down=False):
        self.svg, self.top, self.height = svg, top, height
        self.x0, self.x1 = xr
        self.y0, self.y1 = yr
        self.y_down = y_down
        svg.append(f'<text x="{LEFT}" y="{top - 12}" class="sim-title">{title}</text>')
        for v in ticks(self.x0, self.x1):
            px = self.px(v)
            svg.append(f'<line x1="{px:.1f}" y1="{top}" x2="{px:.1f}" y2="{top + height}" class="sim-grid"/>')
            svg.append(f'<text x="{px:.1f}" y="{top + height + 15}" class="sim-tick" text-anchor="middle">{v:g}</text>')
        for v in ticks(self.y0, self.y1):
            py = self.py(v)
            svg.append(f'<line x1="{LEFT}" y1="{py:.1f}" x2="{LEFT + PLOT_W}" y2="{py:.1f}" class="sim-grid"/>')
            svg.append(f'<text x="{LEFT - 6}" y="{py + 4:.1f}" class="sim-tick" text-anchor="end">{v:g}</text>')
        svg.append(f'<rect x="{LEFT}" y="{top}" width="{PLOT_W}" height="{height}" class="sim-frame"/>')
        svg.append(f'<text x="{LEFT + PLOT_W}" y="{top + height + 30}" class="sim-label" text-anchor="end">{xlabel}</text>')
        svg.append(f'<text x="{LEFT - 50}" y="{top + height / 2}" class="sim-label" text-anchor="middle" '
                   f'transform="rotate(-90 {LEFT - 50} {top + height / 2})">{ylabel}</text>')

    def px(self, v):
        return LEFT + (v - self.x0) / (self.x1 - self.x0) * PLOT_W

    def py(self, v):
        f = (v - self.y0) / (self.y1 - self.y0)
        return self.top + (f if self.y_down else 1 - f) * self.height

    def line(self, xs, ys, colour, width=1.6, dash=None):
        xs, ys = list(xs), list(ys)
        keep = [i for i in range(len(xs)) if i % 3 == 0 or i == len(xs) - 1]
        xs, ys = [xs[i] for i in keep], [ys[i] for i in keep]
        pts = " ".join(f"{self.px(a):.1f},{self.py(b):.1f}" for a, b in zip(xs, ys))
        extra = f' stroke-dasharray="{dash}"' if dash else ""
        self.svg.append(f'<polyline points="{pts}" fill="none" class="sim-s-{colour}" '
                        f'stroke-width="{width}" stroke-linejoin="round"{extra}/>')

    def dots(self, xs, ys, colour, r=1.6):
        for a, b in zip(xs, ys):
            self.svg.append(f'<circle cx="{self.px(a):.1f}" cy="{self.py(b):.1f}" r="{r}" class="sim-f-{colour}"/>')


def legend(svg, x, y, items):
    for label, colour in items:
        svg.append(f'<rect x="{x}" y="{y - 9}" width="14" height="4" class="sim-f-{colour}"/>')
        svg.append(f'<text x="{x + 20}" y="{y - 4}" class="sim-tick">{label}</text>')
        x += 30 + 7 * len(label)


def draw(rows, others, doors, title, path):
    t = [r["t_ms"] / 1000 for r in rows]
    everything = rows + [r for _, o in others for r in o]
    x_max = max(r["x_mm"] for r in everything) / 1000
    x_min = min(0, min(r["x_mm"] for r in everything) / 1000)
    t_max = max(max(r["t_ms"] for r in everything) / 1000, 1)
    c_max = max(200, max(r["clearance_mm"] for r in everything) * 1.1)
    c_min = min(0, min(r["clearance_mm"] for r in everything))

    svg = []
    top = 50
    p1 = Panel(svg, top, 230, (x_min, max(x_max, 0.5)), (c_min, c_max),
               "Path seen from above (not to scale: the wall is the black line)",
               "distance along the wall (m)", "left flank to wall (mm)", y_down=True)
    # The wall, with its doorways.
    segments, at = [], x_min * 1000
    for d in sorted(doors):
        segments.append((at, d[0]))
        at = d[1]
    segments.append((at, max(x_max, 0.5) * 1000))
    for a, b in segments:
        if b > a:
            svg.append(f'<line x1="{p1.px(a / 1000):.1f}" y1="{p1.py(0):.1f}" x2="{p1.px(b / 1000):.1f}" '
                       f'y2="{p1.py(0):.1f}" class="sim-s-wall" stroke-width="5"/>')
    for name, o in others:
        p1.line([r["x_mm"] / 1000 for r in o], [r["clearance_mm"] for r in o], "grey", 1.4, "5 3")
    p1.line([r["x_mm"] / 1000 for r in rows], [r["clearance_mm"] for r in rows], "run", 2)
    # Start (hollow) and end (filled), so that a robot that never moved still shows.
    first, last = rows[0], rows[-1]
    svg.append(f'<circle cx="{p1.px(last["x_mm"] / 1000):.1f}" cy="{p1.py(last["clearance_mm"]):.1f}" '
               f'r="4" class="sim-f-run"/>')
    svg.append(f'<circle cx="{p1.px(first["x_mm"] / 1000):.1f}" cy="{p1.py(first["clearance_mm"]):.1f}" '
               f'r="5" stroke-width="2" class="sim-s-run sim-f-bg"/>')

    top += 230 + 80
    sides = [(r["t_ms"] / 1000, r["side_mm"]) for r in rows if r["side_ok"] and r["t_ms"] % 40 == 0]
    s_max = max([s for _, s in sides] + [r["side_mm"] for _, o in others for r in o if r["side_ok"]] + [200]) * 1.1
    p2 = Panel(svg, top, 180, (0, t_max), (0, s_max),
               "Side sensor reading (red bars: no valid reading)", "time (s)", "side_mm")
    for name, o in others:
        kept = [r for r in o if r["side_ok"] and r["t_ms"] % 40 == 0]
        p2.dots([r["t_ms"] / 1000 for r in kept], [r["side_mm"] for r in kept],
                "grey", 1.1)
    p2.dots([a for a, _ in sides], [b for _, b in sides], "run")
    # One bar per stretch without a valid reading.
    start = None
    for r in rows + [{"t_ms": rows[-1]["t_ms"], "side_ok": 1}]:
        if not r["side_ok"] and start is None:
            start = r["t_ms"]
        elif r["side_ok"] and start is not None:
            a, b = p2.px(start / 1000), p2.px(r["t_ms"] / 1000)
            svg.append(f'<rect x="{a:.1f}" y="{top + 172}" width="{max(b - a, 1):.1f}" height="8" '
                       f'class="sim-f-missing"/>')
            start = None

    top += 180 + 80
    lo = min(-10, min(min(r["left_cmd"], r["right_cmd"]) for r in rows))
    hi = max(10, max(max(r["left_cmd"], r["right_cmd"]) for r in rows))
    p3 = Panel(svg, top, 160, (0, t_max), (lo - 10, hi + 10),
               "Wheel commands", "time (s)", "command (-255 to 255)")
    p3.line(t, [r["left_cmd"] for r in rows], "left", 1.3)
    p3.line(t, [r["right_cmd"] for r in rows], "right", 1.3)
    legend(svg, LEFT + 6, top + 175 + 30, [("left", "left"), ("right", "right")])

    height = top + 160 + 50
    run_legend = [(title, "run")] + [(n, "grey") for n, _ in others]
    head = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {WIDTH} {height}" '
            f'width="{WIDTH}" height="{height}" font-family="system-ui, sans-serif">',
            f'<style>.sim-title{{font-size:14px;font-weight:600;fill:{COLOURS["text"]}}}'
            f'.sim-tick{{font-size:11px;fill:{COLOURS["axis"]}}}.sim-label{{font-size:12px;fill:{COLOURS["text"]}}}'
            f'.sim-grid{{stroke:{COLOURS["grid"]};stroke-width:1}}'
            f'.sim-frame{{fill:none;stroke:{COLOURS["axis"]};stroke-width:1}}'
            + "".join(f".sim-s-{name}{{stroke:{value}}}.sim-f-{name}{{fill:{value}}}"
                      for name, value in COLOURS.items())
            + '</style>',
            f'<rect width="{WIDTH}" height="{height}" class="sim-f-bg"/>']
    legend(head, LEFT, 22, run_legend)
    path.write_text("\n".join(head + svg + ["</svg>"]) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0].strip())
    parser.add_argument("--mission", type=int, default=3, help="mission number (default 3)")
    parser.add_argument("--duration", type=float, default=20, help="seconds (default 20)")
    parser.add_argument("--start", type=float, default=260,
                        help="initial gap between the left flank and the wall, mm (default 260)")
    parser.add_argument("--heading", type=float, default=0,
                        help="initial angle to the wall, degrees, positive towards it (default 0)")
    parser.add_argument("--door", action="append", default=[], metavar="FROM:TO",
                        help="gap in the wall from x=FROM to x=TO mm; repeat for more")
    parser.add_argument("-D", dest="defines", action="append", default=[], metavar="NAME=VALUE",
                        help="#define passed to the compiler; overrides an #ifndef default")
    parser.add_argument("--seed", type=int, default=1, help="sensor noise seed (default 1)")
    parser.add_argument("--compare", action="append", default=[], metavar="CSV",
                        help="earlier run to draw in grey; repeat for more")
    parser.add_argument("--out", default="sim/run", help="output name, without extension (default sim/run)")
    parser.add_argument("--robot", default=str(REPO / "robot"), help=argparse.SUPPRESS)
    args = parser.parse_args()

    doors = []
    for d in args.door:
        try:
            a, b = (float(v) for v in d.split(":"))
        except ValueError:
            sys.exit(f"--door {d}: expected FROM:TO in mm, such as 2000:2800")
        doors.append((min(a, b), max(a, b)))

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    trace = out.with_suffix(".csv")
    picture = out.with_suffix(".svg")

    with tempfile.TemporaryDirectory() as workspace:
        binary = build(Path(args.robot), args.defines, Path(workspace))
        command = [str(binary), str(trace), str(int(args.duration * 1000)), str(args.start),
                   str(args.heading), str(args.mission), str(args.seed),
                   *[f"{a}:{b}" for a, b in doors]]
        if subprocess.run(command).returncode != 0:
            sys.exit("the simulation stopped with an error")

    others = [(Path(c).stem, read_trace(c)) for c in args.compare]
    rows = read_trace(trace)
    if not rows:
        sys.exit("the run ended before recording anything")
    draw(rows, others, doors, out.stem, picture)
    print(f"wrote    {trace}\n         {picture}")


if __name__ == "__main__":
    main()
