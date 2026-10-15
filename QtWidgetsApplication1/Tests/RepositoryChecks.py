"""Offline repository-content checks. Reports paths only, never credential values."""
from pathlib import Path
import json
import re
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def main():
    files = set(git("ls-files", "-z").decode("utf-8").split("\0")) - {""}
    errors = []
    binary_allowlist = {"QtWidgetsApplication1/ThirdParty/WebView2/x86/WebView2Loader.dll"}
    generated = re.compile(r"(^|/)(BuildCheck|\.diagnostics|node_modules|__pycache__|Debug|Release|unpacked)/", re.I)
    for name in sorted(files):
        if generated.search(name):
            errors.append(f"Generated/local file tracked: {name}")
        if re.search(r"\.(exe|dll|pdb|obj|pch|zip|bak|mdf|ldf|pyc)$", name, re.I) and name not in binary_allowlist:
            errors.append(f"Unexpected binary/output tracked: {name}")
        if re.search(r"(^|/)(LemonApp(?:\.[^/]*)?\.dat|session\.dat|SqlServer211\.local\.json|\.env)$", name):
            errors.append(f"Local credential file tracked: {name}")

    # Compare with this machine's migrated values without printing them.
    local = ROOT / "QtWidgetsApplication1/SqlServer211.local.json"
    secrets = []
    if local.exists():
        config = json.loads(local.read_text(encoding="utf-8-sig"))
        secrets = [config.get(key, "") for key in ("password", "sharePassword")]
        secrets = [value for value in secrets if len(value) >= 6]
    key_pattern = re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----|(?:ghp_|github_pat_|sk-proj-)[A-Za-z0-9_-]{20,}")
    for name in sorted(files):
        path = ROOT / name
        if not path.is_file() or path.stat().st_size > 5_000_000 or name in binary_allowlist:
            continue
        try:
            text = path.read_text(encoding="utf-8-sig")
        except UnicodeError:
            continue
        if any(value in text for value in secrets) or key_pattern.search(text):
            errors.append(f"Potential credential in tracked source: {name}")

    project = ROOT / "QtWidgetsApplication1/QtWidgetsApplication1.vcxproj"
    for item in ET.parse(project).iter():
        include = item.get("Include", "")
        tag = item.tag.rsplit("}", 1)[-1]
        if tag not in {"ClCompile", "ClInclude", "QtMoc", "QtRcc", "QtUic", "None"} or not include or "$" in include or "*" in include:
            continue
        relative = "QtWidgetsApplication1/" + include.replace("\\", "/")
        if relative not in files or not (ROOT / relative).is_file():
            errors.append(f"Project dependency missing from repository: {relative}")

    for resource in (ROOT / "QtWidgetsApplication1").glob("*.qrc"):
        for item in ET.parse(resource).iter("file"):
            name = (resource.parent / item.text).relative_to(ROOT).as_posix()
            if name not in files or not (ROOT / name).is_file():
                errors.append(f"Qt resource missing from repository: {name}")

    probes = [".diagnostics/example.json", "BuildCheck/example.exe", "Release/LemonApp.Pro.dat",
              "QtWidgetsApplication1/SqlServer211.local.json", "LemonChromeExtension/node_modules/example.js",
              "LemonChromeExtension/unpacked/manifest.json", "Tests/__pycache__/example.pyc"]
    for name in probes:
        result = subprocess.run(["git", "check-ignore", "--no-index", "-q", name], cwd=ROOT)
        if result.returncode != 0:
            errors.append(f"Expected local path is not ignored: {name}")
    if errors:
        print("\n".join(errors))
        return 1
    print(f"PASS: {len(files)} tracked paths; generated files, credentials, project/resource dependencies and ignore rules checked.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
