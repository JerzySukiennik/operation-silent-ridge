# Runs the build steps in one editor session (startup is slow): import -> materials -> tiles -> level. OSR_STEPS env selects steps.
import os, sys, traceback, unreal
here = os.path.dirname(os.path.abspath(__file__))
steps = os.environ.get("OSR_STEPS", "ue_01_import,ue_02_materials,ue_01b_tiles,ue_03_level").split(",")
for s in steps:
    print("OSR STEP", s, flush=True)
    try:
        g = {"__file__": os.path.join(here, s + ".py"), "__name__": "__main__"}
        exec(compile(open(os.path.join(here, s + ".py")).read(), s + ".py", "exec"), g)
    except Exception:
        print("OSR STEP FAILED", s, traceback.format_exc(), flush=True)
        break
print("OSR ALL END", flush=True)
