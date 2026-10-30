# SPDX-License-Identifier: MPL-2.0
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from cook import Cancelled, atomic_json
import resume
from test_cook import Tests as CookTests, executable


class SnapshotTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.stage = self.root / 'working-fixture'
        self.work = self.stage / 'source'
        self.work.mkdir(parents=True)
        (self.work / 'engine.c').write_text('source')
        (self.work / 'texture.png').write_bytes(b'private fixture')
        self.progress = []
        self.callback = lambda *args: self.progress.append(args)
        resume.checkpoint(self.stage, 'recipe', 100, self.callback)

    def tearDown(self):
        self.temp.cleanup()

    def test_round_trip_and_build_outputs_do_not_invalidate_sources(self):
        output = self.work / 'build/original-mips/engine.o'
        output.parent.mkdir(parents=True)
        output.write_bytes(b'generated fixture')
        self.assertEqual(resume.validate(self.stage, 'recipe', self.callback), 100)

    def test_modified_missing_and_extra_sources_are_rejected(self):
        source = self.work / 'engine.c'
        source.write_text('changed')
        with self.assertRaisesRegex(ValueError, 'changed'):
            resume.validate(self.stage, 'recipe', self.callback)
        source.unlink()
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            resume.validate(self.stage, 'recipe', self.callback)
        source.write_text('source')
        (self.work / 'extra.c').write_text('extra')
        with self.assertRaisesRegex(ValueError, 'changed'):
            resume.validate(self.stage, 'recipe', self.callback)

    def test_recipe_and_untrusted_index_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'different build'):
            resume.validate(self.stage, 'another', self.callback)
        path = self.stage / resume.MARKER
        value = json.loads(path.read_text())
        value['files']['../outside'] = {'bytes': 1, 'sha256': '0' * 64}
        path.write_text(json.dumps(value))
        with self.assertRaisesRegex(ValueError, 'Unsafe'):
            resume.validate(self.stage, 'recipe', self.callback)

    def test_active_pointer_and_incomplete_extraction(self):
        atomic_json(self.root / 'active.json', dict(schema=1, recipe='recipe',
            rom_sha1=resume.US_SHA1, generation=self.stage.name))
        self.assertEqual(resume.active_stage(self.root, 'recipe', self.callback), self.stage)
        self.assertIsNone(resume.active_stage(self.root, 'different', self.callback))
        (self.stage / resume.MARKER).unlink()
        self.assertIsNone(resume.active_stage(self.root, 'recipe', self.callback))

    def test_partial_target_is_removed_but_complete_objects_are_kept(self):
        folder = self.work / 'build/original-mips'
        folder.mkdir(parents=True)
        (folder / 'complete.o').write_bytes(b'completed')
        (folder / 'partial.o').write_bytes(b'partial')
        resume.inflight(self.stage, 'build/original-mips/partial.o')
        resume.clean_inflight(self.stage)
        self.assertFalse((folder / 'partial.o').exists())
        self.assertTrue((folder / 'complete.o').is_file())

    def test_unsafe_target_and_cancel_are_rejected(self):
        for name in ('../outside', '/absolute', 'source/engine.c', 'build/original-mips/../escape'):
            with self.assertRaises(ValueError):
                resume.inflight(self.stage, name)
        with self.assertRaises(Cancelled):
            resume.validate(self.stage, 'recipe', self.callback, lambda: True)

    def test_unknown_producer_discards_only_generated_outputs(self):
        folder = self.work / 'build/original-mips'
        folder.mkdir(parents=True)
        (folder / 'partial.o').write_bytes(b'partial')
        resume.inflight_restart(self.stage)
        resume.clean_inflight(self.stage)
        self.assertFalse((self.work / 'build').exists())
        self.assertTrue((self.work / 'engine.c').exists())
        self.assertEqual(resume.validate(self.stage, 'recipe', self.callback), 100)

    @unittest.skipUnless(os.name == 'posix', 'Linux filesystem semantics')
    def test_build_symlink_and_hardlink_are_rejected(self):
        output = self.work / 'build'
        output.symlink_to(self.root, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'link'):
            resume.validate(self.stage, 'recipe', self.callback)
        output.unlink()
        output.mkdir()
        os.link(self.work / 'engine.c', output / 'linked.o')
        with self.assertRaises(ValueError):
            resume.validate(self.stage, 'recipe', self.callback)


