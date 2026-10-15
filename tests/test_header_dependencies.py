"""Verify header-only edits invalidate every Makefile executable target."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

class HeaderDependencies(unittest.TestCase):
    def test_header_edits_rebuild_all_executables(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            shutil.copyfile(root / 'Makefile', work / 'Makefile')
            for name in ['include/aegisdb/engine.hpp', 'tools/aegisql.cpp',
                         'tests/test_aegis.cpp', 'bench/bench_tpch.cpp',
                         'aegisql', 'aegis_test', 'bench_tpch']:
                path = work / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            for target in ['aegisql', 'aegis_test', 'bench_tpch']:
                with self.subTest(target=target):
                    result = subprocess.run(['make', '-n', '-W', 'include/aegisdb/engine.hpp', target],
                                            cwd=work, text=True, capture_output=True, check=True)
                    self.assertIn('-o ' + target + ' ', result.stdout)
