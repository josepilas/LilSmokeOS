"""Small native PC interface; blocking I/O never runs on Tk's event thread."""
import json
from pathlib import Path
import queue
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
import adapter


class Window:
    def __init__(self, root):
        self.root, self.messages, self.plan = root, queue.Queue(), None
        self.busy = False
        root.title("LilSmoke Device Adapter " + adapter.VERSION)
        root.geometry("870x680")
        root.minsize(680, 540)
        frame = ttk.Frame(root, padding=16)
        frame.pack(fill="both", expand=True)
        ttk.Label(frame, text="LilSmoke Device Adapter", font=("TkDefaultFont", 18, "bold")).pack(anchor="w")
        ttk.Label(frame, text="Private stock firmware import. No formatting, partition changes or raw disk writes.",
                  wraplength=800).pack(anchor="w", pady=(5, 14))
        self.fields, self.buttons = {}, []
        for key, title in (("source", "Original stock card files"), ("export", "Private export folder"),
                           ("target", "Flashed LILOS volume root")):
            row = ttk.Frame(frame)
            row.pack(fill="x", pady=4)
            ttk.Label(row, text=title, width=27).pack(side="left")
            variable = tk.StringVar()
            self.fields[key] = variable
            ttk.Entry(row, textvariable=variable).pack(side="left", fill="x", expand=True, padx=8)
            button = ttk.Button(row, text="Browse...", command=lambda k=key: self.browse(k))
            button.pack(side="left")
            self.buttons.append(button)
        row = ttk.Frame(frame)
        row.pack(fill="x", pady=(12, 8))
        for title, action in (("Inspect", self.inspect), ("Export", self.export), ("Review Install", self.review),
                              ("Install", self.install), ("Recover / Rollback", self.recover)):
            button = ttk.Button(row, text=title, command=action)
            button.pack(side="left", padx=(0, 6))
            self.buttons.append(button)
        self.status = tk.StringVar(value="Select the folder containing cubegm, or cubegm itself.")
        ttk.Label(frame, textvariable=self.status, wraplength=800).pack(anchor="w", pady=6)
        self.bar = ttk.Progressbar(frame, maximum=100)
        self.bar.pack(fill="x", pady=(0, 6))
        self.amount = tk.StringVar(value="")
        ttk.Label(frame, textvariable=self.amount).pack(anchor="w")
        report = ttk.Frame(frame)
        report.pack(fill="both", expand=True, pady=8)
        self.text = tk.Text(report, wrap="word", height=18, state="disabled")
        self.text.pack(side="left", fill="both", expand=True)
        scrollbar = ttk.Scrollbar(report, command=self.text.yview)
        scrollbar.pack(side="right", fill="y")
        self.text.configure(yscrollcommand=scrollbar.set)
        ttk.Label(frame, text="Unknown variants are export-only. A matching folder cannot supply boot sectors. "
                  "After installation, safely eject and verify boot, display, audio, input and shutdown on the device.",
                  wraplength=800).pack(anchor="w")
        root.protocol("WM_DELETE_WINDOW", self.close)
        root.after(80, self.poll)

    def browse(self, key):
        path = filedialog.askdirectory(title={"source": "Select original stock files (read only)",
                                             "export": "Select parent folder for a NEW private export",
                                             "target": "Select flashed LILOS volume root"}[key])
        if path:
            if key == "export":
                path = str(Path(path) / ("stock-export-" + time.strftime("%Y%m%d-%H%M%S")))
            self.fields[key].set(path)
            self.plan = None

    def values(self):
        return {k: v.get().strip() for k, v in self.fields.items()}

    def progress(self, done, total, title):
        self.messages.put(("progress", (done, total, title)))

    def run(self, title, function, mode="steps", kind="normal"):
        if self.busy:
            return
        self.busy, self.mode = True, mode
        self.status.set(title)
        self.bar["value"] = 0
        self.amount.set("")
        for button in self.buttons:
            button.configure(state="disabled")
        def task():
            try:
                result = function()
                self.messages.put(("done", (result, kind)))
            except Exception as error:
                self.messages.put(("error", str(error)))
        threading.Thread(target=task, daemon=False).start()

    def inspect(self):
        paths = self.values()
        self.plan = None
        self.run("Inspecting stock files (read only)...", lambda: adapter.inspect(paths["source"], progress=self.progress))

    def export(self):
        paths = self.values()
        self.plan = None
        self.run("Exporting private firmware...", lambda: adapter.export_bundle(paths["source"], paths["export"],
                 progress=self.progress), "bytes")

    def review(self):
        paths = self.values()
        self.run("Validating bundle and target (read only)...", lambda: adapter.plan_apply(paths["export"], paths["target"]), kind="plan")

    def install(self):
        if not self.plan:
            messagebox.showinfo("Review required", "Select Review Install and read the proposed changes first.")
            return
        paths = self.values()
        try:
            matches = adapter.reviewed_paths_equal(paths["export"], paths["target"], self.plan)
        except (OSError, ValueError):
            matches = False
        if not matches:
            self.plan = None
            messagebox.showinfo("Review required", "The selected paths changed. Review Install again.")
            return
        if not messagebox.askokcancel("Install private adaptation?", "Target: " + self.plan["target"] +
                    "\nFiles changed: " + str(len(self.plan["entries"])) +
                    "\nA verified backup is created first. Do not unplug the card or close the tool during installation."):
            return
        approved = self.plan["plan_sha256"]
        bundle, target = self.plan["bundle"], self.plan["target"]
        self.run("Installing verified adaptation...", lambda: adapter.apply_bundle(bundle, target,
                 approved, progress=self.progress))

    def recover(self):
        paths = self.values()
        target = Path(paths["target"])
        transaction = None
        try:
            control = adapter.within(target, adapter.CONTROL)
            active = adapter.within(control, "active", True)
            if not active.exists() or not adapter.within(active, "owner.json", True).exists():
                journal = filedialog.askopenfilename(title="Select this card's completed transaction journal",
                    initialdir=str(control / "transactions"), filetypes=[("Transaction journal", "journal.json")])
                if not journal:
                    return
                selected = adapter.safe_absolute(journal)
                if selected.parent.parent != control / "transactions":
                    raise adapter.Refusal("Select a journal inside the selected card's transaction folder")
                transaction = selected.parent.name
        except Exception as error:
            messagebox.showerror("Recovery refused", str(error))
            return
        if messagebox.askokcancel("Restore previous files?", "The tool will verify backups and target identity before restoring. "
                                  "Unexpected file changes stop recovery. Do not unplug the card."):
            self.plan = None
            self.run("Restoring verified previous files...", lambda: adapter.rollback(paths["target"], transaction,
                     progress=self.progress))

    def display(self, value):
        self.text.configure(state="normal")
        self.text.delete("1.0", "end")
        self.text.insert("1.0", json.dumps(value, indent=2))
        self.text.configure(state="disabled")

    def poll(self):
        try:
            while True:
                kind, value = self.messages.get_nowait()
                if kind == "progress":
                    done, total, title = value
                    self.bar["value"] = 100 * done / max(1, total)
                    self.status.set(title)
                    detail = (("%.1f / %.1f MB" % (done / 1048576, total / 1048576)) if self.mode == "bytes"
                              else "%d / %d steps" % (done, total))
                    self.amount.set("%.0f%% - %s" % (100 * done / max(1, total), detail))
                else:
                    self.busy = False
                    for button in self.buttons:
                        button.configure(state="normal")
                    if kind == "error":
                        self.status.set("Operation refused or interrupted. Review the report; no repair is performed.")
                        self.display(dict(error=value, recovery="Use Recover / Rollback if installation had begun."))
                        messagebox.showerror("Operation stopped", value)
                    else:
                        result, operation = value
                        self.bar["value"] = 100
                        self.display(result)
                        self.status.set("Review ready. Installation is not a physical boot certification." if operation == "plan"
                                        else "Complete. Safely eject after any card write.")
                        if operation == "plan":
                            self.plan = result
        except queue.Empty:
            pass
        self.root.after(80, self.poll)

    def close(self):
        if self.busy:
            messagebox.showinfo("Operation in progress", "Wait for verification to finish. Do not unplug the card.")
        else:
            self.root.destroy()


def main():
    root = tk.Tk()
    Window(root)
    root.mainloop()
