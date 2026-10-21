"""Portable, offline structure checks for all nine distributed skills.

This is not a YAML certification, client discovery test or behavioral evaluation.
"""
from pathlib import Path
import re
import unittest
from urllib.parse import unquote, urlsplit
from test_mosaico_skills import NAMES, parts, scalar

ROOT = Path(__file__).resolve().parents[1]
ENGINEERING = {
    'mosaico-gsp-parity', 'mosaico-runtime-performance',
    'mosaico-peer-cooperation', 'mosaico-audio-validation',
    'mosaico-release-evidence', 'mosaico-realtime-input',
}


def catalog():
    return [ROOT / folder / name / 'SKILL.md'
            for folder, names in [('.agents/skills', ENGINEERING), ('skills', NAMES)]
            for name in sorted(names)]


def local_links(path):
    text = re.sub(r'```.*?```', '', path.read_text(encoding='utf-8'), flags=re.S)
    for dest in re.findall(r'\]\(([^\s)]+)\)', text):
        url = urlsplit(dest)
        if not url.scheme and not url.netloc and url.path:
            yield (path.parent / unquote(url.path)).resolve()


class CatalogChecks(unittest.TestCase):
    def test_all_skills_present_once(self):
        actual = list((ROOT / '.agents/skills').glob('*/SKILL.md'))
        actual += list((ROOT / 'skills').glob('*/SKILL.md'))
        self.assertEqual(set(actual), set(catalog()))
        names = [p.parent.name for p in actual]
        self.assertEqual(len(names), len(set(names)))

    def test_engineering_headers(self):
        for name in sorted(ENGINEERING):
            with self.subTest(skill=name):
                header, body = parts(ROOT / '.agents/skills' / name / 'SKILL.md')
                self.assertEqual(scalar(header, 'name'), name)
                self.assertRegex(name, r'^[a-z0-9]+(?:-[a-z0-9]+)*$')
                desc = scalar(header, 'description')
                self.assertTrue(desc.startswith('Use when'))
                self.assertTrue(30 < len(desc) <= 500)
                self.assertTrue(body.strip())

    def test_root_catalog_links_every_skill(self):
        self.assertTrue(set(catalog()).issubset(set(local_links(ROOT / 'README.md'))))

    def test_all_markdown_references_and_fences(self):
        paths = [ROOT / 'README.md']
        for folder in ['.agents/skills', 'skills', 'docs', 'evals']:
            paths.extend((ROOT / folder).rglob('*.md'))
        for path in paths:
            with self.subTest(file=str(path.relative_to(ROOT))):
                text = path.read_text(encoding='utf-8')
                self.assertEqual(len(re.findall(r'^```', text, re.M)) % 2, 0)
                for target in local_links(path):
                    self.assertTrue(target.is_relative_to(ROOT), str(target))
                    self.assertTrue(target.is_file(), str(target))

    def test_skill_references_stay_in_copied_folder(self):
        for path in catalog():
            # References must remain usable when only this skill is copied.
            for doc in path.parent.rglob('*.md'):
                for target in local_links(doc):
                    with self.subTest(file=str(doc.relative_to(ROOT))):
                        self.assertTrue(target.is_relative_to(path.parent), str(target))
                        self.assertTrue(target.is_file(), str(target))

    def test_no_local_session_paths_in_distributed_docs(self):
        for folder in ['.agents/skills', 'skills', 'docs', 'evals']:
            for path in (ROOT / folder).rglob('*.md'):
                with self.subTest(file=str(path.relative_to(ROOT))):
                    text = path.read_text(encoding='utf-8')
                    for forbidden in ['sandbox:/', '/mnt/data/', '/home/', '\ue200filecite']:
                        self.assertNotIn(forbidden, text)


if __name__ == '__main__':
    unittest.main()
