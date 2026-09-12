"""Pucky — base (part 2 of 2).

A round puck with a rear pillar whose front is a half-round saddle, tilted 15 degrees back, that
the cradle disc nests into. In front of the saddle a cylindrical trough is cut so the 80 mm
rotating knob body dips into the base without touching it. Two teardropped heat-set insert holes
in the saddle floor take the cradle's M3 screws. The USB cable drops from the cradle's slot down a
vertical shaft into a groove in the underside that exits at the rear.

Built in a local frame where the cradle axis is +Y (toward the screen), Z up, origin at the
cradle front-face centre; that assembly is tilted and placed, then unioned with the puck.
Printed on its flat bottom. All dimensions mm; angles suffixed _deg.
"""

from __future__ import annotations

import json
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent


def load_params():
    """Role-tagged params, as threedp.io.export expects; build() receives the resolved values."""
    with open(HERE / "params.json", encoding="utf-8") as f:
        return json.load(f)


def _polar(r, angle_deg):
    a = math.radians(angle_deg)
    return (r * math.cos(a), r * math.sin(a))


def build(p, cable_groove: bool = True):
    from build123d import Align, Axis, Box, Cylinder, Location, Rotation

    C, MIN, MAX = Align.CENTER, Align.MIN, Align.MAX
    tilt = p["TILT_deg"]
    r_pil, r_pock, r_tr = p["PILLAR_D"] / 2, p["POCKET_D"] / 2, p["TROUGH_D"] / 2
    L = p["POCKET_DEPTH"] + p["PILLAR_BACK"]
    to_y = Rotation(-90, 0, 0)            # default +Z axis -> local +Y (front)

    # --- local frame: pillar = half-round-topped block behind the cradle face, open saddle on top
    pillar = Cylinder(r_pil, L, align=(C, C, MAX)).rotate(Axis.X, -90)          # y in [-L, 0]
    pillar += Box(2 * r_pil, L, r_pil + 80, align=(C, MAX, MAX))                # fill below
    pillar &= Box(400, 400, 400, align=(C, C, MAX)).moved(Location((0, 0, p["SADDLE_TOP"])))

    pocket = Cylinder(r_pock, p["POCKET_DEPTH"] + 6, align=(C, C, MIN)).rotate(Axis.X, -90)
    pocket = pocket.moved(Location((0, -p["POCKET_DEPTH"], 0)))                # y in [-depth, +6]
    # starts exactly at the pillar face: starting behind it shaves a 0.3 mm shell off the block's
    # front corners (trough r 43 > pillar half-width 40), which DFM caught as a 0.013 mm wall
    trough = Cylinder(r_tr, p["TROUGH_LEN"], align=(C, C, MIN)).rotate(Axis.X, -90)

    # teardropped insert holes drilled into the pocket floor along -Y, apex toward local +Z.
    # Teardrop = cylinder + a square of side r rotated 45 deg whose far corner is the apex at
    # r*sqrt(2); built with the apex toward default -Y, which the X rotation maps to local +Z.
    r_i = p["INSERT_D"] / 2
    d_i = p["INSERT_DEPTH"]
    # the square is 3 % over tangent size so its sides cross the circle instead of kissing it;
    # an exactly tangent union tessellates into zero-area sliver faces at the hole's end cap
    s = r_i * 1.03
    drop = Cylinder(r_i, d_i, align=(C, C, MIN))
    drop += Box(s, s, d_i, align=(C, C, MIN)).rotate(Axis.Z, 45).moved(
        Location((0, -s / math.sqrt(2), 0)))
    drop = drop.rotate(Axis.X, -90)                                              # y in [0, depth]
    inserts = None
    for i in range(2):
        x, z = _polar(p["INSERT_R"], p["INSERT_ANGLE0_deg"] + 120 * i)
        d = drop.moved(Location((x, -p["POCKET_DEPTH"] - p["INSERT_DEPTH"], z)))
        inserts = d if inserts is None else inserts + d

    local = pillar - pocket - trough - inserts

    # --- world: tilt the axis up by TILT, place the cradle centre, add the puck, cut at the floor
    place = Location((0, p["CRADLE_Y"], p["CRADLE_Z"])) * Rotation(tilt, 0, 0)
    body = local.moved(place)
    body += Cylinder(p["PUCK_D"] / 2, p["PUCK_H"], align=(C, C, MIN))
    body &= Box(400, 400, 400, align=(C, C, MIN))                                # z >= 0

    # the knob trough must also clear the puck in front of the saddle
    body -= trough.moved(place)

    if cable_groove:
        y0, y1 = p["SHAFT_Y0"], p["SHAFT_Y1"]
        shaft = Box(p["GROOVE_W"], y1 - y0, p["CRADLE_Z"] - 10, align=(C, MIN, MIN)).moved(Location((0, y0, 0)))
        groove = Box(p["GROOVE_W"], p["PUCK_D"], p["GROOVE_H"], align=(C, MAX, MIN)).moved(Location((0, y1, 0)))
        body -= shaft
        body -= groove

    return body


if __name__ == "__main__":
    shape = build({k: v["value"] for k, v in load_params().items()})
    bb = shape.bounding_box()
    print("bbox", tuple(round(v, 2) for v in bb.size), "volume", round(shape.volume, 2))
