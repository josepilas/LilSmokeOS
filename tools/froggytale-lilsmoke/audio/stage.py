#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Checked audio-only transform of FroggyTale 5699418; never changes upstream."""
import argparse
import hashlib
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
PINNED = {
    'src/audio/miniaudio/ma_audio_system.c': '26d3091f783b541c4c156ad8f19671931d903c0afdccf02b3ee83ce566d18711',
    'src/audio/miniaudio/ma_audio_system.h': '253f9293847b6f2676bfbb239d440348d9768d0bdb1d136e0e2d0bdaa38e328d',
    'src/audio_system.h': '793289380189497876a5149d70f027919b13af7f176eef330a15f7bd4e84b458',
    'vendor/miniaudio/miniaudio.h': '1fc9d961182f3b0306d784c55575fd2e099f6b0ba057a1239649b4a700a191e1',
    'vendor/stb/vorbis/stb_vorbis.c': '4a7a46add14c80810ad1f1695f70107207476c4a5474b1cfb0877c83f9b0a148',
}
DEFINES = '\n'.join('#define ' + flag for flag in (
    'MA_NO_DEVICE_IO', 'MA_NO_RESOURCE_MANAGER', 'MA_NO_ENCODING', 'MA_NO_GENERATION',
    'MA_NO_FLAC', 'MA_NO_MP3')) + '\n'


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Upstream audio context changed: ' + old[:100])
    return text.replace(old, new, 1)


def function(text, name, body):
    pattern = r'^static ([^\n]+)\b' + name + r'\(([^\n]*)\) \{.*?^\}'
    match = re.search(pattern, text, re.M | re.S)
    if not match:
        raise ValueError('Missing upstream function ' + name)
    replacement = 'static ' + match[1] + name + '(' + match[2] + ') {\n' + body + '\n}'
    return text[:match.start()] + replacement + text[match.end():]


