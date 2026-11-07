"""Regression checks for coverage-common's fprime-tools 4.3 invocation."""

import json
import sys

import pytest

import adapter


def test_coverage_arguments_are_only_forwarded_to_gcovr():
    args = [
        "check",
        "--all",
        "--coverage",
        "-j4",
        "native",
        "--comp-ac",
        "--pass-through",
        "--json-summary",
        "coverage/a summary.json",
    ]
    assert adapter.commands(args) == [
        ["check", "--all", "-j4", "native"],
        args[:6] + ["--existing"] + args[6:],
    ]


@pytest.mark.parametrize(
    "args",
    [
        ["check", "--pass-through", "--output-on-failure"],
        ["check", "--coverage", "--existing", "--pass-through", "--json-summary", "x"],
        ["build", "--all"],
        ["check", "--pass-through", "--coverage"],
    ],
)
def test_other_invocations_are_unchanged(args):
    assert adapter.commands(args) == [args]


@pytest.mark.parametrize("failure_stage", [None, 0, 1])
def test_subprocess_status_and_order(tmp_path, monkeypatch, failure_stage):
    log = tmp_path / "calls.jsonl"
    fake = tmp_path / "fake-util.py"
    fake.write_text(
        "import json, pathlib, sys\n"
        f"log = pathlib.Path({str(log)!r})\n"
        "calls = log.read_text().splitlines() if log.exists() else []\n"
        "with log.open('a') as out: out.write(json.dumps(sys.argv[1:]) + '\\n')\n"
        f"sys.exit(17 if len(calls) == {failure_stage!r} else 0)\n"
    )
    run = adapter.subprocess.run

    def run_fake(command, **kwargs):
        assert command[0] == "real-fprime-util"
        return run([sys.executable, str(fake), *command[1:]], **kwargs)

    monkeypatch.setattr(adapter.subprocess, "run", run_fake)
    args = ["check", "--coverage", "--pass-through", "--json-summary", "summary.json"]
    assert adapter.main(args, "real-fprime-util") == (
        0 if failure_stage is None else 17
    )
    calls = [json.loads(line) for line in log.read_text().splitlines()]
    assert calls == adapter.commands(args)[: 1 if failure_stage == 0 else 2]
