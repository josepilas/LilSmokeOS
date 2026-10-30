"""Generate a deterministic diff that reuses identical perspective endpoints."""
import argparse
import difflib
from pathlib import Path
import re


def transform(original):
    starts = list(re.finditer(r"static void opt_scan_\w+\(", original))
    assert len(starts) == 12
    text = original
    for index in range(len(starts) - 1, -1, -1):
        start = starts[index].start()
        end = starts[index + 1].start() if index + 1 < len(starts) else original.index("/* --- parameterized triangle walker")
        body = original[start:end]
        loop = "    while (n > 0) {\n        const int cnt = (n > OPT_STEP) ? OPT_STEP : n;\n"
        at = body.index(loop)
        init_start = at + len(loop)
        init_end = body.index("        float q[", init_start)
        init = body[init_start:init_end]
        assert init.count("const float w0 = 1.0f / p[3];") == 1
        pairs = re.findall(r"opt_span_div\((\w+) - (\w+), cnt\)", body)
        variables = re.findall(r"        int (\w+) =", init)
        assert {old for new, old in pairs} == set(variables)
        assert len(pairs) == len(variables)
        tail = "        n -= cnt;\n"
        assert body.count(tail) == 1
        reset = "".join(f"        {old} = {new};\n" for new, old in pairs)
        body = body[:at] + "    if (n <= 0) return;\n" + "".join(
            line[4:] if line.strip() else line for line in init.splitlines(True)
        ) + loop + body[init_end:]
        body = body.replace(tail, reset + tail, 1)
        text = text[:start] + body + text[end:]
    marker = "/* --- opaque textured (tex_rgb): v3 path, unchanged --- */"
    text = text.replace(marker, "/* A block endpoint is the next block's exact start. Retain the converted\n"
        " * values instead of repeating perspective division and conversion. */\n" + marker, 1)
    return text


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True)
    original = parser.parse_args().reference.read_text()
    modified = transform(original)
    print("".join(difflib.unified_diff(original.splitlines(True), modified.splitlines(True),
        "a/src/pc/gfx/gfx_soft.c", "b/src/pc/gfx/gfx_soft.c")), end="")
