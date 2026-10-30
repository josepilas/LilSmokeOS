#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Export only an accepted asset-free package for a clean SD installation."""
import argparse
import json
from pathlib import Path
import re
import shutil

from cook import digest_file


def export(stage, native, vm, installer, out, audio):
    required = {
        'native': ('full_native_mips_build', 'legacy_source_verified', 'source_dates_in_future',
                   'cancellation_preserves_original', 'resumes_without_reextracting',
                   'repeat_launch', 'owner_rom_preserved'),
        'clock-vm': ('source_future_date_loop_fixed', 'production_extraction_ui', 'production_application_script',
                     'cancel_preserves_original', 'resumes_without_reextracting',
                     'passed_50_percent', 'native_menu_and_resume', 'repeat_launch',
                     'durable_game_readback', 'owner_rom_preserved', 'filesystem_clean',
                     'root_is_block_backed_readonly'),
        'installer': ('production_package_inspect', 'production_package_install',
                      'all_immutable_file_hash_readback', 'single_root_script',
                      'rom_absent', 'cooked_game_absent', 'storage_unmounted_cleanly'),
    }
    reports = {}
    for name, root in (('native', native), ('clock-vm', vm), ('installer', installer)):
        value = json.loads((root / 'report.json').read_text())
        if value.get('passed') is not True or any(value.get(key) is not True for key in required[name]):
            raise ValueError('Incomplete or failed acceptance: ' + name)
        reports[name] = value
    if not 0 <= reports['clock-vm'].get('guest_clock_epoch', 315532800) < 315532800:
        raise ValueError('The full VM must reproduce the pre-FAT clock')
    vm_report = reports['clock-vm']
    if vm_report.get('swap_kib') is None:
        serial_path = vm / 'serial.log'
        samples = re.findall(r'^SwapTotal:\s+(\d+) kB', serial_path.read_text(), re.M)
        if len(samples) != 1:
            raise ValueError('Missing measured clock-vm swap evidence')
        vm_report['swap_kib'] = int(samples[0])
        vm_report['memory_audit_serial_sha256'] = digest_file(serial_path)
    if (vm_report.get('cpus') != 1 or vm_report.get('ram_mib') != 120 or
            vm_report.get('process_limit_kib') != 131072 or vm_report.get('swap_kib') != 0):
        raise ValueError('Unexpected clock-vm CPU, physical RAM, swap or virtual-address limit')
    if vm_report.get('complete_fat32_vm_resume') is True:
        fresh = vm_report.get('fresh_clock_checkpoint', {})
        fresh_required = ('passed', 'fresh_rom_extraction', 'planning_completed',
                          'native_compilation_started', 'cancel_preserves_original',
                          'resumes_without_reextracting')
        expected_targets = {'build/original-mips/src/pc/gfx/gfx_soft.o',
                            'build/original-mips/sound/sound_data.o',
                            'build/original-mips/levels/castle_inside/leveldata.o',
                            'build/original-mips/assets/mario_anim_data.o',
                            'build/original-mips/actors/group12.o',
                            'build/original-mips/src/game/behavior_actions.o',
                            'build/original-mips/src/game/camera.o',
                            'build/original-mips/sm64.us.f3dex2e'}
        selected = set(vm_report.get('selected_vm_compile_and_link_targets', []))
        covered = selected
        prior = vm_report.get('prior_vm_compilation')
        if prior:
            pending = {'build/original-mips/sound/sound_data.o', 'build/original-mips/sm64.us.f3dex2e'}
            done = set(prior.get('completed_compiler_targets', []))
            if (any(prior.get(key) is not True for key in ('passed', 'filesystem_clean', 'no_oom',
                    'only_asm_header_fix', 'all_immutable_runtime_equal_except_bounded_fix')) or
                    prior.get('uncommitted_game_recovery_fix') is not True or
                    selected != pending or done != expected_targets - pending or
                    set(prior.get('object_sha256', {})) != done or
                    any(not re.fullmatch('[a-f0-9]{64}', value) for value in prior.get('object_sha256', {}).values()) or
                    prior.get('reference_complete_game_sha256') != reports['native'].get('game_sha256') or
                    prior.get('new_native_source_sha256') != vm_report.get('runtime_sources', {}).get('native_build.py') or
                    prior.get('new_cook_source_sha256') != vm_report.get('runtime_sources', {}).get('cook.py') or
                    prior.get('cpus') != 1 or prior.get('ram_mib') != 120 or prior.get('swap_kib') != 0):
                raise ValueError('Incomplete bounded predecessor VM compiler evidence')
            covered |= done
            if set(vm_report.get('covered_vm_compile_and_link_targets', [])) != covered:
                raise ValueError('Predecessor coverage was not declared honestly')
        if (any(fresh.get(key) is not True for key in fresh_required) or
                not 0 <= fresh.get('guest_clock_epoch', 315532800) < 315532800 or
                not (fresh.get('completed_tasks_at_stop', 0) >= 8 or
                    (fresh.get('clean_compile_checkpoint') is True and
                     fresh.get('storage_cleanly_unmounted') is True and fresh.get('filesystem_clean') is True)) or
                covered != expected_targets or vm_report.get('uncommitted_final_output_recovery') is not True or
                vm_report.get('verified_snapshot_game_sha256') != reports['native'].get('game_sha256') or
                vm_report.get('game_sha256') != reports['native'].get('game_sha256') or
                not reports['native'].get('game_sha256') or
                vm_report.get('full_game_compiled') is not False):
            raise ValueError('Incomplete split-build clock-vm acceptance')
    elif (vm_report.get('complete_fat32_vm_preparation') is not True or
          vm_report.get('full_game_compiled') is not True):
        raise ValueError('Incomplete full-build clock-vm acceptance')
    audit = json.loads((native / 'progress-audit.json').read_text())
    if (audit.get('passed') is not True or audit.get('plan_matches') is not True or
            audit.get('percent') != 100 or audit.get('completed') != audit.get('planned') or
            audit.get('by_kind', {}).get('texture', {}).get('completed', 0) < 100 or
            audit.get('by_kind', {}).get('compile', {}).get('completed', 0) < 10):
        raise ValueError('Completed native build does not match the progress plan')
    if reports['clock-vm'].get('rendered_game_frames') != 900 or reports['native'].get('rendered_preflight_frames') != 30:
        raise ValueError('Missing actual rendered frame verification')
    package = json.loads((stage / 'package-report.json').read_text())
    for key in ('rom_included', 'cooked_game_included', 'renderer_binary_included'):
        if package.get(key) is not False:
            raise ValueError('Asset-exclusion review missing')
    name = package['package']
    if Path(name).name != name or name != 'SM64-LilSmoke-0.0.1-prep.7.lilpck':
        raise ValueError('Unexpected package identity')
    archive = stage / name
    if digest_file(archive) != package['sha256'] or digest_file(installer / 'package-input' / name) != package['sha256']:
        raise ValueError('Accepted package changed')
    app = stage / 'Super Mario 64'
    audio_proof = json.loads((audio / 'report.json').read_text())
    if (audio_proof.get('passed') is not True or audio_proof.get('data_bytes', 0) < 2 * 1024 * 1024 or
            audio_proof.get('game_audio_bytes_unchanged') is not True or
            audio_proof.get('all_four_symbol_offsets_sizes_and_bindings_equal') is not True or
            audio_proof.get('assembly_source_sha256') != digest_file(app / 'runtime/sound_data.S')):
        raise ValueError('Original audio data/symbol equivalence was not verified')
    if vm_report.get('launcher_sha256') != digest_file(app / 'SM64.sh'):
        raise ValueError('The actual application launcher was not tested')
    for module in ('cook.py', 'native_build.py', 'resume.py', 'build_progress.py'):
        actual = digest_file(app / 'runtime' / module)
        if any(value.get('runtime_sources', {}).get(module) != actual
               for value in (reports['native'], reports['clock-vm'])):
            raise ValueError('The tested runtime changed: ' + module)
        if vm_report.get('complete_fat32_vm_resume') and fresh.get('runtime_sources', {}).get(module) != actual:
            raise ValueError('Fresh-clock extraction used a different runtime: ' + module)
    manifest_path = app / 'runtime/distribution-manifest.json'
    manifest = json.loads(manifest_path.read_text())
    native_runtime = native / 'Test App/runtime'
    for path, row in manifest['files'].items():
        if path.startswith('runtime/'):
            if digest_file(native_runtime / path[len('runtime/'):]) != row['sha256']:
                raise ValueError('Cannot reuse full native build for changed runtime: ' + path)
    meta = json.loads((app / 'meta.json').read_text())
    if meta['id'] != 'org.lilsmoke.sm64' or meta['version'] != '0.0.1-prep.7':
        raise ValueError('Application metadata differs')
    if reports['installer']['verified_files'] != len(manifest['files']) + 1:
        raise ValueError('Installer file count differs')
    out.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(archive, out / name)
    shutil.copyfile(manifest_path, out / 'distribution-manifest.json')
    for label, report in reports.items():
        (out / (label + '-report.json')).write_text(json.dumps(report, indent=2) + '\n')
    (out / 'progress-audit.json').write_text(json.dumps(audit, indent=2) + '\n')
    (out / 'audio-data-equivalence.json').write_text(json.dumps(audio_proof, indent=2) + '\n')
    for image in ('assets-extracted', 'past-50', 'game', 'game-menu'):
        shutil.copyfile(vm / (image + '.png'), out / (image + '.png'))
    result = dict(schema=1, passed=True, fresh_install=True, app_id=meta['id'], version=meta['version'],
        package=name, bytes=archive.stat().st_size, sha256=package['sha256'],
        manifest_sha256=digest_file(manifest_path), files=len(manifest['files']) + 1,
        rom_included=False, cooked_game_included=False, renderer_binary_included=False,
        vm_full_fresh_compilation=vm_report.get('full_game_compiled'),
        vm_resume_from_verified_native_build=vm_report.get('complete_fat32_vm_resume', False),
        runtime_equal_to_full_native_build=True,
        physical_hc16xx_tested=False)
    (out / 'deployment.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stage', 'native', 'vm', 'installer', 'out', 'audio'):
        parser.add_argument('--' + name, type=Path, required=True)
    print(json.dumps(export(**vars(parser.parse_args())), indent=2))
