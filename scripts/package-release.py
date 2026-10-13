"""校验发布版本，并从指定 Git 提交生成源码包、说明和校验清单。"""

import argparse
import hashlib
import json
import re
import subprocess
import tarfile
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERSION = r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)"


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True, encoding="utf-8").strip()


def describe(path):
    return {"name": path.name, "size": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--require-tag", action="store_true")
    args = parser.parse_args()
    if not re.fullmatch("v" + VERSION, args.tag):
        parser.error("Tag must have the form vMAJOR.MINOR.PATCH without leading zeroes")

    revision = git("rev-parse", "HEAD")
    if args.require_tag and git("rev-parse", f"refs/tags/{args.tag}^{{commit}}") != revision:
        parser.error("Release tag does not point to the checked-out commit")
    cmake = git("show", f"{revision}:CMakeLists.txt")
    match = re.search(r"project\(axisly\s+VERSION\s+(" + VERSION + r")\s", cmake)
    if not match or args.tag != "v" + match[1]:
        parser.error("Release tag does not match the CMake project version")
    version = match[1]
    changelog = git("show", f"{revision}:CHANGELOG.md")
    entry = re.search(r"^## " + re.escape(version)
                      + r" - (\d{4}-\d{2}-\d{2})\n(.*?)(?=^## |\Z)",
                      changelog, re.MULTILINE | re.DOTALL)
    if not entry or not entry[2].strip():
        parser.error("The changelog must contain a dated, nonempty entry for this version")

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        parser.error("Use an empty output directory to avoid mixing release artifacts")
    assets = output / "assets"
    assets.mkdir()
    prefix = f"axisly-{version}/"
    files = []
    expected = {prefix + name for name in git("ls-tree", "-r", "--name-only", revision).splitlines()}
    for extension in ("zip", "tar.gz"):
        archive = assets / f"axisly-{version}-source.{extension}"
        subprocess.run(["git", "archive", f"--format={extension}", f"--prefix={prefix}",
                        f"--output={archive}", revision], cwd=ROOT, check=True)
        if extension == "zip":
            with zipfile.ZipFile(archive) as package:
                if package.testzip() is not None:
                    raise RuntimeError("Source ZIP failed its CRC check")
                members = {item.filename for item in package.infolist() if not item.is_dir()}
        else:
            with tarfile.open(archive, "r:gz") as package:
                members = {item.name for item in package if not item.isdir()}
        if members != expected:
            raise RuntimeError(f"{archive.name} does not contain the complete Git source tree")
        files.append(describe(archive))

    checksums = assets / "SHA256SUMS.txt"
    checksums.write_text("".join(f"{item['sha256']}  {item['name']}\n" for item in files),
                         encoding="utf-8", newline="\n")
    files.append(describe(checksums))
    manifest = {"version": version, "tag": args.tag, "commit": revision,
                "release_date": entry[1], "assets": files}
    (assets / "release-manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    # 独立保存同一次打包的完整报告，供远端资产下载后逐项核对。
    report = {**manifest, "assets": [describe(path) for path in sorted(assets.iterdir())]}
    (output / "asset-report.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    notes = entry[2].strip() + "\n\n"
    notes += (f"完整变更与迁移说明：[CHANGELOG.md]"
              f"(https://github.com/Hunlongyu/axisly/blob/{args.tag}/CHANGELOG.md)。\n")
    (output / "release-notes.md").write_text(notes, encoding="utf-8", newline="\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
