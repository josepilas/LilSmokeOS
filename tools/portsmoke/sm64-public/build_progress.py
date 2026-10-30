# SPDX-License-Identifier: MPL-2.0
"""Bounded, observational progress for the trusted SM64 serial make recipes.

Parent contract (this module never invokes make or touches build files):
  plan = BuildPlan(); run_stage(make_command + ['-n'], ..., plan.on_line, ...)
  plan.finish()  # ONLY after a successful dry run, with the same flags/tree
  live = BuildProgress(plan)
  run_stage(make_command, ..., live.on_line, ...)
  live.finish()  # on success; use finish(False) on failure/cancellation
  stats = live.snapshot()

Use fail-fast -j1 (no -k/-i), no recursive/parallel/background recipes, and the
verified, prepared Makefile with NOEXTRACT=1. GNU make -n is NOT a sandbox: make-time shell
expansions and recursive recipes can still execute. Planning belongs after
prepare_engine_makefile(), never against an arbitrary Makefile or the SD.

Counts are recipe command lines, not files, bytes, elapsed time or weighted
work. Syntax checks (silent in the real build), probes and housekeeping are
excluded. A pipeline is one task. The next recognized command echo proves the
previous task completed under these serial recipes; diagnostics prove nothing.
The final task needs successful process exit. No output-file existence guesses,
timers, stored command histories or invented completions are used. The caller
can map percent into 50..95 and display active.kind/target even at 0 percent.
"""

from dataclasses import dataclass
from pathlib import PurePosixPath
import re
import shlex


KINDS = ('texture', 'compile', 'assembly', 'link', 'audio', 'generate')
MAX_LINE = 65536
MAX_TASKS = 1000000
_ASSIGNMENT = re.compile(r'^[A-Za-z_][A-Za-z_0-9]*=')
_COMPILER = re.compile(r'^(?:.*-)?(?:cc|gcc|g\+\+|c\+\+|clang|clang\+\+)(?:-[0-9.]+)?$')
_PYTHON = re.compile(r'^python(?:[0-9.]+)?$')
_HEX_FORMAT = '1/1 "0x%X,"'


@dataclass(frozen=True)
class Task:
    """One command; target is its primary output, or '' when not identifiable.

    This is descriptive, not a trusted path. The caller must validate it before
    journaling/removing anything. Multi-output tools use the make rule's primary
    target so removing it forces the entire producer to rerun.
    """

    kind: str
    tool: str
    target: str = ''


def _option(words, name):
    try:
        return words[words.index(name) + 1]
    except (ValueError, IndexError):
        return ''


