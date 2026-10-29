"""Rebind legacy SEQ animation channels to a revised named studio skeleton.

Only equal rest poses and parent names are accepted. CSO SEQ tracks use fixed
channel units, independent of either model's bone scales; new bones keep their bind pose.
The caller must supply both verified model versions and record their hashes.
"""
import struct


def word(data, at):
    return struct.unpack_from('<i', data, at)[0]


def skeleton(model):
    count, offset = struct.unpack_from('<ii', model, 140)
    if not 0 < count <= 128 or offset < 244 or offset + count * 112 > len(model):
        raise ValueError('Invalid skeleton')
    result = []
    for i in range(count):
        at = offset + i * 112
        name = model[at:at + 32].split(b'\0')[0].decode('ascii')
        parent = word(model, at + 32)
        if parent >= i or parent < -1:
            raise ValueError('Invalid skeleton hierarchy')
        result.append(dict(name=name, parent=parent, pose=struct.unpack_from('<6f', model, at + 64),
                           scale=struct.unpack_from('<6f', model, at + 88)))
    if len({b['name'] for b in result}) != count:
        raise ValueError('Ambiguous bone names')
    return result


def decode_channel(block, table, axis, frames):
    offset = struct.unpack_from('<H', block, table + axis * 2)[0]
    if not offset:
        return [0] * frames
    cursor, result = table + offset, []
    while len(result) < frames:
        valid, total = struct.unpack_from('<BB', block, cursor)
        if not 0 < valid <= total:
            raise ValueError('Invalid animation RLE')
        values = struct.unpack_from('<' + 'h' * valid, block, cursor + 2)
        result.extend(values)
        result.extend([values[-1]] * (total - valid))
        cursor += (valid + 1) * 2
    return result[:frames]


def encode_channel(values):
    # RLE run: changing prefix followed by repeated final value, max255 frames.
    result, cursor = bytearray(), 0
    while cursor < len(values):
        end = min(cursor + 255, len(values))
        valid = end - cursor
        while valid > 1 and values[cursor + valid - 1] == values[cursor + valid - 2]:
            valid -= 1
        result += struct.pack('<BB', valid, end - cursor)
        result += struct.pack('<' + 'h' * valid, *values[cursor:cursor + valid])
        cursor = end
    return result


def rebind(seq, old_model, new_model):
    old, new = skeleton(old_model), skeleton(new_model)
    by_name = {b['name']: i for i, b in enumerate(old)}
    mapping = [by_name.get(b['name']) for b in new]
    for i, source in enumerate(mapping):
        if source is None:
            continue
        a, b = old[source], new[i]
        parent = lambda bones, bone: bones[bone['parent']]['name'] if bone['parent'] >= 0 else None
        if parent(old, a) != parent(new, b) or max(abs(x - y) for x, y in zip(a['pose'], b['pose'])) > 1e-5:
            raise ValueError('Changed rest pose or parent: ' + b['name'])
    count, index = struct.unpack_from('<ii', seq)
    if not 0 < count <= 2048 or index + count * 176 != len(seq):
        raise ValueError('Invalid SEQ descriptors')
    descriptors = bytearray(seq[index:])
    cursor = 8
    for i in range(count):
        cursor += word(descriptors, i * 176 + 48) * 76 + word(descriptors, i * 176 + 60) * 20
    result = bytearray(seq[:cursor])
    comparisons, max_error = 0, 0.0
    for i in range(count):
        at = i * 176
        frames, blends = word(descriptors, at + 56), word(descriptors, at + 120)
        if not 0 < frames <= 65536 or not 0 < blends <= 16:
            raise ValueError('Invalid animation dimensions')
        end = index if i + 1 == count else cursor + word(descriptors, at + 300) - word(descriptors, at + 124)
        if not cursor <= end <= index:
            raise ValueError('Invalid animation spacing')
        block = seq[cursor:end]
        tables = len(new) * blends * 12
        output = bytearray(tables)
        for blend in range(blends):
            for bone, source in enumerate(mapping):
                if source is None:
                    continue
                table = (blend * len(new) + bone) * 12
                original = (blend * len(old) + source) * 12
                for axis in range(6):
                    a, b = old[source], new[bone]
                    values = decode_channel(block, original, axis, frames)
                    scale = struct.unpack('<f', struct.pack('<f', 1 / (256 if axis < 3 else 10240)))[0]
                    converted = [round((a['pose'][axis] + v * scale - b['pose'][axis]) / scale) for v in values]
                    if any(v < -32768 or v > 32767 for v in converted):
                        raise ValueError('Destination scale cannot represent animation')
                    error = max(abs(a['pose'][axis] + v * scale - b['pose'][axis] - w * scale)
                                for v, w in zip(values, converted))
                    if error > scale * .5001 + 1e-7:
                        raise ValueError('Quantization error exceeds half a channel unit')
                    max_error = max(max_error, error)
                    comparisons += frames
                    if not any(converted):
                        continue
                    offset = len(output) - table
                    if not 0 < offset <= 65535:
                        raise ValueError('Rebound animation exceeds 16-bit channel offset')
                    struct.pack_into('<H', output, table + axis * 2, offset)
                    output += encode_channel(converted)
                    if decode_channel(output, table, axis, frames) != converted:
                        raise ValueError('Rebound RLE verification failed')
        while len(output) % 4:
            output.append(0)
        struct.pack_into('<i', descriptors, at + 124, len(result))
        result += output
        cursor = end
    struct.pack_into('<i', result, 4, len(result))
    result += descriptors
    return bytes(result), dict(sequences=count, channelFramesCompared=comparisons, maxQuantizationError=max_error,
                              added=[b['name'] for b, source in zip(new, mapping) if source is None],
                              removed=[b['name'] for b in old if b['name'] not in {n['name'] for n in new}])
