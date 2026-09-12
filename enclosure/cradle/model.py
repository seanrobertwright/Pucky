"""Pucky — cradle (part 1 of 2).

Flat disc, printed face-up. The Elecrow 2.1" rotary module drops in: its three brass standoffs
land in three pockets on top of three posts, the round PCB floats inside the cavity, and the
rotating 80 mm knob body sits 6 mm in front of the cradle face touching nothing. Two M3 screws
through counterbored holes fix the cradle into the base's saddle. Cable leaves through a slot at
the bottom (270 degrees).

All dimensions are millimetres; angles are suffixed _deg. Z is the cradle axis, +Z toward the
display, bottom face on the build plate at z=0.
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


def build(p, screw_holes: bool = True):
    from build123d import Align, Axis, Box, BuildPart, Cylinder, Locations, Mode, Rotation

    bottom = (Align.CENTER, Align.CENTER, Align.MIN)
    top = (Align.CENTER, Align.CENTER, Align.MAX)
    T = p["CRADLE_T"]
    cavity_floor = T - p["CAVITY_DEPTH"]
    post_top = T - p["POST_TOP_DEPTH"]
    posts = [_polar(p["POST_PCD"] / 2, p["POST_ANGLE0_deg"] + 120 * i) for i in range(3)]
    mounts = [_polar(p["MOUNT_R"], p["MOUNT_ANGLE0_deg"] + 120 * i) for i in range(2)]

    with BuildPart() as part:
        Cylinder(radius=p["CRADLE_OD"] / 2, height=T, align=bottom)

        # cavity for the PCB + everything behind it
        with Locations((0, 0, T)):
            Cylinder(radius=p["CAVITY_D"] / 2, height=p["CAVITY_DEPTH"], align=top, mode=Mode.SUBTRACT)

        # three posts up from the cavity floor, each with a standoff pocket
        with Locations(*[(x, y, cavity_floor) for x, y in posts]):
            Cylinder(radius=p["POST_D"] / 2, height=post_top - cavity_floor, align=bottom)
        with Locations(*[(x, y, post_top) for x, y in posts]):
            Cylinder(radius=p["POCKET_D"] / 2, height=p["POCKET_DEPTH"], align=top, mode=Mode.SUBTRACT)
        if screw_holes:
            with Locations(*[(x, y, 0) for x, y in posts]):
                Cylinder(radius=p["SCREW_D"] / 2, height=post_top, align=bottom, mode=Mode.SUBTRACT)

        # cable slot: from inside the cavity out through the rim, floor level with the cavity floor
        slot_len = p["CRADLE_OD"] / 2 - p["CAVITY_D"] / 2 + 6.0
        with Locations((0, -(p["CRADLE_OD"] / 2 + 1.0), cavity_floor)):
            Box(p["SLOT_W"], slot_len + 1.0, p["CAVITY_DEPTH"] + 1.0,
                align=(Align.CENTER, Align.MIN, Align.MIN), mode=Mode.SUBTRACT)

        # two M3 mounting holes with head counterbores from the front face
        with Locations(*[(x, y, 0) for x, y in mounts]):
            Cylinder(radius=p["MOUNT_D"] / 2, height=T * 3, mode=Mode.SUBTRACT)
        with Locations(*[(x, y, T) for x, y in mounts]):
            Cylinder(radius=p["CBORE_D"] / 2, height=p["CBORE_DEPTH"], align=top, mode=Mode.SUBTRACT)

    return part.part


if __name__ == "__main__":
    shape = build({k: v["value"] for k, v in load_params().items()})
    bb = shape.bounding_box()
    print("bbox", tuple(round(v, 2) for v in bb.size), "volume", round(shape.volume, 2))