def _segment(words):
    while words and (_ASSIGNMENT.match(words[0]) or words[0] in ('env', 'ccache', 'sccache')):
        words = words[1:]
    if not words:
        return None
    tool = words[0].rsplit('/', 1)[-1]
    args = words[1:]
    target = _option(args, '-o') or _option(args, '>')
    if tool in ('n64graphics', 'n64graphics_ci'):
        return Task('texture', tool, _option(args, '-i') or target)
    if tool == 'skyconv':
        if '--split' in args and len(args) >= 2:
            source, directory = args[-2:]
            mode = _option(args, '--type')
            name = {'cake': 'cake.inc.c', 'cake-eu': 'cake_eu.inc.c'}.get(mode)
            if mode == 'sky':
                name = PurePosixPath(source).stem + '_skybox.c'
            if name:
                target = str(PurePosixPath(directory) / name)
        return Task('texture', tool, target)
    if tool in ('hexdump', 'bin2c', 'sm64-bin2c'):
        if tool == 'hexdump' and not ('-v' in args and _option(args, '-e') == _HEX_FORMAT):
            return None
        kind = 'audio' if any('/sound/' in '/' + arg for arg in args) else 'texture'
        return Task(kind, tool, target)
    if tool in ('aiff_extract_codebook', 'vadpcm_enc', 'aifc_decode'):
        if tool == 'vadpcm_enc' and args:
            target = target or args[-1]
        return Task('audio', tool, target)
    if _PYTHON.fullmatch(tool):
        scripts = {arg.rsplit('/', 1)[-1] for arg in args if arg.endswith('.py')}
        if 'assemble_sound.py' in scripts:
            script = next(i for i, arg in enumerate(args) if arg.rsplit('/', 1)[-1] == 'assemble_sound.py')
            operands = args[script + 1:]
            target = _option(operands, '--sequences') or target
            if not target and len(operands) >= 3:
                target = operands[2]
            return Task('audio', 'assemble_sound.py', target)
        for script in ('mario_anims_converter.py', 'demo_data_converter.py', 'output_level_headers.py'):
            if script in scripts:
                return Task('generate', script, target)
        return None
    if tool in ('textconv', 'mio0', 'cpp'):
        if tool in ('textconv', 'mio0') and args:
            target = target or args[-1]
        return Task('generate', tool, target)
    if tool == 'armips' or tool == 'as' or tool.endswith('-as'):
        return Task('assembly', tool, target)
    if tool == 'objcopy' or tool.endswith('-objcopy'):
        return Task('assembly', tool, target or (args[-1] if args else ''))
    if tool in ('ld', 'ar') or tool.endswith(('-ld', '-ar')):
        return Task('link', tool, target)
    if _COMPILER.fullmatch(tool):
        if '-fsyntax-only' in args or any(arg.endswith('determine-endian-bitwidth.c') for arg in args):
            return None
        if '-E' in args:
            return Task('generate', tool, target)
        if '-c' in args:
            kind = 'assembly' if any(arg.endswith(('.s', '.S')) for arg in args) else 'compile'
            return Task(kind, tool, target)
        if target and '-S' not in args:
            return Task('link', tool, target)
    return None


def classify_command(line):
    """Return one Task for a supported recipe line, otherwise None.

    POSIX quoting is parsed, not evaluated. Command names must be at shell
    command positions, never substrings of diagnostics or echo arguments.
    Unsupported control flow/background jobs are deliberately not counted.
    """
    if len(line) > MAX_LINE or '\n' in line or '\r' in line:
        return None
    try:
        lexer = shlex.shlex(line.lstrip().lstrip('@+'), posix=True, punctuation_chars=';&|<>()')
        lexer.whitespace_split = True
        words = list(lexer)
    except ValueError:
        return None
    if not words or words[0] in ('if', 'for', 'while', 'case', 'until', '!') or '&' in words:
        return None
    # These recipes do not use shell branches. Do not infer a task from an
    # unexecuted branch; the endian probe's '; true' is excluded by _segment.
    if any(word in ('&&', '||') for word in words):
        return None
    if any(word in ('(', ')') for word in words):
        # assemble_sound uses $(cat endian-and-bitwidth), not shell branching.
        if not any(word.endswith('/assemble_sound.py') for word in words):
            return None
    task = None
    start = 0
    for i in range(len(words) + 1):
        if i == len(words) or words[i] in (';', '|'):
            candidate = _segment(words[start:i])
            if start == 0 and candidate is None:
                return None
            if candidate is not None:
                task = candidate
            start = i + 1
    return task


class _Lines:
    """Retain at most MAX_LINE characters, including across continuations."""

    def __init__(self, consume):
        self.consume = consume
        self.pending = ''
        self.dropping = False
        self.tail = ''
        self.rejected = 0
        self.continued = False

    def feed(self, chunk):
        offset = 0
        while offset < len(chunk):
            end = chunk.find('\n', offset)
            stop = len(chunk) if end < 0 else end
            size = stop - offset
            if size:
                self.continued = False
                self.tail = (self.tail + chunk[max(offset, stop - 2):stop])[-2:]
            if not self.dropping:
                if len(self.pending) + size > MAX_LINE:
                    self.pending = ''
                    self.dropping = True
                else:
                    self.pending += chunk[offset:stop]
            if end < 0:
                break
            continuation = self.tail.rstrip('\r').endswith('\\')
            self.continued = continuation
            if continuation and not self.dropping:
                self.pending = self.pending.rstrip('\r')[:-1]
            elif not continuation:
                self.flush()
            self.tail = ''
            offset = end + 1

    def flush(self):
        if self.dropping or self.continued:
            self.rejected += 1
        elif self.pending:
            self.consume(self.pending.rstrip('\r'))
        self.pending = ''
        self.tail = ''
        self.dropping = False
        self.continued = False


