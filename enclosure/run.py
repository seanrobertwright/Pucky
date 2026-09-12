"""Export, measure, DFM-check and render the enclosure parts with the threedp toolkit.

    uv run --project D:/repos/3d-skills python run.py [cradle|base ...]

Nothing here measures anything itself: dimensions come from threedp.features / intent.check,
printability from threedp.dfm, pictures from threedp.render (a channel, not a gate).
"""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MATERIAL = "PLA_generic"


def load_model(name: str):
    spec = importlib.util.spec_from_file_location(f"{name}_model", HERE / name / "model.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def run(name: str) -> bool:
    import warnings

    from threedp import dfm, features, intent, io, printability, render

    mod = load_model(name)
    params = mod.load_params()
    out = HERE / name / "out"
    out.mkdir(exist_ok=True)
    (HERE / name / "renders").mkdir(exist_ok=True)

    print(f"\n===== {name} =====")
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        try:
            res = io.export(mod.build, out / "part", nominal=("step",), compensated=("stl", "3mf"),
                            calibration=MATERIAL, params=params)
        except RuntimeError as e:          # 3MF writer refused the mesh: export STL, report why
            print("3MF EXPORT FAILED:", e, "-- exporting STL only; mesh validity is checked below")
            res = io.export(mod.build, out / "part", nominal=("step",), compensated=("stl",),
                            calibration=MATERIAL, params=params)
        # an uncompensated mesh, so the mesh ruler can be compared with the BREP ruler like for like
        io.export(mod.build, out / "nominal", nominal=("stl",), compensated=(), calibration=None,
                  params=params)
        for w in {str(w.message) for w in caught}:
            print("WARNING:", w)
    print("exported:", res)

    ok = True
    checks = [("part.step", "nominal BREP — the verdict"),
              ("nominal.stl", "nominal mesh — cross-check of the ruler"),
              ("part.stl", f"compensated mesh [{MATERIAL}] — expected to differ by the compensation deltas")]
    for fname, label in checks:
        path = out / fname
        if not path.exists():
            continue
        feats = features.extract(path)
        print(f"\n--- intent check on {fname}: {label} ---")
        print(f"watertight={feats.watertight}  noncircular sections={len(feats.noncircular)}  "
              f"tapered={len(feats.tapered)}  cylinders={len(feats.cylinders)}")
        report = intent.check(feats, HERE / name / "intent.json")
        print(report)
        if fname == "part.step":
            ok = bool(getattr(report, "passed", getattr(report, "ok", False)))

    feats = features.extract(out / "part.stl")
    print("\n--- printability (measurements) ---")
    print("min_wall (ESTIMATE):", printability.min_wall(feats.mesh, samples=2000))
    print("overhang histogram (deg from vertical):", printability.overhang_histogram(feats.mesh, threshold_deg=45))
    print("\n--- DFM verdict ---")
    print(dfm.evaluate(feats.mesh, MATERIAL, part=name))

    sheet = HERE / name / "renders" / "latest.png"
    render.contact_sheet(out / "part.stl", sheet, views=("iso", "top", "front", "right"),
                         projection="parallel", scale_bar=True, plate="p1s")
    print("render:", sheet)
    return ok


if __name__ == "__main__":
    names = sys.argv[1:] or ["cradle", "base"]
    results = {n: run(n) for n in names}
    print("\nintent verdicts:", results)
