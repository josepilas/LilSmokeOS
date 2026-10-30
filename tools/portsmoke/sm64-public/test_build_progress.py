# SPDX-License-Identifier: MPL-2.0
"""Asset-free QA: python3 -B -m unittest -v test_build_progress."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import tracemalloc
import unittest
from unittest.mock import patch

from build_progress import BuildPlan, BuildProgress, MAX_LINE, classify_command


GRAPHICS = 'tools/n64graphics -i "build/actor one.rgba16" -g "actor one.png" -f rgba16'
HEX = 'hexdump -v -e \'1/1 "0x%X,"\' "build/actor one.rgba16" > "build/actor one.inc.c"'
COMPILE = '\'/runtime/Super Mario 64/compiler/wrappers/cc\' -O2 -c "src/file one.c" -o "build/file one.o"'
LINK = '\'/runtime/Super Mario 64/compiler/wrappers/cc\' -nostdlib -o "build/game one" "build/file one.o" -lc'


def plan_for(*lines):
    plan = BuildPlan()
    for line in lines:
        plan.on_line(line)
    return plan.finish()


class ClassifierTests(unittest.TestCase):
    def test_representative_trusted_recipes(self):
        cases = [(GRAPHICS, 'texture'), (HEX, 'texture'), (COMPILE, 'compile'), (LINK, 'link'),
                 ('"/runtime/bin/bin2c" "build/sound/table" > "build/sound/table.inc.c"', 'audio'),
                 ('"/Super Mario 64/runtime/bin/sm64-bin2c" "build/texture.rgba16" > "build/texture.inc.c"', 'texture'),
                 ('"/Super Mario 64/runtime/bin/sm64-bin2c" "build/sound/table" > "build/sound/table.inc.c"', 'audio'),
                 ('hexdump -v -e \'1/1 "0x%X,"\' build/sound/bank_sets >build/sound/bank_sets.inc.c', 'audio'),
                 ('tools/skyconv --type sky --split image.png out.c', 'texture'),
                 ('tools/n64graphics_ci -i x.ci8 -g x.png -f ci8', 'texture'),
                 ('mipsel-linux-as -o build/sound/a.o sound/a.s', 'assembly'),
                 ('cc -c sound/a.S -o a.o', 'assembly'),
                 ('mipsel-linux-objcopy -j .rodata a.o -O binary a.m64', 'assembly'),
                 ('mipsel-linux-ld -o game a.o', 'link'),
                 ('mipsel-linux-ar rcs -o lib.a a.o', 'link'),
                 ('tools/aiff_extract_codebook sound/a.aiff >build/sound/a.table', 'audio'),
                 ('tools/vadpcm_enc -c a.table a.aiff a.aifc', 'audio'),
                 ('"/app with spaces/python3.11" tools/assemble_sound.py --sequences out banksets '
                  'banks sequences.json -DVERSION_US $(cat build/endian-and-bitwidth)', 'audio'),
                 ('python3 tools/mario_anims_converter.py >build/anim.c', 'generate'),
                 ('python3 tools/demo_data_converter.py assets/demo_data.json > build/demo.c', 'generate'),
                 ('cc -E -x c text.c -o - | tools/textconv charmap.txt - text.inc.c', 'generate'),
                 ('cpp -I . levels/level_headers.h.in | python3 tools/output_level_headers.py > out.h', 'generate'),
                 ('tools/mio0 in.bin out.mio0', 'generate'),
                 ('env LC_ALL=C ccache mipsel-linux-gcc -c a.c -o a.o', 'compile')]
        for line, kind in cases:
            with self.subTest(line=line):
                self.assertEqual(classify_command(line).kind, kind)
        self.assertEqual(classify_command(COMPILE).target, 'build/file one.o')
        self.assertEqual(classify_command(HEX).target, 'build/actor one.inc.c')

    def test_diagnostics_housekeeping_and_silent_checks_do_not_count(self):
        lines = ['', 'make: Entering directory /tmp', 'make: *** [all] Error 1',
                 'file.c: warning: cc -c bad.c -o bad.o', 'echo "cc -c a.c -o a.o"',
                 'echo >> out.inc.c', 'mkdir -p build', 'true', 'rm -f scratch',
                 'echo ";" cc -c a.c -o a.o', 'diagnostic: failed; tools/n64graphics -i output',
                 'echo "|" tools/n64graphics -i output',
                 '@cc -fsyntax-only -MMD -MP -MT a.o -MF a.d a.c',
                 'cc -c -o endian.dummy2 tools/determine-endian-bitwidth.c 2>endian.dummy1; true',
                 'hexdump -C data.bin', 'cc -c "unterminated', 'false && cc -c a.c -o a.o',
                 'false && python3 tools/assemble_sound.py out $(cat endian)',
                 'cc -c a.c -o a.o &', 'if true; then cc -c a.c -o a.o; fi']
        for line in lines:
            with self.subTest(line=line):
                self.assertIsNone(classify_command(line))

    def test_escaped_paths_and_compiler_version(self):
        task = classify_command(r'/runtime/Super\ Mario\ 64/gcc-12 -c source\ file.c -o result\ file.o')
        self.assertEqual((task.kind, task.target), ('compile', 'result file.o'))

    def test_primary_targets_for_resume_journal(self):
        build = 'build/original-mips/'
        cases = [
            ('tools/n64graphics -i ' + build + 'texture.rgba16 -g texture.png -f rgba16', 'texture.rgba16'),
            ('"/Super Mario 64/runtime/bin/sm64-bin2c" "' + build + 'texture.rgba16" > "' +
             build + 'texture.inc.c"', 'texture.inc.c'),
            ('cc -c src/main.c -o ' + build + 'src/main.o', 'src/main.o'),
            ('cc -o ' + build + 'game obj.o -lc', 'game'),
            ('as -o ' + build + 'sound/a.o sound/a.s', 'sound/a.o'),
            ('objcopy -j .rodata sound/a.o -O binary ' + build + 'sound/a.m64', 'sound/a.m64'),
            ('tools/vadpcm_enc -c a.table a.aiff ' + build + 'sound/a.aifc', 'sound/a.aifc'),
            ('tools/aiff_extract_codebook a.aiff >' + build + 'sound/a.table', 'sound/a.table'),
            ('python3 tools/assemble_sound.py samples banks ' + build + 'sound/data.ctl ' +
             build + 'sound/data.tbl -DVERSION_US $(cat ' + build + 'endian)', 'sound/data.ctl'),
            ('python3 tools/assemble_sound.py --sequences ' + build + 'sound/sequences.bin ' +
             build + 'sound/bank_sets banks seq.json $(cat ' + build + 'endian)', 'sound/sequences.bin'),
            ('cc -E -x c text.c -o - | tools/textconv charmap.txt - ' + build + 'text.inc.c', 'text.inc.c'),
            ('cpp headers.h.in | python3 tools/output_level_headers.py >' + build + 'include/headers.h', 'include/headers.h'),
            ('tools/skyconv --type sky --split textures/clouds.png ' + build + 'bin', 'bin/clouds_skybox.c'),
            ('tools/skyconv --type cake --split levels/ending/cake.png ' + build + 'levels/ending', 'levels/ending/cake.inc.c'),
            ('tools/skyconv --type cake-eu --split levels/ending/cake.png ' + build + 'levels/ending', 'levels/ending/cake_eu.inc.c'),
        ]
        for line, target in cases:
            with self.subTest(line=line):
                self.assertEqual(classify_command(line).target, build + target)
        self.assertEqual(classify_command('cc -E -x c a.c -o -').kind, 'generate')


class ProgressTests(unittest.TestCase):
    def test_plan_is_chunk_boundary_independent_and_handles_continuations(self):
        text = GRAPHICS + '\r\n' + HEX + '\n' + COMPILE.replace(' -o ', ' \\\n -o ') + '\n' + LINK
        for chunk_size in (1, 7, 8192):
            plan = BuildPlan()
            for i in range(0, len(text), chunk_size):
                plan.feed(text[i:i + chunk_size])
            plan.finish()
            self.assertEqual(plan.total, 4)
            self.assertEqual(plan.counts['texture'], 2)
            self.assertEqual(plan.counts['compile'], 1)
            self.assertEqual(plan.counts['link'], 1)

    def test_conversion_moves_real_completed_progress_before_c_compile(self):
        live = BuildProgress(plan_for(GRAPHICS, HEX, COMPILE, LINK))
        live.on_line(GRAPHICS)
        self.assertEqual((live.snapshot()['started'], live.snapshot()['completed']), (1, 0))
        live.on_line('n64graphics: diagnostic output, not completion')
        self.assertEqual(live.snapshot()['percent'], 0)
        live.on_line(HEX)
        self.assertEqual(live.snapshot()['percent'], 25)
        live.on_line('echo >> "build/actor one.inc.c"')
        self.assertEqual(live.snapshot()['completed'], 1)
        live.on_line(COMPILE)
        self.assertEqual(live.snapshot()['by_kind']['texture']['completed'], 2)
        self.assertEqual(live.snapshot()['percent'], 50)
        live.on_line(LINK)
        self.assertEqual(live.snapshot()['percent'], 75)
        final = live.finish()
        self.assertEqual((final['started'], final['completed'], final['percent']), (4, 4, 100))
        self.assertIsNone(final['active'])

    def test_failure_or_cancel_does_not_complete_last_task(self):
        for last in (GRAPHICS, HEX, COMPILE, LINK):
            live = BuildProgress(plan_for(GRAPHICS, last))
            live.on_line(GRAPHICS)
            live.on_line(last)
            live.on_line('make: *** [all] Error 1')
            final = live.finish(False)
            self.assertEqual(final['completed'], 1)
            self.assertEqual(final['active'].kind, classify_command(last).kind)
            self.assertEqual(final['percent'], 50)
            self.assertFalse(final['success'])

    def test_no_fabricated_work_when_plan_is_empty_short_or_exceeded(self):
        empty = BuildProgress(plan_for())
        self.assertIsNone(empty.finish()['percent'])
        short = BuildProgress(plan_for(GRAPHICS, HEX, COMPILE))
        short.on_line(GRAPHICS)
        self.assertIsNone(short.finish()['percent'])
        extra = BuildProgress(plan_for(COMPILE))
        extra.on_line(GRAPHICS)
        extra.on_line(COMPILE)
        self.assertIsNone(extra.finish()['percent'])
        self.assertEqual(extra.snapshot()['completed'], 2)

    def test_plan_copy_and_snapshot_do_not_leak_mutable_counters(self):
        plan = plan_for(COMPILE)
        live = BuildProgress(plan)
        plan.counts['compile'] = 900
        live.snapshot()['by_kind']['compile']['planned'] = 800
        self.assertEqual(live.snapshot()['planned'], 1)

    def test_requires_successful_plan_and_enforces_closed_lifecycle(self):
        with self.assertRaises(ValueError):
            BuildProgress(BuildPlan())
        failed = BuildPlan()
        with self.assertRaises(ValueError):
            failed.finish(False)
        self.assertFalse(failed.ready)
        good = plan_for(COMPILE)
        with self.assertRaises(ValueError):
            good.on_line(COMPILE)
        live = BuildProgress(good)
        live.finish(False)
        with self.assertRaises(ValueError):
            live.on_line(COMPILE)
        with self.assertRaises(ValueError):
            live.finish()

    def test_overlong_lines_discard_through_newline_and_resynchronize(self):
        plan = BuildPlan()
        plan.feed('x' * (MAX_LINE + 10))
        self.assertLessEqual(len(plan._lines.pending), MAX_LINE)
        plan.feed(COMPILE + '\n' + HEX + '\n')
        self.assertEqual(plan.total, 1)
        with self.assertRaisesRegex(ValueError, 'overlong'):
            plan.finish()
        live = BuildProgress(plan_for(HEX))
        live.feed('x' * (MAX_LINE + 10) + '\n' + HEX + '\n')
        self.assertEqual(live.snapshot()['started'], 1)
        self.assertIsNone(live.finish()['percent'])

    def test_overlong_continued_line_does_not_turn_suffix_into_command(self):
        plan = BuildPlan()
        plan.feed('x' * MAX_LINE + '\\\n' + COMPILE + '\n' + HEX + '\n')
        self.assertEqual(plan.total, 1)
        with self.assertRaises(ValueError):
            plan.finish()

    def test_unfinished_continuation_is_not_a_finished_plan(self):
        plan = BuildPlan()
        plan.feed(COMPILE + ' \\\n')
        with self.assertRaises(ValueError):
            plan.finish()
        self.assertEqual(plan.total, 0)

    def test_continued_command_with_final_line_without_newline(self):
        plan = BuildPlan()
        plan.feed('cc -c "src/file one.c" \\\n')
        plan.feed('-o "build/file one.o"')
        plan.finish()
        self.assertEqual(plan.total, 1)

    def test_stream_memory_does_not_grow_with_command_history(self):
        tracemalloc.start()
        try:
            plan = BuildPlan()
            for _ in range(5000):
                plan.on_line(COMPILE)
            plan.finish()
            live = BuildProgress(plan)
            for _ in range(5000):
                live.on_line(COMPILE)
            self.assertEqual(live.finish()['completed'], 5000)
            _, peak = tracemalloc.get_traced_memory()
        finally:
            tracemalloc.stop()
        self.assertEqual(plan.total, 5000)
        self.assertLess(peak, 512 * 1024)

    def test_explicit_task_budget(self):
        with patch('build_progress.MAX_TASKS', 1):
            plan = BuildPlan()
            plan.on_line(COMPILE)
            with self.assertRaisesRegex(ValueError, 'budget'):
                plan.on_line(COMPILE)

    @unittest.skipUnless(shutil.which('make') and shutil.which('cc'), 'GNU make/cc required')
    def test_real_make_dry_run_and_live_output_with_quoted_paths(self):
        with tempfile.TemporaryDirectory(prefix='sm64 progress ') as folder:
            root = Path(folder)
            (root / 'source one.c').write_text('int main(void) { return 0; }\n')
            (root / 'Makefile').write_text(
                '.PHONY: all\nall:\n'
                '\t@cc -fsyntax-only "source one.c"\n'
                '\tcc -c "source one.c" -o "object one.o"\n'
                '\tcc "object one.o" -o "game one"\n')
            dry = subprocess.run(['make', '-j1', '-n'], cwd=root, capture_output=True, text=True, check=True)
            plan = BuildPlan()
            plan.feed(dry.stdout)
            plan.finish()
            self.assertEqual(plan.total, 2)
            live = BuildProgress(plan)
            with subprocess.Popen(['make', '-j1'], cwd=root, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True) as process:
                for line in process.stdout:
                    live.feed(line)
                status = process.wait()
            self.assertEqual(status, 0)
            self.assertEqual(live.finish()['percent'], 100)


if __name__ == '__main__':
    unittest.main()
