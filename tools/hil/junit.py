"""JUnit XML emission for HIL test runs.

Schema follows the common Jenkins/Bamboo flavor — most CI systems accept it.
"""
from __future__ import annotations

import html
import re
import time
from dataclasses import dataclass, field
from pathlib import Path

# Characters illegal in XML 1.0 even when escaped (XML spec §2.2). Failure
# messages can carry raw IS-Viewer bytes, so strip these before emission.
_ILLEGAL_XML_CHARS = re.compile(r"[\x00-\x08\x0B\x0C\x0E-\x1F]")


@dataclass
class JUnitTestCase:
    name: str
    duration_s: float
    passes: list[str] = field(default_factory=list)
    failures: list[str] = field(default_factory=list)
    skipped: bool = False
    skip_reason: str = ""

    @property
    def passed(self) -> bool:
        return not self.failures and not self.skipped


@dataclass
class JUnitSuite:
    name: str
    cases: list[JUnitTestCase] = field(default_factory=list)
    started_at: float = field(default_factory=time.time)

    @property
    def total_duration_s(self) -> float:
        return sum(c.duration_s for c in self.cases)

    @property
    def n_failures(self) -> int:
        return sum(1 for c in self.cases if c.failures)

    @property
    def n_skipped(self) -> int:
        return sum(1 for c in self.cases if c.skipped)


def render(suite: JUnitSuite) -> str:
    def _xml_escape(s: str) -> str:
        return html.escape(_ILLEGAL_XML_CHARS.sub("", s), quote=True)

    out = []
    out.append('<?xml version="1.0" encoding="UTF-8"?>')
    out.append(
        f'<testsuite name="{_xml_escape(suite.name)}" '
        f'tests="{len(suite.cases)}" '
        f'failures="{suite.n_failures}" '
        f'skipped="{suite.n_skipped}" '
        f'time="{suite.total_duration_s:.3f}">'
    )
    for c in suite.cases:
        out.append(
            f'  <testcase name="{_xml_escape(c.name)}" '
            f'time="{c.duration_s:.3f}">'
        )
        if c.skipped:
            out.append(f'    <skipped message="{_xml_escape(c.skip_reason)}"/>')
        for f in c.failures:
            out.append(
                f'    <failure message="{_xml_escape(f)}">{_xml_escape(f)}</failure>'
            )
        out.append("  </testcase>")
    out.append("</testsuite>")
    return "\n".join(out)


def write(suite: JUnitSuite, path: Path) -> None:
    path.write_text(render(suite), encoding="utf-8")
