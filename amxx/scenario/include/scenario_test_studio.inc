// SPDX-License-Identifier: GPL-3.0-or-later
#if defined _scenario_test_studio_included
    #endinput
#endif
#define _scenario_test_studio_included

// Studio v10 mstudioseqdesc_t and mstudioevent_t. Read once during map precache.
// Only monster voice/body sound events are consumed by these stationary entities.
enum _:StudioCue { Float:CueTime, CueChannel, CueSample[64] };
enum _:StudioTrack { ClipIndex, Float:ClipDuration, ClipFirstCue, ClipCueCount };
const MAX_CREATURE_CUES = 32;

stock ReadStudioInt(file, at)
{
    new value;
    fseek(file, at, SEEK_SET);
    fread(file, value, BLOCK_INT);
    return value;
}

stock bool:ReadCreatureTrack(file, size, table, count, const label[], const soundDir[],
    track[StudioTrack], cues[][StudioCue], &used)
{
    for (new i; i < count; i++)
    {
        new base = table + i * 176, name[33];
        fseek(file, base, SEEK_SET);
        fread_blocks(file, name, 32, BLOCK_BYTE);
        if (!equal(name, label)) continue;
        new Float:fps = Float:ReadStudioInt(file, base + 32);
        new events = ReadStudioInt(file, base + 48), start = ReadStudioInt(file, base + 52);
        new frames = ReadStudioInt(file, base + 56);
        if (!(fps > 0.0 && fps <= 256.0) || frames < 2 || events < 0 || events > 64 ||
            start < 0 || start > size || events > (size-start)/76) return false;
        track[ClipIndex] = i;
        track[ClipDuration] = float(frames-1)/fps;
        track[ClipFirstCue] = used;
        for (new e; e < events; e++)
        {
            new at = start+76*e;
            new code = ReadStudioInt(file, at+4);
            if (code != 1004 && code != 1008) continue;
            new frame = ReadStudioInt(file, at), sample[65], path[80];
            fseek(file, at+12, SEEK_SET);
            fread_blocks(file, sample, 64, BLOCK_BYTE);
            if (frame < 0 || frame >= frames || used >= MAX_CREATURE_CUES ||
                strlen(sample) >= 64 || contain(sample, soundDir) != 0 || contain(sample, "..") != -1)
                return false;
            formatex(path, charsmax(path), "sound/%s", sample);
            if (!file_exists(path)) return false;
            precache_sound(sample);
            cues[used][CueTime] = float(frame)/fps;
            cues[used][CueChannel] = (code == 1008) ? CHAN_VOICE : CHAN_BODY;
            copy(cues[used][CueSample], 63, sample);
            used++;
        }
        track[ClipCueCount] = used-track[ClipFirstCue];
        return true;
    }
    return false;
}
