"""Offline checks for this experimental pack, not agent or hardware tests.

Run at repository root: python -m unittest discover -s tests -v
Only this pack's documented YAML subset is checked, not the complete spec.
The inherited .agents/skills distribution is outside this suite's scope.
"""
from pathlib import Path
import json
import re
import unittest
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
NAMES = {
    'prototyping-mosaico-experiences',
    'iterating-mosaico-ui',
    'modeling-mosaico-multi-device-interactions',
}
EVALS = ROOT / 'evals' / 'mosaico-experience'


def skills():
    return [ROOT / 'skills' / name / 'SKILL.md' for name in sorted(NAMES)]


def docs():
    return list((ROOT / 'skills').rglob('*.md')) + [
        ROOT / 'docs/mosaico-skills-verification.md', EVALS / 'README.md']


def parts(path):
    text = path.read_text(encoding='utf-8')
    match = re.fullmatch(r'---\n(.*?)\n---\n(.+)', text, re.S)
    if not match:
        raise ValueError(f'{path}: expected frontmatter and body')
    return match.groups()


def scalar(header, key):
    matches = re.findall(rf'^{re.escape(key)}: (.+)$', header, re.M)
    if len(matches) != 1:
        raise ValueError(f'expected one scalar {key!r}')
    value = matches[0]
    return json.loads(value) if value.startswith('"') else value


class PackageChecks(unittest.TestCase):
    def test_01_expected_skills_exist(self):
        actual = {p.parent.name for p in (ROOT / 'skills').glob('*/SKILL.md')}
        self.assertEqual(actual, NAMES)

    def test_02_frontmatter(self):
        for path in skills():
            with self.subTest(skill=path.parent.name):
                header, _ = parts(path)
                name, desc = scalar(header, 'name'), scalar(header, 'description')
                self.assertEqual(name, path.parent.name)
                self.assertRegex(name, r'^[a-z0-9]+(?:-[a-z0-9]+)*$')
                self.assertLessEqual(len(name), 64)
                self.assertTrue(desc.startswith('Use when'))
                self.assertTrue(30 < len(desc) <= 500)
                self.assertEqual(set(re.findall(r'^(\S+):', header, re.M)),
                                 {'name', 'description', 'metadata'})
                for key, value in [('version', '0.1.0'), ('status', 'experimental'),
                                   ('behavioral-validation', 'not-run')]:
                    self.assertIn(f'  {key}: "{value}"', header)

    def test_03_sections_and_size(self):
        for path in skills():
            _, body = parts(path)
            for heading in ('## 适用范围', '## 工作方式', '## 交付与验收', '## 参考'):
                self.assertIn(heading, body)
            self.assertTrue(500 < len(body) < 4000)
            self.assertLess(len(path.read_text(encoding='utf-8').splitlines()), 100)

    def test_04_relative_links(self):
        self.assertGreater(len(docs()), 6)
        for path in docs():
            text = re.sub(r'```.*?```', '', path.read_text(encoding='utf-8'), flags=re.S)
            for dest in re.findall(r'\]\(([^\s)]+)\)', text):
                url = urlsplit(dest)
                if url.scheme or url.netloc or not url.path:
                    continue
                target = (path.parent / unquote(url.path)).resolve()
                with self.subTest(file=path.relative_to(ROOT), link=dest):
                    self.assertTrue(target.is_relative_to(ROOT))
                    self.assertTrue(target.is_file(), f'missing: {target}')

    def test_05_portable_skills(self):
        for path in skills():
            _, body = parts(path)
            links = re.findall(r'\]\(([^\s)]+)\)', body)
            self.assertTrue(links)
            for link in links:
                self.assertTrue(link.startswith('references/'))
                self.assertTrue((path.parent / link).is_file())
            self.assertNotIn('REQUIRED SUB-SKILL', body)
            self.assertNotIn('superpowers:', body)

    def test_06_no_chat_links_or_fixed_platform_values(self):
        for path in docs():
            text = path.read_text(encoding='utf-8')
            for forbidden in ('sandbox:/', '/mnt/data/', '\ue200filecite', '\ue200cite'):
                self.assertNotIn(forbidden, text)
        for path in skills():
            _, body = parts(path)
            for fixed in ('84px', '480×480', 'ESP-IDF 6.1', '450ms', '1.05s'):
                self.assertNotIn(fixed, body)

    def test_07_usage_and_boundaries(self):
        text = (ROOT / 'skills/README.md').read_text(encoding='utf-8')
        for required in ('试用版', '未执行', '.agents/skills/', 'python -m unittest',
                         '不会自动', '不修改'):
            self.assertIn(required, text)

    def test_08_eval_cases(self):
        data = json.loads((EVALS / 'cases.json').read_text(encoding='utf-8'))
        cases = data['cases']
        self.assertGreaterEqual(len(cases), 8)
        self.assertEqual(len({c['id'] for c in cases}), len(cases))
        for case in cases:
            self.assertIn(case['skill'], NAMES)
            self.assertIsInstance(case['should_trigger'], bool)
            self.assertGreater(len(case['prompt']), 30)
            self.assertGreaterEqual(len(case['acceptance']), 2)
            self.assertTrue(case['failure_signals'])
        for name in NAMES:
            self.assertTrue(any(c['skill'] == name and c['should_trigger'] for c in cases))
            self.assertTrue(any(c['skill'] == name and not c['should_trigger'] for c in cases))

    def test_09_evals_not_falsely_marked_as_run(self):
        data = json.loads((EVALS / 'cases.json').read_text(encoding='utf-8'))
        self.assertEqual(data['execution_status'], 'not-run')
        self.assertEqual(data['runs'], [])
        text = (EVALS / 'README.md').read_text(encoding='utf-8')
        for required in ('无 skill', '有 skill', '未执行', '独立', '原始输出', '评分'):
            self.assertIn(required, text)

    def test_10_no_implicit_install_or_product_dependencies(self):
        for name in NAMES:
            self.assertFalse((ROOT / '.agents/skills' / name).exists())
        for path in ('.codex', '.github/workflows', 'package.json',
                     'requirements.txt', '.mcp.json', 'hooks'):
            self.assertFalse((ROOT / path).exists(), path)


if __name__ == '__main__':
    unittest.main()
