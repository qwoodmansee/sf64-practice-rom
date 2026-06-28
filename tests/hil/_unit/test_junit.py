"""Unit tests for hil.junit XML emission."""
from __future__ import annotations

from tools.hil.junit import JUnitSuite, JUnitTestCase, render


def test_render_passing_suite():
    s = JUnitSuite(name="hil", cases=[
        JUnitTestCase(name="t1", duration_s=0.5, passes=["ok"]),
    ])
    xml = render(s)
    assert '<testsuite ' in xml
    assert 'tests="1"' in xml
    assert 'failures="0"' in xml


def test_failure_emits_failure_node():
    s = JUnitSuite(name="hil", cases=[
        JUnitTestCase(name="t1", duration_s=0.1, failures=["boom"]),
    ])
    xml = render(s)
    assert '<failure ' in xml
    assert "boom" in xml
    assert 'failures="1"' in xml


def test_xml_escapes_special_chars():
    s = JUnitSuite(name="hil", cases=[
        JUnitTestCase(name="t<1>", duration_s=0.1, failures=['x"y\'z<&>']),
    ])
    xml = render(s)
    assert "<1>" not in xml.split("<testcase")[1].split(">")[0]
    assert "&lt;" in xml or "&amp;" in xml


def test_strips_xml_illegal_control_chars():
    # Raw IS-Viewer bytes can carry control chars that are illegal in
    # XML 1.0 even when escaped — they must be stripped, and the result
    # must still parse.
    import xml.dom.minidom as minidom
    s = JUnitSuite(name="hil", cases=[
        JUnitTestCase(name="t1", duration_s=0.1,
                      failures=["bad\x00null\x07bell\x0bvt\x0cff\x1besc end"]),
    ])
    out = render(s)
    for ch in ("\x00", "\x07", "\x0b", "\x0c", "\x1b"):
        assert ch not in out
    assert "badnullbellvtffesc end" in out
    minidom.parseString(out)  # raises if the document is invalid


def test_keeps_legal_whitespace_chars():
    # Tab and newline are legal XML and must survive (escaped or raw).
    s = JUnitSuite(name="hil", cases=[
        JUnitTestCase(name="t1", duration_s=0.1, failures=["a\tb\nc"]),
    ])
    out = render(s)
    assert "a\tb\nc" in out
