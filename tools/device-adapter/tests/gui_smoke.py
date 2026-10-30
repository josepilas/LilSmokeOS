"""Actual Tk callbacks + real synthetic operations; no global input injection."""
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import time
import tkinter as tk
from unittest.mock import patch

sys.path[:0] = [str(Path(__file__).resolve().parents[1]), str(Path(__file__).resolve().parent)]
import adapter as a
import gui
from test_adapter import Fixture

def wait(window):
    deadline = time.monotonic() + 15
    while window.busy and time.monotonic() < deadline:
        window.root.update()
        time.sleep(0.01)
    assert not window.busy, "Worker did not finish"
    return json.loads(window.text.get("1.0", "end"))


with tempfile.TemporaryDirectory() as sandbox:
    f = Fixture(sandbox)
    root = tk.Tk()
    root.withdraw()
    window = gui.Window(root)
    before = f.snapshot()
    actual_inspect, actual_plan, actual_apply = a.inspect, a.plan_apply, a.apply_bundle
    gate = threading.Event()
    def inspect(source, profile=None, progress=None):
        gate.wait(5)
        return actual_inspect(source, f.profile, progress)
    def plan(bundle, target, profile=None, provider=None):
        return actual_plan(bundle, target, f.profile, f.provider)
    def install(bundle, target, approved, profile=None, progress=None):
        return actual_apply(bundle, target, approved, f.profile, f.provider, progress)
    actual_rollback = a.rollback
    def recover(target, transaction=None, profile=None, progress=None):
        return actual_rollback(target, transaction, f.profile, f.provider, progress)
    with patch.object(a, "profile_load", return_value=f.profile), patch.object(a, "identity", f.provider), \
         patch.object(a, "inspect", inspect), patch.object(a, "plan_apply", plan), \
         patch.object(a, "apply_bundle", install), patch.object(a, "rollback", recover), \
         patch.object(gui.messagebox, "askokcancel", return_value=True), \
         patch.object(gui.messagebox, "showerror") as errors, patch.object(gui.messagebox, "showinfo") as infos:
        with patch.object(gui.filedialog, "askdirectory", return_value=str(f.source).replace("\\", "/")):
            window.browse("source")
        window.fields["export"].set(str(f.bundle).replace("\\", "/"))
        with patch.object(gui.filedialog, "askdirectory", return_value=str(f.target).replace("\\", "/")):
            window.browse("target")
        window.inspect()
        assert window.busy
        assert all(str(button["state"]) == "disabled" for button in window.buttons)
        gate.set()
        assert wait(window)["profile_id"] == f.profile["id"]
        window.export()
        assert wait(window)["complete"]
        assert "100%" in window.amount.get() and "MB" in window.amount.get()
        window.review()
        reviewed = wait(window)
        assert window.plan["plan_sha256"] == reviewed["plan_sha256"]
        for key in ("export", "target"):
            value = window.fields[key].get().replace("\\", "/")
            window.fields[key].set(value.lower() if os.name == "nt" else value)
        window.install()
        result = wait(window)
        assert result.get("status") == "installed", result
        assert (f.target / "cubegm/icube").read_bytes() == a.HOOK
        assert all(str(button["state"]) == "normal" for button in window.buttons)
        journal = f.target / a.CONTROL / "transactions" / result["transaction"] / "journal.json"
        with patch.object(gui.filedialog, "askopenfilename", return_value=str(journal)):
            window.recover()
        assert wait(window)["status"] == "rolled-back"
        assert f.snapshot() == before
        assert not errors.called and not infos.called
    root.destroy()
print(json.dumps(dict(passed=True, actual_tk_widgets=True, busy_progress_report=True,
                      folder_chooser_callbacks=True, inspect_export_review_install_rollback=True,
                      equivalent_windows_folder_paths=True, windows_input_injected=False, sd_written=False)))
