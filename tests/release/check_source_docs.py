#!/usr/bin/env python3
"""Check declaration-adjacent documentation for handwritten ImageKit source.

The scan is intentionally conservative: it recognizes common Vala declarations,
checked native C/C++ functions and C headers, and Python AST docstrings.
It excludes generated/third-party code and never rewrites a source file.
"""

import argparse
import ast
import re
from pathlib import Path

from native_docs import function_definitions


TYPE_RE = re.compile(
    r"^\s*(?:(?:\[[^]]*\]\s*)|(?:(?:public|private|protected|internal|"
    r"abstract|sealed|compact|static)\s+))*(class|interface|enum|struct|"
    r"errordomain|delegate)\s+([A-Za-z_]\w*)\b"
)
BODY_RE = re.compile(
    r"^\s*(?:\[[^]]*\]\s*)?(?:(?:public|internal|private|protected|"
    r"abstract|virtual|override|static|async|extern|owned|unowned|weak|new)\s+)+"
    r"(?P<rest>.+)$"
)
PROP_RE = re.compile(
    r"^(?:(?:static|async|override|virtual|abstract|owned|unowned|weak|new)\s+)*"
    r"[\w.?\[\]<> ,*]+\s+([A-Za-z_]\w*)\s*\{"
)
METHOD_RE = re.compile(
    r"^(?:(?:static|async|override|virtual|abstract|owned|unowned|weak|new)\s+)*"
    r"(?:[\w.?\[\]<> ,*]+\s+)?([A-Za-z_]\w*)\s*\("
)
SIGNAL_RE = re.compile(
    r"^(?:(?:static|async|override|virtual|abstract|owned|unowned|weak|new)\s+)*"
    r"signal\s+[\w.?\[\]<> ,*]+\s+([A-Za-z_]\w*)\s*\("
)
IGNORED = {"if", "for", "foreach", "while", "switch", "catch", "return", "new", "get", "set"}


def adjacent_comment(lines: list[str], index: int) -> bool:
    """Find a Valadoc/Doxygen comment next to the declaration or its attributes."""
    index -= 1
    while index >= 0:
        candidate = lines[index].strip()
        if not candidate.endswith("]"):
            break
        opening = index
        while opening >= 0 and not lines[opening].lstrip().startswith("["):
            opening -= 1
        if opening < 0 or index - opening > 6:
            break
        index = opening - 1
    if index < 0:
        return False
    current = lines[index].strip()
    if current.startswith("///"):
        return True
    if not current.endswith("*/"):
        return False
    while index >= 0:
        line = lines[index]
        if "/**" in line:
            return True
        if "/*" in line:
            return False
        index -= 1
    return False


def vala_coverage(root: Path) -> tuple[int, list[str]]:
    """Inspect production Vala types, methods, properties, and signals."""
    total = 0
    missing = []
    for path in sorted((root / "src").rglob("*.vala")):
        lines = path.read_text(encoding="utf-8").splitlines()
        for index, line in enumerate(lines):
            name = None
            type_match = TYPE_RE.match(line)
            if type_match:
                name = type_match.group(2)
            else:
                member = BODY_RE.match(line)
                if member:
                    declaration = member.group("rest")
                    match = (
                        SIGNAL_RE.match(declaration)
                        or PROP_RE.match(declaration)
                        or METHOD_RE.match(declaration)
                    )
                    if match and match.group(1) not in IGNORED:
                        name = match.group(1)
            if name is not None:
                total += 1
                if not adjacent_comment(lines, index):
                    missing.append(f"{path.relative_to(root)}:{index + 1}: {name}")
    return total, missing


def python_coverage(root: Path) -> tuple[int, list[str]]:
    """Use Python's syntax tree to identify missing module and callable docstrings."""
    total = 0
    missing = []
    for path in sorted(root.rglob("*.py")):
        if "__pycache__" in path.parts:
            continue
        tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
        nodes = [tree, *(
            node for node in ast.walk(tree)
            if isinstance(node, (ast.ClassDef, ast.FunctionDef, ast.AsyncFunctionDef))
        )]
        for node in nodes:
            total += 1
            if ast.get_docstring(node) is None:
                name = getattr(node, "name", "<module>")
                missing.append(f"{path.relative_to(root)}:{getattr(node, 'lineno', 1)}: {name}")
    return total, missing


def native_coverage(root: Path) -> tuple[int, list[str]]:
    """Verify structured comments on native headers and recognized function definitions."""
    total = 0
    missing = []
    # A source-level scan rather than a C grammar parser: never treat it as a
    # replacement for compiler or GI/Valadoc generation checks.
    for path in sorted((root / "native").glob("*.h")) + sorted((root / "src").rglob("*.h")):
        lines = path.read_text(encoding="utf-8").splitlines()
        for i, line in enumerate(lines):
            match = re.search(r"\b([a-zA-Z_]\w+)\s*\(", line)
            if not match or line.strip().startswith(("#", "//", "*", "/*")):
                continue
            if "G_BEGIN_DECLS" in line:
                continue
            name = match.group(1)
            index = i
            while index > 0 and lines[index - 1].strip() == "G_GNUC_INTERNAL":
                index -= 1
            total += 1
            if not adjacent_comment(lines, index):
                missing.append(f"{path.relative_to(root)}:{i + 1}: {name}")
    return total, missing



def native_implementation_coverage(root: Path) -> tuple[int, list[str]]:
    """Inspect C/C++ definitions so private safety-critical helpers are not missed."""
    total = 0
    missing = []
    paths = sorted((root / "native").glob("*.c"))
    paths += sorted((root / "native").glob("*.cpp"))
    paths += sorted((root / "src").rglob("*.c"))
    paths += sorted((root / "src").rglob("*.cpp"))
    for path in paths:
        lines = path.read_text(encoding="utf-8").splitlines()
        for name, first_line in function_definitions("\n".join(lines) + "\n", path.suffix):
            total += 1
            if not adjacent_comment(lines, first_line - 1):
                missing.append(f"{path.relative_to(root)}:{first_line}: {name}")
    return total, missing

def main() -> int:
    """Report coverage for the source tree and fail if any required summary is absent."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    root = args.source_root.resolve()
    failures = []
    for language, checker in (("Vala", vala_coverage), ("Python", python_coverage),
                              ("Native headers", native_coverage),
                              ("Native definitions", native_implementation_coverage)):
        count, missing = checker(root)
        print(f"{language}: {count - len(missing)}/{count} declarations documented")
        failures.extend(missing)
    if failures:
        print("Missing source documentation:\n" + "\n".join(failures))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
