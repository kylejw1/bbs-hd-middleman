#!/usr/bin/env python3
"""Embed web/index.html into include/WebContent.h.

`web/index.html` is the single source of truth for the dashboard. The same file
is served by the ESP32 (wrapped in a PROGMEM raw string literal) and hosted on
GitHub Pages for the Bluetooth transport, so it must never be forked.

This runs as a PlatformIO pre-script (see `extra_scripts` in platformio.ini) and
can also be invoked directly:

    python3 tools/embed_web.py

The generated header is committed so that a checkout always compiles, even if
the build system never runs this script. Any manual edit to include/WebContent.h
is overwritten on the next build -- edit web/index.html instead.
"""

import hashlib
import pathlib
import sys

# PlatformIO pre-scripts are exec()'d by SCons, which injects `Import` but not
# `__file__`/`__name__`. Running the file directly with python3 must also work.
try:
    Import("env")  # type: ignore[name-defined]  # noqa: F821

    _UNDER_SCONS = True
except NameError:
    _UNDER_SCONS = False


def project_root() -> pathlib.Path:
    """Locate the project directory under either execution context."""
    try:
        return pathlib.Path(__file__).resolve().parent.parent
    except NameError:
        pass

    if _UNDER_SCONS:
        return pathlib.Path(globals()["env"]["PROJECT_DIR"]).resolve()

    return pathlib.Path.cwd().resolve()


ROOT = project_root()
SOURCE = ROOT / "web" / "index.html"
TARGET = ROOT / "include" / "WebContent.h"

# Raw string delimiters are tried in order. The delimiter only has to be absent
# from the payload; the first candidate will essentially always work because the
# sequence `)INDEXHTML"` cannot occur in valid HTML/JS.
DELIMITERS = ("INDEXHTML", "BBSHDWEB", "EMBEDDEDPAGE")


class EmbedError(RuntimeError):
    """Raised for unusable web sources; surfaces as a build failure."""


def pick_delimiter(html: str) -> str:
    for candidate in DELIMITERS:
        if f'){candidate}"' not in html:
            return candidate
    raise EmbedError(
        "web/index.html contains every candidate raw-string delimiter; "
        "add a longer one to DELIMITERS."
    )


def render(html: str) -> str:
    delimiter = pick_delimiter(html)
    digest = hashlib.sha256(html.encode("utf-8")).hexdigest()

    header = f"""// AUTO-GENERATED FILE -- DO NOT EDIT.
//
// Produced by tools/embed_web.py from web/index.html.
// The dashboard is served by both the ESP32 (this header) and GitHub Pages
// (web/index.html directly), so its single source of truth is web/index.html.
// Edits made here are silently reverted on the next build.
//
// source    : web/index.html
// sha256    : {digest}
// bytes     : {len(html.encode("utf-8"))}

#ifndef WEB_CONTENT_H
#define WEB_CONTENT_H

#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"{delimiter}(
"""
    footer = f'){delimiter}";\n\n#endif // WEB_CONTENT_H\n'
    return header + html + footer


def main() -> int:
    try:
        html = SOURCE.read_text(encoding="utf-8")
    except FileNotFoundError:
        raise EmbedError(f"missing source file {SOURCE}")

    if not html.strip():
        raise EmbedError(f"{SOURCE} is empty; refusing to embed.")

    rendered = render(html)
    previous = TARGET.read_text(encoding="utf-8") if TARGET.exists() else None

    if previous != rendered:
        TARGET.write_text(rendered, encoding="utf-8")
        action = "wrote" if previous is None else "regenerated"
        print(f"embed_web.py: {action} {TARGET.relative_to(ROOT)} "
              f"from web/index.html ({len(html.encode('utf-8'))} bytes)")

    return 0


# NOTE: never call sys.exit() from a PlatformIO pre-script. SystemExit
# propagates out of the SConscript and terminates SCons with a clean status, so
# `pio run` reports SUCCESS without compiling anything at all.
if _UNDER_SCONS:
    main()
elif globals().get("__name__") == "__main__":
    try:
        sys.exit(main())
    except EmbedError as exc:
        sys.exit(f"embed_web.py: {exc}")
