"""Read indirect map precaches from their production consumers, never a second asset list."""
import hashlib
import re

from csol_material_stage import resource_name


def precache_resources(source, classes):
    """Return direct resources and input identities for the selected BSP classes.

    This deliberately supports the reviewed C++ expressions used by these
    consumers. A new expression must be reviewed rather than silently omitted.
    Model companions still go through the normal production model collector.
    """
    classes = set(classes)
    resources, inputs, texts = set(), {}, {}

    def read(name):
        if name not in texts:
            path = source / name
            data = path.read_bytes()
            texts[name] = data.decode('utf-8-sig')
            inputs[str(path)] = dict(path=str(path), bytes=len(data),
                                     sha256=hashlib.sha256(data).hexdigest())
        return texts[name]

    def consume(name, method, headers):
        text = re.sub(r'//[^\n]*|/\*.*?\*/', '', read(name), flags=re.S)
        matches = list(re.finditer(r'\bvoid\s+' + re.escape(method) + r'\s*\(\s*\)\s*\{', text))
        if len(matches) != 1:
            raise ValueError('Missing or ambiguous map precache consumer: ' + method)
        start, depth, quoted, escaped = matches[0].end(), 1, False, False
        end = start
        while depth and end < len(text):
            c = text[end]
            if escaped:
                escaped = False
            elif quoted and c == '\\':
                escaped = True
            elif c == '"':
                quoted = not quoted
            elif not quoted:
                depth += (c == '{') - (c == '}')
            end += 1
        if depth:
            raise ValueError('Unclosed map precache consumer: ' + method)
        body = text[start:end - 1]
        constants = {}
        for prefix, header in headers:
            pairs = re.findall(r'inline constexpr\s+(?:const char\*\s*|char\s+)(\w+)(?:\[\])?\s*=\s*"([^"\r\n]+)"\s*;', read(header))
            for key, value in pairs:
                symbol = prefix + key
                if symbol in constants:
                    raise ValueError('Duplicate map resource constant: ' + symbol)
                constants[symbol] = value
        calls = list(re.finditer(r'\b(PRECACHE_\w+)\s*\(', body))
        if not calls:
            raise ValueError('Empty map precache consumer: ' + method)
        for call in calls:
            kind = call[1]
            if kind not in ('PRECACHE_MODEL', 'PRECACHE_SOUND', 'PRECACHE_EVENT', 'PRECACHE_GENERIC'):
                raise ValueError('Unreviewed map precache call: ' + kind)
            i, balance, quote = call.end(), 1, False
            while balance and i < len(body):
                c = body[i]
                if c == '"':
                    quote = not quote
                elif not quote:
                    balance += (c == '(') - (c == ')')
                i += 1
            if balance:
                raise ValueError('Unclosed map precache call: ' + method)
            expression = body[call.end():i - 1].strip()
            if kind == 'PRECACHE_EVENT':
                event = re.fullmatch(r'1\s*,\s*(.+)', expression, re.S)
                if not event:
                    raise ValueError('Unreviewed map event arguments: ' + expression)
                expression = event[1]
            cast = re.fullmatch(r'const_cast<char\*>\(([^()]+)\)', expression)
            if cast:
                expression = cast[1].strip()
            value = re.fullmatch(r'"([^"\\\r\n]+)"', expression)
            value = value[1] if value else constants.get(expression)
            if value is None:
                raise ValueError('Unresolved map precache resource: ' + expression)
            resources.add(resource_name(('sound/' if kind == 'PRECACHE_SOUND' else '') + value))

    if classes & {'mount_weapon', 'weapon_mountgun'}:
        consume('native/csol/server/weapons/wpn_mountgun.cpp', 'CMountgun::Precache',
                [('mg::', 'native/csol/weapons/mountgun.hpp')])
    if 'ze_counter' in classes:
        consume('native/csol/server/scenario/core/scenario_season2_entities.cpp', 'CCSOLZeCounter::Spawn', [
            ('csol::scenario::ze_counter::', 'native/csol/scenario/core/ze_counter.hpp'),
            ('csol::scenario_hud::', 'native/csol/scenario/core/scenario_round_hud.hpp')])
    return resources, list(inputs.values())
