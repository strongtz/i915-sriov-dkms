import pathlib
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class PackageSourceTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.repo = pathlib.Path(self.directory.name) / "repo"
        self.repo.mkdir()
        for name in ("PKGBUILD", "i915-sriov-dkms.install"):
            shutil.copyfile(ROOT / name, self.repo / name)
        self.git("init", "-b", "main")
        self.git("config", "user.name", "Package test")
        self.git("config", "user.email", "test@example.com")
        self.git("add", "PKGBUILD", "i915-sriov-dkms.install")
        self.git("commit", "-m", "Initial package source")

    def git(self, *args, cwd=None):
        return subprocess.check_output(
            ["git", "-C", str(cwd or self.repo), *args],
            stderr=subprocess.PIPE,
            text=True,
        ).strip()

    def makepkg(self, cwd=None):
        cwd = cwd or self.repo
        result = subprocess.run(
            ["makepkg", "--nobuild", "--nodeps", "--nocheck"],
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(
            self.git("rev-parse", "HEAD", cwd=cwd / "src/i915-sriov-dkms"),
            self.git("rev-parse", "HEAD", cwd=cwd),
            result.stdout,
        )

    def add_commit(self):
        (self.repo / "marker").write_text("Selected source revision\n")
        self.git("add", "marker")
        self.git("commit", "-m", "Different source revision")

    def test_fresh_checkout(self):
        self.makepkg()

    def test_cached_source_after_branch_switch(self):
        self.makepkg()
        self.git("checkout", "-b", "other")
        self.add_commit()
        self.makepkg()

    def test_cached_source_after_detached_checkout(self):
        self.makepkg()
        self.git("checkout", "-b", "other")
        self.add_commit()
        self.git("checkout", "--detach")
        self.makepkg()

    def linked_worktree(self):
        self.git("checkout", "-b", "other")
        self.add_commit()
        self.git("checkout", "main")
        linked = pathlib.Path(self.directory.name) / "linked"
        self.git("worktree", "add", str(linked), "other")
        return linked

    def test_linked_worktree(self):
        self.makepkg(cwd=self.linked_worktree())

    def test_cached_linked_worktree_after_recipe_upgrade(self):
        linked = self.linked_worktree()
        recipe = (ROOT / "PKGBUILD").read_text()
        legacy = "\n".join(
            'source=("$pkgname::git+file://$(pwd)/.git")'
            if line.startswith("source=(") else line
            for line in recipe.splitlines()
        ) + "\n"
        (linked / "PKGBUILD").write_text(legacy)
        self.git("add", "PKGBUILD", cwd=linked)
        self.git("commit", "--allow-empty", "-m", "Legacy source URL", cwd=linked)
        self.makepkg(cwd=linked)
        (linked / "PKGBUILD").write_text(recipe)
        self.git("add", "PKGBUILD", cwd=linked)
        self.git("commit", "--allow-empty", "-m", "Updated source selection", cwd=linked)
        self.makepkg(cwd=linked)


if __name__ == "__main__":
    unittest.main()
