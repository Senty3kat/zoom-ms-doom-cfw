"""Minimal YAML shim for zoomms. Handles maps, lists of maps, scalars."""
from __future__ import annotations


def _scalar(s: str):
    s = s.strip()
    if len(s) >= 2 and s[0] == '"' and s[-1] == '"':
        return bytes(s[1:-1], "utf-8").decode("unicode_escape")
    if len(s) >= 2 and s[0] == "'" and s[-1] == "'":
        return s[1:-1]
    if s in ("true", "True"):
        return True
    if s in ("false", "False"):
        return False
    if s in ("null", "~"):
        return None
    if s == "":
        return ""
    try:
        return int(s, 0)
    except ValueError:
        pass
    try:
        return float(s)
    except ValueError:
        pass
    return s


def safe_load(text: str):
    rows = []
    for raw in text.splitlines():
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        indent = len(raw) - len(raw.lstrip(" "))
        rows.append((indent, raw.strip()))
    pos = 0

    def parse_value(indent, cur):
        nonlocal pos
        """Parse a block that is a list or a map at the given indent."""
        if pos >= len(rows):
            return cur
        if rows[pos][1].startswith("- "):
            out = []
            while pos < len(rows) and rows[pos][0] == indent and rows[pos][1].startswith("- "):
                item_line = rows[pos][1][2:].strip()
                pos += 1
                d = {}
                # first key on the dash line
                if ":" in item_line:
                    k, _, v = item_line.partition(":")
                    v = v.strip()
                    if v:
                        d[k.strip()] = _scalar(v)
                    else:
                        d[k.strip()] = parse_value(indent + 2, {})
                # continuation keys at indent+2
                while pos < len(rows) and rows[pos][0] >= indent + 2 and not rows[pos][1].startswith("- "):
                    k2, _, v2 = rows[pos][1].partition(":")
                    v2 = v2.strip()
                    key_indent = rows[pos][0]
                    pos += 1
                    if v2:
                        d[k2.strip()] = _scalar(v2)
                    else:
                        d[k2.strip()] = parse_value(key_indent + 2, {})
                out.append(d)
            return out
        out = {}
        while pos < len(rows) and rows[pos][0] == indent and not rows[pos][1].startswith("- "):
            line = rows[pos][1]
            pos += 1
            k, _, v = line.partition(":")
            key = k.strip()
            v = v.strip()
            if v == "":
                if pos < len(rows) and rows[pos][0] > indent:
                    out[key] = parse_value(rows[pos][0], {})
                else:
                    out[key] = {}
            elif v.startswith("[") and v.endswith("]"):
                inner = v[1:-1].strip()
                out[key] = [_scalar(x) for x in inner.split(",")] if inner else []
            else:
                out[key] = _scalar(v)
        return out

    if not rows:
        return {}
    return parse_value(rows[0][0], {})
