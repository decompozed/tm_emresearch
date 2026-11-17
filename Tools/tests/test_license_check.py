#!/usr/bin/env python3
"""Tests of Tools/sbom/check_licenses.py: the expression rules and the way missing licenses are resolved."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'sbom'))
import check_licenses as cl  # noqa: E402

ALLOWED = {'MIT', 'BSD-3-Clause', 'Apache-2.0', 'LLVM-exception'}


def blockers(expression):
    return cl.not_allowed(cl.parse(expression), ALLOWED)


def document(*packages):
    entries = [{'name': 'project', 'SPDXID': 'SPDXRef-project', 'licenseConcluded': 'NOASSERTION', 'licenseDeclared': 'NOASSERTION'}]
    for index, (name, concluded, declared) in enumerate(packages):
        entries.append({'name': name, 'SPDXID': f'SPDXRef-{index}', 'versionInfo': '1', 'licenseConcluded': concluded, 'licenseDeclared': declared})
    return {'packages': entries, 'relationships': [{'spdxElementId': 'SPDXRef-DOCUMENT', 'relationshipType': 'DESCRIBES', 'relatedSpdxElement': 'SPDXRef-project'}]}


class Expressions(unittest.TestCase):
    def test_single(self):
        self.assertEqual(blockers('MIT'), [])
        self.assertEqual(blockers('GPL-3.0-only'), ['GPL-3.0-only'])

    def test_and_needs_both(self):
        self.assertEqual(blockers('MIT AND BSD-3-Clause'), [])
        self.assertEqual(blockers('MIT AND GPL-2.0-only'), ['GPL-2.0-only'])

    def test_or_needs_one(self):
        self.assertEqual(blockers('GPL-2.0-only OR MIT'), [])
        self.assertEqual(blockers('GPL-2.0-only OR LGPL-2.1-only'), ['GPL-2.0-only', 'LGPL-2.1-only'])

    def test_precedence_and_parentheses(self):
        self.assertEqual(blockers('MIT OR GPL-2.0-only AND GPL-3.0-only'), [])
        self.assertEqual(blockers('(MIT OR GPL-2.0-only) AND GPL-3.0-only'), ['GPL-3.0-only'])

    def test_with_and_plus(self):
        self.assertEqual(blockers('Apache-2.0 WITH LLVM-exception'), [])
        self.assertEqual(blockers('Apache-2.0 WITH Classpath-exception-2.0'), ['Classpath-exception-2.0'])
        self.assertEqual(blockers('Apache-2.0+'), [])

    def test_broken(self):
        for text in ('', 'MIT AND', '(MIT', 'MIT OR OR BSD-3-Clause', 'MIT; rm'):
            with self.assertRaises(cl.ExpressionError, msg=text):
                cl.parse(text)


class Documents(unittest.TestCase):
    def test_concluded_then_declared(self):
        rows, problems = cl.check(document(('a', 'MIT', 'GPL-2.0-only'), ('b', 'NOASSERTION', 'BSD-3-Clause')), ALLOWED, {}, {})
        self.assertEqual(problems, [])
        self.assertEqual([r['license'] for r in rows], ['MIT', 'BSD-3-Clause'])

    def test_project_is_skipped(self):
        rows, _ = cl.check(document(), ALLOWED, {}, {})
        self.assertEqual(rows, [])

    def test_missing_needs_override(self):
        _, problems = cl.check(document(('a', 'NOASSERTION', 'NOASSERTION')), ALLOWED, {}, {})
        self.assertEqual(len(problems), 1)
        override = {'a': {'name': 'a', 'license': 'MIT', 'source': 'header'}}
        rows, problems = cl.check(document(('a', 'NOASSERTION', 'NONE')), ALLOWED, override, {})
        self.assertEqual(problems, [])
        self.assertIn('header', rows[0]['source'])

    def test_override_must_be_allowed_too(self):
        override = {'a': {'name': 'a', 'license': 'GPL-3.0-only', 'source': 'header'}}
        _, problems = cl.check(document(('a', 'NOASSERTION', 'NOASSERTION')), ALLOWED, override, {})
        self.assertEqual(len(problems), 1)

    def test_not_allowed_needs_accepted_entry_for_that_license(self):
        accepted = {'a': {'name': 'a', 'license': 'GPL-2.0-only', 'reason': 'tool only'}}
        _, problems = cl.check(document(('a', 'GPL-2.0-only', 'NOASSERTION')), ALLOWED, {}, accepted)
        self.assertEqual(problems, [])
        _, problems = cl.check(document(('a', 'GPL-3.0-only', 'NOASSERTION')), ALLOWED, {}, accepted)
        self.assertEqual(len(problems), 2)   # refused, and the accepted entry matches nothing

    def test_stale_entries_fail(self):
        override = {'gone': {'name': 'gone', 'license': 'MIT', 'source': 'x'}}
        _, problems = cl.check(document(('a', 'MIT', 'MIT')), ALLOWED, override, {})
        self.assertEqual(len(problems), 1)
        override = {'a': {'name': 'a', 'license': 'MIT', 'source': 'x'}}
        _, problems = cl.check(document(('a', 'MIT', 'MIT')), ALLOWED, override, {})
        self.assertEqual(len(problems), 1)


if __name__ == '__main__':
    unittest.main()
