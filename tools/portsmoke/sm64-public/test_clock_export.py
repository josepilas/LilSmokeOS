# SPDX-License-Identifier: MPL-2.0
import json
from pathlib import Path
import tempfile
import unittest

from export_clock_update import export


class ClockExportGuards(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.args = {name: self.root / name for name in ('stage', 'native', 'vm', 'installer', 'out', 'audio')}
        for name in ('native', 'vm', 'installer'):
            self.args[name].mkdir()

    def report(self, role, fields):
        (self.args[role] / 'report.json').write_text(json.dumps(fields))

    def test_failed_native_run_never_exports(self):
        self.report('native', dict(passed=False))
        with self.assertRaisesRegex(ValueError, 'Incomplete or failed'):
            export(**self.args)
        self.assertFalse(self.args['out'].exists())

    def test_generic_pass_does_not_replace_complete_clock_testing(self):
        self.report('native', dict.fromkeys(('passed', 'full_native_mips_build', 'legacy_source_verified',
            'source_dates_in_future', 'cancellation_preserves_original', 'resumes_without_reextracting',
            'repeat_launch', 'owner_rom_preserved'), True))
        self.report('vm', dict(passed=True))
        with self.assertRaisesRegex(ValueError, 'clock-vm'):
            export(**self.args)
        self.assertFalse(self.args['out'].exists())

    def test_missing_source_future_date_coverage_is_rejected(self):
        value = dict.fromkeys(('passed', 'full_native_mips_build', 'legacy_source_verified',
            'cancellation_preserves_original', 'resumes_without_reextracting',
            'repeat_launch', 'owner_rom_preserved'), True)
        self.report('native', value)
        with self.assertRaisesRegex(ValueError, 'native'):
            export(**self.args)
        self.assertFalse(self.args['out'].exists())

    def split_reports(self):
        native = dict.fromkeys(('passed', 'full_native_mips_build', 'legacy_source_verified',
            'source_dates_in_future', 'cancellation_preserves_original', 'resumes_without_reextracting',
            'repeat_launch', 'owner_rom_preserved'), True)
        native['game_sha256'] = 'a' * 64
        self.report('native', native)
        vm = dict.fromkeys(('passed', 'source_future_date_loop_fixed', 'production_extraction_ui',
            'cancel_preserves_original', 'resumes_without_reextracting', 'passed_50_percent',
            'native_menu_and_resume', 'repeat_launch', 'durable_game_readback',
            'owner_rom_preserved', 'filesystem_clean', 'complete_fat32_vm_resume',
            'production_application_script', 'root_is_block_backed_readonly',
            'uncommitted_final_output_recovery'), True)
        vm.update(guest_clock_epoch=28800, full_game_compiled=False,
            cpus=1, ram_mib=120, swap_kib=0, process_limit_kib=131072,
            verified_snapshot_game_sha256='a' * 64,
            game_sha256='a' * 64,
            selected_vm_compile_and_link_targets=['build/original-mips/src/pc/gfx/gfx_soft.o',
                'build/original-mips/sound/sound_data.o',
                'build/original-mips/levels/castle_inside/leveldata.o',
                'build/original-mips/assets/mario_anim_data.o',
                'build/original-mips/actors/group12.o',
                'build/original-mips/src/game/behavior_actions.o',
                'build/original-mips/src/game/camera.o',
                'build/original-mips/sm64.us.f3dex2e'])
        fresh = dict.fromkeys(('passed', 'fresh_rom_extraction', 'planning_completed',
            'native_compilation_started', 'cancel_preserves_original', 'resumes_without_reextracting'), True)
        fresh.update(guest_clock_epoch=28800, completed_tasks_at_stop=12)
        vm['fresh_clock_checkpoint'] = fresh
        installer = dict.fromkeys(('passed', 'production_package_inspect', 'production_package_install',
            'all_immutable_file_hash_readback', 'single_root_script', 'rom_absent',
            'cooked_game_absent', 'storage_unmounted_cleanly'), True)
        self.report('installer', installer)
        return vm

    def test_resume_cannot_claim_full_fresh_vm_compilation(self):
        vm = self.split_reports()
        vm['full_game_compiled'] = True
        self.report('vm', vm)
        with self.assertRaisesRegex(ValueError, 'split-build'):
            export(**self.args)
        self.assertFalse(self.args['out'].exists())

    def test_resume_requires_independent_fresh_clock_extraction(self):
        vm = self.split_reports()
        vm['fresh_clock_checkpoint']['fresh_rom_extraction'] = False
        self.report('vm', vm)
        with self.assertRaisesRegex(ValueError, 'split-build'):
            export(**self.args)
        self.assertFalse(self.args['out'].exists())

    def test_resume_requires_native_commit_hash_match(self):
        vm = self.split_reports()
        vm['verified_snapshot_game_sha256'] = 'b' * 64
        self.report('vm', vm)
        with self.assertRaisesRegex(ValueError, 'split-build'):
            export(**self.args)
        self.assertFalse(self.args['out'].exists())

    def test_virtual_address_headroom_cannot_raise_physical_ram(self):
        vm = self.split_reports()
        vm['ram_mib'] = 256
        self.report('vm', vm)
        with self.assertRaisesRegex(ValueError, 'physical RAM'):
            export(**self.args)
        self.assertFalse(self.args['out'].exists())

    def test_resume_requires_large_data_and_complex_gameplay_jobs(self):
        vm = self.split_reports()
        vm['selected_vm_compile_and_link_targets'].remove(
            'build/original-mips/src/game/behavior_actions.o')
        self.report('vm', vm)
        with self.assertRaisesRegex(ValueError, 'split-build'):
            export(**self.args)
        self.assertFalse(self.args['out'].exists())

    def test_ram_root_cannot_replace_block_backed_memory_acceptance(self):
        vm = self.split_reports()
        vm['root_is_block_backed_readonly'] = False
        self.report('vm', vm)
        with self.assertRaisesRegex(ValueError, 'clock-vm'):
            export(**self.args)
        self.assertFalse(self.args['out'].exists())

    def test_unverified_predecessor_cannot_replace_compiler_jobs(self):
        vm = self.split_reports()
        vm['prior_vm_compilation'] = {'passed': True}
        self.report('vm', vm)
        with self.assertRaisesRegex(ValueError, 'predecessor'):
            export(**self.args)
        self.assertFalse(self.args['out'].exists())

    def test_resume_must_recover_interrupted_final_output(self):
        vm = self.split_reports()
        vm['uncommitted_final_output_recovery'] = False
        self.report('vm', vm)
        with self.assertRaisesRegex(ValueError, 'split-build'):
            export(**self.args)
        self.assertFalse(self.args['out'].exists())

    def test_actual_vm_game_must_match_independent_complete_build(self):
        vm = self.split_reports()
        vm['game_sha256'] = 'b' * 64
        self.report('vm', vm)
        with self.assertRaisesRegex(ValueError, 'split-build'):
            export(**self.args)
        self.assertFalse(self.args['out'].exists())


if __name__ == '__main__':
    unittest.main()
