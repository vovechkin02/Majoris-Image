"""Locate handwritten C/C++ definitions for source documentation validation."""

import bisect
import re


CONTROL_KEYWORDS = {"if", "for", "while", "switch", "catch", "return", "sizeof", "static_assert"}


def masked_source(text: str) -> str:
    """Mask comments and literals while retaining declaration offsets and line breaks."""
    output = list(text)
    index = 0
    state = "code"
    while index < len(text):
        char = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if state == "code":
            if char == "/" and following in ("/", "*"):
                output[index] = output[index + 1] = " "
                state = "line" if following == "/" else "block"
                index += 2
                continue
            if char in ("'", '"'):
                state = "string" if char == '"' else "character"
                output[index] = " "
        elif state == "line":
            if char == "\n":
                state = "code"
            else:
                output[index] = " "
        elif state == "block":
            if char == "*" and following == "/":
                output[index] = output[index + 1] = " "
                index += 2
                state = "code"
                continue
            if char != "\n":
                output[index] = " "
        else:
            if char == "\\" and following:
                output[index] = output[index + 1] = " "
                index += 2
                continue
            if char == ('"' if state == "string" else "'"):
                state = "code"
            if char != "\n":
                output[index] = " "
        index += 1
    return "".join(output)


def function_definitions(text: str, suffix: str) -> list[tuple[str, int]]:
    """Return likely file/namespace-scope function names and their declaration lines.

    Constructor initializer lists in OpenEXR are handled by class-level review;
    this scanner intentionally excludes them rather than inventing false methods.
    """
    code = masked_source(text)
    start_lines = [0, *(match.end() for match in re.finditer("\n", text))]
    cpp = suffix in (".cpp", ".hpp")
    nesting = 0
    results = []
    originals = text.splitlines()
    for index, char in enumerate(code):
        if char == "}":
            nesting = max(0, nesting - 1)
            continue
        if char != "{":
            continue
        if nesting == 0 or (cpp and nesting <= 2):
            last = index - 1
            while last >= 0 and code[last].isspace():
                last -= 1
            if last >= 0 and code[last] == ")":
                level = 1
                cursor = last - 1
                while cursor >= 0 and level:
                    if code[cursor] == ")":
                        level += 1
                    elif code[cursor] == "(":
                        level -= 1
                    cursor -= 1
                if level == 0:
                    prefix = code[max(0, cursor - 120):cursor + 1]
                    match = re.search(r"(?:\b|::)([A-Za-z_]\w*)\s*$", prefix)
                    if match and match.group(1) not in CONTROL_KEYWORDS:
                        name = match.group(1)
                        if name != "views":  # C++ member initializer, not a definition.
                            line = bisect.bisect_right(start_lines, cursor + 1)
                            first = line
                            while first > 1:
                                previous = originals[first - 2].strip()
                                if not previous or previous.endswith((";", "}", "{", "*/")):
                                    break
                                if previous.startswith(("#", "//", "*", "/*")):
                                    break
                                if re.search(r"\b(return|if|for|while|break)\b", previous):
                                    break
                                if previous.startswith(("static ", "extern ", "G_GNUC_", "inline ",
                                                        "const ", "unsigned ", "void", "bool", "int ",
                                                        "gboolean", "gchar", "Vips", "Gdk", "GBytes",
                                                        "char", "double", "float", "std::", "uint",
                                                        "gsize", "gint", "guint", "size_t", "GFile",
                                                        "GOutput", "GError", "auto ", "SequenceBox",
                                                        "struct")) or previous in ("static", "G_GNUC_INTERNAL") or previous.endswith(("*", "&", ",")):
                                    first -= 1
                                else:
                                    break
                            results.append((name, first))
        nesting += 1
    return results