def stage(upstream, out):
    for relative, digest in PINNED.items():
        # Line endings are immaterial, including fetched sources on NTFS.
        actual = hashlib.sha256((upstream / relative).read_text().encode()).hexdigest()
        if actual != digest:
            raise ValueError('Pinned source mismatch: ' + relative + ' got ' + actual)
    source = (upstream / 'src/audio/miniaudio/ma_audio_system.c').read_text()
    header = (upstream / 'src/audio/miniaudio/ma_audio_system.h').read_text()
    source = '#include <SDL.h>\n' + DEFINES + source
    header = once(header, '#include "miniaudio.h"', '#include <SDL.h>\n' + DEFINES + '#include "miniaudio.h"')
    header = once(header, '#define MAX_SOUND_INSTANCES 128', '#define MAX_SOUND_INSTANCES 32')
    header = once(header, '    ma_device device;', '''    SDL_AudioDeviceID device;
    bool initialized, sdlAudioRef, manualPull, suspended;
    uint64_t framesRead, nonzeroFrames;
    uint32_t pullErrors;''')
    source = once(source, '// ===[ Helpers ]===', '#include "sdl_pull.inc"\n\n// ===[ Helpers ]===')
    source = function(source, 'maInit', '    lsAudioInit((MaAudioSystem*)audio, dataWin, fileSystem);')
    source = function(source, 'maDestroy', '''    MaAudioSystem* ma = (MaAudioSystem*)audio;
    if (ma->device) SDL_CloseAudioDevice(ma->device);
    if (ma->initialized) {
        for (int i = 0; i < MAX_SOUND_INSTANCES; ++i) {
            SoundInstance* inst = &ma->instances[i];
            if (!inst->active) continue;
            ma_sound_uninit(&inst->maSound);
            if (inst->ownsDecoder) ma_decoder_uninit(&inst->decoder);
        }
        for (int i = 0; i < MAX_LISTENERS; ++i) ma_sound_group_uninit(&ma->listenerGroups[i]);
        ma_engine_uninit(&ma->engine);
    }
    for (int i = 0; i < MAX_AUDIO_STREAMS; ++i) free(ma->streams[i].filePath);
    for (ptrdiff_t i = 1; i < arrlen(audio->audioGroups); ++i)
        if (audio->audioGroups[i]) DataWin_free(audio->audioGroups[i]);
    arrfree(audio->audioGroups);
    free(audio->groupGains);
    if (ma->sdlAudioRef) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    free(ma);''')
    source = function(source, 'maSuspend', '''    MaAudioSystem* ma = (MaAudioSystem*)audio;
    if (!ma->initialized) return;
    SDL_PauseAudioDevice(ma->device, 1);
    SDL_LockAudioDevice(ma->device);
    ma->suspended = true;
    SDL_UnlockAudioDevice(ma->device);''')
    source = function(source, 'maResume', '''    MaAudioSystem* ma = (MaAudioSystem*)audio;
    if (!ma->initialized) return;
    SDL_LockAudioDevice(ma->device);
    ma->suspended = false;
    SDL_UnlockAudioDevice(ma->device);
    if (!ma->manualPull) SDL_PauseAudioDevice(ma->device, 0);''')
    source = once(source, 'if (!ma_sound_is_playing(&inst->maSound)) {',
                  'if (ma_sound_at_end(&inst->maSound) && !ma_sound_is_looping(&inst->maSound)) {')
    source = once(source, 'result = ma_sound_init_from_file(&ma->engine, streamPath, BS_SOUND_FLAG_STREAM, &ma->listenerGroups[0], nullptr, &slot->maSound);',
                  'result = lsAudioFile(ma, streamPath, slot);')
    source = once(source, 'result = ma_sound_init_from_file(&ma->engine, path, BS_SOUND_FLAG_ASYNC, &ma->listenerGroups[0], nullptr, &slot->maSound);',
                  'result = lsAudioFile(ma, path, slot);')
    # Both file paths now own incremental decoders, just like embedded entries.
    source = source.replace('        slot->ownsDecoder = false;', '        slot->ownsDecoder = true;')
    start = source.index('            if (0 > sound->audioFile', source.index('static int32_t maPlaySound'))
    end = source.index('            ma_decoder_config decoderConfig', start)
    source = source[:start] + '''            AudioEntry* entry = lsAudioEntry(ma, sound);
            if (!entry) return -1;

''' + source[end:]
    start = source.index('        if (0 > sound->audioFile', source.index('static float maGetSoundLength'))
    end = source.index('        ma_decoder_config decoderConfig', start)
    source = source[:start] + '''        AudioEntry* entry = lsAudioEntry(ma, sound);
        if (!entry) return 0.0f;
''' + source[end:]
    source = source.replace('ma_decoder_config_init_default()', 'lsAudioDecoderConfig()')
    source = once(source, '    // Track unique IDs for disambiguation\n    ma->nextInstanceCounter++;', '')
    source = once(source, '    slot->instanceId = SOUND_INSTANCE_ID_BASE + ma->nextInstanceCounter++;', '''    do {
        if (ma->nextInstanceCounter >= AUDIO_STREAM_INDEX_BASE - SOUND_INSTANCE_ID_BASE)
            ma->nextInstanceCounter = 0;
        slot->instanceId = SOUND_INSTANCE_ID_BASE + ma->nextInstanceCounter++;
    } while (findInstanceById(ma, slot->instanceId) != NULL);''')
    # Set active only after selecting the ID, so the collision test cannot find itself.
    source = once(source, '    slot->active = true;\n    slot->soundIndex', '    slot->soundIndex')
    source = once(source, '    slot->currentGain = volume;', '    slot->active = true;\n    slot->currentGain = volume;')
    # Invalid GML stream ids must not index outside the fixed stream table.
    source = source.replace('        AudioStreamEntry* stream = &ma->streams[streamSlot];',
                            '        if (streamSlot < 0 || streamSlot >= MAX_AUDIO_STREAMS || !ma->streams[streamSlot].active) return' + ' LS_BAD_STREAM;\n        AudioStreamEntry* stream = &ma->streams[streamSlot];')
    # Return type differs between the four stream property methods.
    for name, fallback in [('maSetSoundGain', ''), ('maGetSoundGain', ' 0.0f'),
                           ('maSetSoundPitch', ''), ('maGetSoundPitch', ' 1.0f')]:
        pattern = r'(^static [^\n]+\b' + name + r'\([^\n]*\) \{.*?^\})'
        source = re.sub(pattern, lambda m: m[0].replace(' LS_BAD_STREAM', fallback), source, flags=re.M | re.S)
    # maPlaySound already validates streams; this extra check uses its int fallback.
    source = source.replace(' LS_BAD_STREAM', ' -1')
    source = once(source, '    MaAudioSystem* ma = (MaAudioSystem*) audio;\n\n    if (isValidSoundInstanceId(soundOrInstance)) {\n        SoundInstance* inst = findInstanceById(ma, soundOrInstance);\n        if (inst != nullptr) {\n            ma_sound_seek',
                  '    MaAudioSystem* ma = (MaAudioSystem*) audio;\n    if (!isfinite(positionSeconds) || positionSeconds < 0) return;\n\n    if (isValidSoundInstanceId(soundOrInstance)) {\n        SoundInstance* inst = findInstanceById(ma, soundOrInstance);\n        if (inst != nullptr) {\n            ma_sound_seek')
    source = function(source, 'maGroupLoad', '''    MaAudioSystem* ma = (MaAudioSystem*)audio;
    if (groupIndex <= 0 || groupIndex >= 16 || (uint32_t)groupIndex >= audio->dw->agrp.count) return;
    if (groupIndex < arrlen(audio->audioGroups) && audio->audioGroups[groupIndex]) return;
    char name[64];
    snprintf(name, sizeof(name), "audiogroup%d.dat", groupIndex);
    const char* relative = audio->dw->agrp.audioGroups[groupIndex].path;
    if (!relative) relative = name;
    if (!ma->fileSystem->vtable->fileExists(ma->fileSystem, relative)) return;
    char* path = ma->fileSystem->vtable->resolvePath(ma->fileSystem, relative);
    if (!path) return;
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || (uint64_t)st.st_size > LS_AUDIO_FILE_CAP) {
        free(path); return;
    }
    DataWinParserOptions options = {0};
    options.parseAudo = true;
    options.loadType = DATAWINLOADTYPE_MAP_FILE;
    DataWin* group = DataWin_parse(path, options);
    free(path);
    ptrdiff_t previous = arrlen(audio->audioGroups);
    if (previous <= groupIndex) {
        arrsetlen(audio->audioGroups, groupIndex + 1);
        for (ptrdiff_t i = previous; i <= groupIndex; ++i) audio->audioGroups[i] = NULL;
    }
    audio->audioGroups[groupIndex] = group;''')
    source = function(source, 'maGroupIsLoaded', '''    return groupIndex >= 0 && groupIndex < arrlen(audio->audioGroups) && audio->audioGroups[groupIndex] != NULL;''')
    # Serialize lifecycle/decoder operations against the SDL pull thread. The game
    # still uses precisely the upstream vtable, not a second implementation.
    wrappers = []
    names = re.findall(r'maAudioSystemVtable\.\w+ = (ma\w+);', source)
    for name in names:
        if name in ('maInit', 'maDestroy', 'maSuspend', 'maResume'):
            continue
        match = re.search(r'^static (\w+) ' + name + r'\(([^\n]*)\) \{', source, re.M)
        if not match:
            raise ValueError('Cannot wrap ' + name)
        result, args = match.groups()
        args = args.replace('MAYBE_UNUSED ', '')
        call = ', '.join(param.strip().split()[-1].lstrip('*') for param in args.split(','))
        fallback = {'void': '', 'bool': ' false', 'float': ' 0.0f', 'int32_t': ' -1'}[result]
        operation = name + '(' + call + ');'
        if result != 'void':
            operation = result + ' value = ' + operation
        wrappers.append(f'''static {result} lsLocked_{name}({args}) {{
    MaAudioSystem* ma = (MaAudioSystem*)audio;
    if (!ma->initialized) return{fallback};
    SDL_LockAudioDevice(ma->device);
    {operation}
    SDL_UnlockAudioDevice(ma->device);
    {'return value;' if result != 'void' else ''}
}}
''')
        source = source.replace('= ' + name + ';', '= lsLocked_' + name + ';')
    source = once(source, '// ===[ Vtable ]===', '\n'.join(wrappers) + '\n// ===[ Vtable ]===')
    out.mkdir(parents=True, exist_ok=True)
    (out / 'ma_audio_system.c').write_text('/* SPDX-License-Identifier: AGPL-3.0-only */\n' + source)
    (out / 'ma_audio_system.h').write_text('/* SPDX-License-Identifier: AGPL-3.0-only */\n' + header)
    manifest = dict(upstream_revision='5699418f671dc8bb07dc2e4adc60169b616ab906',
                    inputs=PINNED, outputs={p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                            for p in sorted(out.glob('ma_audio_system.*'))})
    (out / 'audio-stage.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--upstream', type=Path, default=HERE.parent / '_build/upstream')
    parser.add_argument('--out', type=Path, default=HERE / '_build/stage')
    args = parser.parse_args()
    print(stage(args.upstream.resolve(), args.out.resolve()))


if __name__ == '__main__':
    main()
