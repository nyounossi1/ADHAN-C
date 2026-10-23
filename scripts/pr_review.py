#!/usr/bin/env python3
"""AD-56: ask a second, independent LLM to review a PR and post one sticky comment.

Env: OPENAI_API_KEY, REVIEW_MODEL, GH_TOKEN, PR_NUMBER, BASE_REF, GITHUB_REPOSITORY,
     optional REVIEW_REASONING_EFFORT, REVIEW_MAX_DIFF_BYTES.
Advisory only: any failure is reported in the comment/log and the script exits 0.
"""
import json
import os
import subprocess
import sys
import urllib.request

MARKER = "<!-- adhan-pr-review -->"
EXCLUDE = [":(exclude)*.lock", ":(exclude)docs/*.json", ":(exclude)src/QrHelp.h"]


def sh(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def build_diff(base, limit):
    diff = sh("git", "diff", f"origin/{base}...HEAD", "--", ".", *EXCLUDE)
    if len(diff.encode()) <= limit:
        return diff, False
    return diff.encode()[:limit].decode(errors="ignore"), True


def call_model(instructions, user_input):
    body = {
        "model": os.environ["REVIEW_MODEL"],
        "instructions": instructions,
        "input": user_input,
    }
    effort = os.environ.get("REVIEW_REASONING_EFFORT")
    if effort:
        body["reasoning"] = {"effort": effort}
    req = urllib.request.Request(
        "https://api.openai.com/v1/responses",
        data=json.dumps(body).encode(),
        headers={
            "Authorization": f"Bearer {os.environ['OPENAI_API_KEY']}",
            "Content-Type": "application/json",
        },
    )
    with urllib.request.urlopen(req, timeout=600) as resp:
        data = json.load(resp)
    parts = [
        c.get("text", "")
        for item in data.get("output", [])
        if item.get("type") == "message"
        for c in item.get("content", [])
        if c.get("type") == "output_text"
    ]
    return "\n".join(parts).strip()


def post_comment(repo, pr, text):
    api = f"repos/{repo}/issues/{pr}/comments"
    existing = json.loads(sh("gh", "api", api, "--paginate", "--jq", "[.[] | {id, body}]"))
    ids = [c["id"] for c in existing if MARKER in c["body"]]
    payload = json.dumps({"body": text})
    if ids:
        cmd = ["gh", "api", "-X", "PATCH", f"repos/{repo}/issues/comments/{ids[0]}", "--input", "-"]
    else:
        cmd = ["gh", "api", "-X", "POST", api, "--input", "-"]
    subprocess.run(cmd, input=payload, text=True, check=True, capture_output=True)


def main():
    repo, pr = os.environ["GITHUB_REPOSITORY"], os.environ["PR_NUMBER"]
    limit = int(os.environ.get("REVIEW_MAX_DIFF_BYTES", "250000"))
    diff, truncated = build_diff(os.environ["BASE_REF"], limit)
    meta = json.loads(sh("gh", "pr", "view", pr, "--repo", repo, "--json", "title,body"))
    rules = open("CLAUDE.md", encoding="utf-8").read()
    instructions = open(".github/review-prompt.md", encoding="utf-8").read()
    user_input = (
        f"## Project rules (CLAUDE.md)\n{rules}\n\n"
        f"## PR title\n{meta['title']}\n\n## PR body\n{meta['body']}\n\n"
        f"## Diff{' (TRUNCATED)' if truncated else ''}\n```diff\n{diff}\n```\n"
    )
    model = os.environ["REVIEW_MODEL"]
    try:
        review = call_model(instructions, user_input)
        if not review:
            raise RuntimeError("empty response")
    except Exception as exc:  # advisory: never fail the PR
        review = f"Second-LLM review failed: `{type(exc).__name__}: {exc}`"
    note = "\n\n_Diff was truncated; part of the PR was not reviewed._" if truncated else ""
    post_comment(
        repo,
        pr,
        f"{MARKER}\n## Independent PR review (`{model}`)\n\n{review}{note}\n\n"
        "---\n_Advisory only. Claude triages valid findings; a human merges._",
    )


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"pr_review failed: {exc}", file=sys.stderr)
    sys.exit(0)
