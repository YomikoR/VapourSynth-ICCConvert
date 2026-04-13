import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

from hatchling.builders.hooks.plugin.interface import BuildHookInterface
from packaging import tags


class CustomHook(BuildHookInterface[Any]):
    source_dir = Path("build")
    if sys.platform == "win32":
        target_dir = Path("vapoursynth/plugins")
    else:
        target_dir = Path("vapoursynth/plugins/iccc")

    def initialize(self, version: str, build_data: dict[str, Any]) -> None:
        if self.source_dir.exists():
            shutil.rmtree(self.source_dir)

        build_data["pure_python"] = False
        build_data["tag"] = f"py3-none-{next(tags.platform_tags())}"

        subprocess.run(
            [
                sys.executable,
                "-m",
                "mesonbuild.mesonmain",
                "setup",
                "build",
            ],
            check=True,
        )
        subprocess.run([sys.executable, "-m", "mesonbuild.mesonmain", "compile", "-C", "build"], check=True)

        self.target_dir.mkdir(parents=True, exist_ok=True)
        for file_path in self.source_dir.glob("*"):
            if file_path.is_file() and file_path.suffix in [".dll", ".so", ".dylib"]:
                shutil.copy2(file_path, self.target_dir)
        if sys.platform != "win32":
            shutil.copy2(Path("manifest.vs"), self.target_dir)

    def finalize(self, version: str, build_data: dict[str, Any], artifact_path: str) -> None:
        shutil.rmtree(self.target_dir.parent, ignore_errors=True)