class BuildPlan:
    """feed(text chunks) or on_line(newline-free callback); finish on success.

    A failed/truncated/over-budget plan raises ValueError and must not be used.
    Storage is one bounded line and six counters, independent of task count.
    """

    def __init__(self):
        self.counts = dict.fromkeys(KINDS, 0)
        self.ready = False
        self.closed = False
        self._lines = _Lines(self._consume)

    @property
    def total(self):
        return sum(self.counts.values())

    def _consume(self, line):
        task = classify_command(line)
        if task:
            if self.total >= MAX_TASKS:
                raise ValueError('Make plan exceeds task budget')
            self.counts[task.kind] += 1

    def feed(self, chunk):
        if self.closed:
            raise ValueError('Make plan is closed')
        self._lines.feed(chunk)

    def on_line(self, line):
        self.feed(line + '\n')

    def finish(self, success=True):
        if self.closed:
            raise ValueError('Make plan is closed')
        self.closed = True
        self._lines.flush()
        if not success or self._lines.rejected:
            raise ValueError('Make plan failed or contains overlong lines')
        self.ready = True
        return self


class BuildProgress:
    """Serial starts/completions. finish(False) never completes the active task.

    snapshot(): planned/started/completed totals; by_kind dictionaries with the
    same three counts; active Task or None; percent (integer, or None for an
    empty/unreliable plan); finished/success/plan_matches booleans. A mismatch
    disables percentages. Only fully reconciled successful exit yields 100.
    No callbacks, clocks, I/O, mutable plan references or per-task sets retained.
    """

    def __init__(self, plan):
        if not plan.ready:
            raise ValueError('A successful finished make plan is required')
        self.planned = dict(plan.counts)
        self.started = dict.fromkeys(KINDS, 0)
        self.completed = dict.fromkeys(KINDS, 0)
        self.active = None
        self.finished = False
        self.success = False
        self._lines = _Lines(self._consume)

    def _consume(self, line):
        task = classify_command(line)
        if task:
            if sum(self.started.values()) >= MAX_TASKS:
                raise ValueError('Build output exceeds task budget')
            if self.active:
                self.completed[self.active.kind] += 1
            self.started[task.kind] += 1
            self.active = task

    def feed(self, chunk):
        if self.finished:
            raise ValueError('Build progress is closed')
        self._lines.feed(chunk)

    def on_line(self, line):
        self.feed(line + '\n')

    def finish(self, success=True):
        if self.finished:
            raise ValueError('Build progress is closed')
        self._lines.flush()
        if success and self.active:
            self.completed[self.active.kind] += 1
            self.active = None
        self.finished = True
        self.success = bool(success)
        return self.snapshot()

    def snapshot(self):
        planned = sum(self.planned.values())
        completed = sum(self.completed.values())
        matches = not self._lines.rejected and not self._lines.dropping
        matches = matches and all(self.started[k] <= self.planned[k] for k in KINDS)
        if self.finished:
            matches = matches and self.started == self.planned
        percent = None
        if planned and matches:
            percent = min(99, 100 * completed // planned)
            if self.finished and self.success and self.completed == self.planned:
                percent = 100
        return dict(planned=planned, started=sum(self.started.values()), completed=completed,
                    by_kind={k: dict(planned=self.planned[k], started=self.started[k],
                                     completed=self.completed[k]) for k in KINDS},
                    active=self.active, percent=percent, finished=self.finished,
                    success=self.success, plan_matches=bool(matches))
