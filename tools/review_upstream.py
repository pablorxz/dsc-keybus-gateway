"""Fetch public upstream review evidence into ignored build storage; never changes vendor."""

import concurrent.futures
import argparse
from datetime import datetime, timezone
import difflib
import json
from pathlib import Path
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / "build/upstream-review"
REPO = "taligentx/dscKeybusInterface"


def api(path, paginate=False):
    command = ["gh", "api", f"repos/{REPO}/{path}".rstrip("/")]
    if paginate:
        command += ["--paginate", "--slurp"]
    result = json.loads(subprocess.check_output(command, text=True, encoding="utf-8"))
    return [item for page in result for item in page] if paginate else result


def save(name, value):
    path = CACHE / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--details", action="store_true", help="Also fetch issue comments and selected PR diffs/reviews.")
    args = parser.parse_args()
    CACHE.mkdir(parents=True, exist_ok=True)
    baseline = json.loads((ROOT / "tests/reference-taligentx.lock.json").read_text(encoding="utf-8"))["commit"]
    jobs = {
        "repository.json": lambda: api(""),
        "branches.json": lambda: api("branches?per_page=100"),
        "issues.json": lambda: api("issues?state=all&per_page=100", True),
        "pulls.json": lambda: api("pulls?state=all&per_page=100", True),
        "compare-develop.json": lambda: api(f"compare/{baseline}...develop"),
    }
    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
        futures = {pool.submit(fetch): name for name, fetch in jobs.items()}
        for future in concurrent.futures.as_completed(futures):
            save(futures[future], future.result())
    issues = json.loads((CACHE / "issues.json").read_text())
    pulls = json.loads((CACHE / "pulls.json").read_text())
    comparison = json.loads((CACHE / "compare-develop.json").read_text())
    branches = json.loads((CACHE / "branches.json").read_text())
    develop = next(branch["commit"]["sha"] for branch in branches if branch["name"] == "develop")
    tree = api(f"git/trees/{develop}?recursive=1")
    paths = [entry["path"] for entry in tree["tree"]
             if entry["type"] == "blob" and (entry["path"].startswith("src/") or entry["path"] in {"LICENSE", "library.json", "library.properties"})]

    def download(path):
        url = f"https://raw.githubusercontent.com/{REPO}/{develop}/{path}"
        with urllib.request.urlopen(url, timeout=45) as response:
            content = response.read()
        target = CACHE / "develop" / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content)

    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        list(pool.map(download, paths))
    diff = []
    for file in comparison["files"]:
        if not file["filename"].startswith("src/"):
            continue
        original = ROOT / "tests/reference-taligentx" / file["filename"]
        changed = CACHE / "develop" / file["filename"]
        diff += difflib.unified_diff(
            original.read_text(encoding="utf-8-sig").splitlines(True) if original.exists() else [],
            changed.read_text(encoding="utf-8-sig").splitlines(True) if changed.exists() else [],
            fromfile="master/" + file["filename"], tofile="develop/" + file["filename"])
    (CACHE / "master-vs-develop.diff").write_text("".join(diff), encoding="utf-8")
    actual_issues = [issue for issue in issues if "pull_request" not in issue]
    if args.details:
        selected = {374, 368, 363, 362, 358, 339, 329, 290, 283, 150, 147,
                    144, 173, 165, 161, 192, 187, 182, 296, 299, 297}
        details = [(f"comments/{issue['number']}.json",
                    f"issues/{issue['number']}/comments?per_page=100")
                   for issue in actual_issues if issue["comments"]]
        for pull in pulls:
            number = pull["number"]
            if number not in selected and pull["state"] != "open":
                continue
            details.extend([
                (f"pr-details/{number}.json", f"pulls/{number}"),
                (f"pr-files/{number}.json", f"pulls/{number}/files?per_page=100"),
                (f"pr-comments/{number}.json", f"issues/{number}/comments?per_page=100"),
                (f"pr-reviews/{number}.json", f"pulls/{number}/reviews?per_page=100"),
            ])
        def fetch_detail(task):
            name, endpoint = task
            save(name, api(endpoint))
        with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
            list(pool.map(fetch_detail, details))
    save("snapshot.json", {"fetched_utc": datetime.now(timezone.utc).isoformat(),
                           "repository": REPO, "master": comparison["base_commit"]["sha"],
                           "develop": develop, "details_requested": args.details})
    print(json.dumps({"cache": str(CACHE), "develop": develop,
                      "issues_open": sum(item["state"] == "open" for item in actual_issues),
                      "issues_closed": sum(item["state"] == "closed" for item in actual_issues),
                      "pulls_open": sum(item["state"] == "open" for item in pulls),
                      "pulls_merged": sum(bool(item["merged_at"]) for item in pulls),
                      "pulls_closed_unmerged": sum(item["state"] == "closed" and not item["merged_at"] for item in pulls)}, indent=2))


if __name__ == "__main__":
    main()