@unittest.skipUnless(os.name == 'posix', 'device transaction semantics')
class ResumeTransactionTests(unittest.TestCase):
    setUp = CookTests.setUp
    tearDown = CookTests.tearDown
    def prepare_resume(self, builder):
        actual = __import__('cook').discover
        def discover(folder, cancel=lambda: False):
            return actual(folder, cancel, expected=self.sha1)
        with patch('cook.US_SHA1', self.sha1), patch('resume.US_SHA1', self.sha1), patch('cook.discover', discover):
            from cook import Cache
            return Cache(self.app, 'recipe').prepare(builder, lambda *args: None, resumable=True)

    def test_cancelled_compilation_resumes_without_rom_copy_or_extraction(self):
        seen = []
        def first(stage, progress, cancel):
            work = stage / 'source'
            work.mkdir()
            (stage / 'baserom.us.z64').rename(work / 'baserom.us.z64')
            (work / 'engine.c').write_text('source')
            resume.checkpoint(stage, 'recipe', 100, progress)
            seen.append(stage)
            raise Cancelled('intentional interruption')
        with self.assertRaises(Cancelled):
            self.prepare_resume(first)
        self.assertTrue(self.rom.exists())
        def second(stage, progress, cancel):
            self.assertEqual(stage, seen[0])
            self.assertFalse((stage / 'baserom.us.z64').exists())
            self.assertEqual((stage / 'source/baserom.us.z64').read_bytes(), self.raw)
            (stage / 'game').write_bytes(executable())
            return stage / 'game', 100
        game = self.prepare_resume(second)
        self.assertTrue(game.exists())
        self.assertFalse(self.rom.exists())
        self.assertFalse((self.app / '.sm64-cache/active.json').exists())

    def interrupt_final_copy(self, payload):
        stages = []
        def interrupted(stage, progress, cancel):
            work = stage / 'source'
            work.mkdir()
            (stage / 'baserom.us.z64').rename(work / 'baserom.us.z64')
            (work / 'engine.c').write_text('verified source')
            resume.checkpoint(stage, 'recipe', 100, progress)
            (stage / 'game').write_bytes(payload)
            stages.append(stage)
            raise Cancelled('Interrupted before cache commit')
        with self.assertRaises(Cancelled):
            self.prepare_resume(interrupted)
        self.assertTrue(self.rom.exists())
        return stages[0]

    def test_partial_final_copy_can_resume_and_commit(self):
        stage = self.interrupt_final_copy(b'partial executable')
        def resumed(actual, progress, cancel):
            self.assertEqual(actual, stage)
            self.assertFalse((actual / 'game').exists())
            self.assertEqual((actual / 'source/engine.c').read_text(), 'verified source')
            with (actual / 'game').open('xb') as output:
                output.write(executable())
            return actual / 'game', 100
        game = self.prepare_resume(resumed)
        self.assertEqual(game.read_bytes(), executable())
        self.assertFalse(self.rom.exists())

    def test_linked_pending_game_is_rejected_without_touching_target(self):
        stage = self.interrupt_final_copy(b'partial')
        outside = self.app / 'protected'
        outside.write_bytes(b'keep')
        pending = stage / 'game'
        pending.unlink()
        pending.symlink_to(outside)
        with self.assertRaises(ValueError):
            self.prepare_resume(lambda *_: self.fail('Unsafe output reached builder'))
        self.assertEqual(outside.read_bytes(), b'keep')
        self.assertTrue(pending.is_symlink())
        self.assertTrue(self.rom.exists())
        pending.unlink()
        os.link(outside, pending)
        with self.assertRaises(ValueError):
            self.prepare_resume(lambda *_: self.fail('Shared output reached builder'))
        self.assertEqual(outside.read_bytes(), b'keep')
        self.assertEqual(pending.stat().st_nlink, 2)
        self.assertTrue(self.rom.exists())


del CookTests


if __name__ == '__main__':
    unittest.main()
