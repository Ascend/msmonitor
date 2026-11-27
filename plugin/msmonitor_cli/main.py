# -------------------------------------------------------------------------
# This file is part of the MindStudio project.
# Copyright (c) 2026 Huawei Technologies Co.,Ltd.
#
# MindStudio is licensed under Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#
#          http://license.coscl.org.cn/MulanPSL2
#
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
# EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
# MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
# -------------------------------------------------------------------------

"""Entry point for the ``msmonitor`` command.

``--help`` follows the MindStudio unified CLI layout: Description, Usage,
Required arguments, Optional arguments and Examples, with 3-column aligned
argument rows and ``[default: xxx]`` annotations.
"""

from __future__ import annotations

import argparse
import sys
import textwrap
from typing import List, Optional, Sequence, Tuple

from .commands import (
    EXIT_CONFIG,
    EXIT_ERROR,
    cmd_collect_start,
    cmd_collect_stop,
    cmd_start,
    cmd_stop,
)
from .config import (
    JSONL_EXPORTABLE_KINDS,
    ROTATE_FILES_MIN,
    ROTATE_LINES_MAX,
    ROTATE_LINES_MIN,
    STRING_MAX_LEN,
    ConfigError,
    MonitorConfig,
)

# 帮助正文的折行宽度：示例命令保持单行，仅描述按此宽度折行
_HELP_WIDTH = 100
# 默认值只来自 MonitorConfig，避免 help 文案与实现各写一份
_DEFAULTS = MonitorConfig()


def _uint32(value: str) -> int:
    try:
        number = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError("expected an integer: {!r}".format(value))
    if not 0 <= number <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("must be within [0, 4294967295] (uint32)")
    return number


def _int32(value: str) -> int:
    try:
        number = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError("expected an integer: {!r}".format(value))
    if not -0x80000000 <= number <= 0x7FFFFFFF:
        raise argparse.ArgumentTypeError("must be within [-2147483648, 2147483647] (int32)")
    return number


def _rotate_lines(value: str) -> int:
    try:
        number = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError("expected an integer: {!r}".format(value))
    if not ROTATE_LINES_MIN <= number <= ROTATE_LINES_MAX:
        raise argparse.ArgumentTypeError("must be within [{}, {}]".format(ROTATE_LINES_MIN, ROTATE_LINES_MAX))
    return number


def _rotate_files(value: str) -> int:
    try:
        number = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError("expected an integer: {!r}".format(value))
    if number != -1 and number < ROTATE_FILES_MIN:
        raise argparse.ArgumentTypeError("must be -1 (no cleanup) or at least {}".format(ROTATE_FILES_MIN))
    return number


def _port(value: str) -> int:
    try:
        number = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError("expected an integer: {!r}".format(value))
    if not 1 <= number <= 65535:
        raise argparse.ArgumentTypeError("must be within [1, 65535]")
    return number


def _bounded_text(value: str) -> str:
    if len(value) > STRING_MAX_LEN:
        raise argparse.ArgumentTypeError("must not exceed {} characters".format(STRING_MAX_LEN))
    return value


class UnifiedHelpParser(argparse.ArgumentParser):
    """Prints ``--help`` in the MindStudio unified CLI layout.

    Sections are emitted in the mandated order: Description, Usage, Required
    arguments, Optional arguments and Examples.
    """

    def __init__(self, *args, **kwargs):
        self.examples: Sequence[Tuple[str, str]] = kwargs.pop("examples", ())
        self.output_lines: Sequence[str] = kwargs.pop("output_lines", ())
        self.hint: str = kwargs.pop("hint", "")
        super().__init__(*args, **kwargs)
        # -h/--help 照常可用，但不在帮助正文里列出
        for action in self._actions:
            if action.dest == "help":
                action.hidden_in_help = True

    # -- rendering helpers -------------------------------------------------
    @staticmethod
    def _short_option(action: argparse.Action) -> str:
        for option in action.option_strings:
            if not option.startswith("--"):
                return option + ","
        return ""

    @staticmethod
    def _signature(action: argparse.Action) -> str:
        if not action.option_strings:
            choices = getattr(action, "choices", None)
            if choices:
                return "{" + ",".join(str(choice) for choice in choices) + "}"
            return action.metavar or action.dest
        long_options = [option for option in action.option_strings if option.startswith("--")]
        name = long_options[0] if long_options else action.option_strings[0]
        if action.nargs == 0:
            return name
        return "{} {}".format(name, action.metavar or action.dest.upper())

    def _visible(self, required: bool) -> List[argparse.Action]:
        return [
            action
            for action in self._actions
            if action.required == required
            and action.help is not argparse.SUPPRESS
            and not getattr(action, "hidden_in_help", False)
        ]

    def _usage_line(self) -> str:
        parts = [self.prog]
        parts.extend(self._signature(action) for action in self._visible(True))
        if self._visible(False):
            parts.append("[options]")
        return " ".join(parts)

    def _render_arguments(self, actions: Sequence[argparse.Action]) -> List[str]:
        rows = []
        for action in actions:
            description = action.help or ""
            default = getattr(action, "help_default", None)
            if default is not None:
                description = (
                    "{} [default: {}]".format(description, default) if description else "[default: {}]".format(default)
                )
            rows.append((self._short_option(action), self._signature(action), description))
        if not rows:
            return []

        short_width = max(len(row[0]) for row in rows)
        signature_width = max(len(row[1]) for row in rows)
        if short_width:
            prefix_width = 2 + short_width + 1 + signature_width + 2
        else:
            prefix_width = 2 + signature_width + 2

        lines: List[str] = []
        for short, signature, description in rows:
            if short_width:
                prefix = "  {:<{}} {:<{}}  ".format(short, short_width, signature, signature_width)
            else:
                prefix = "  {:<{}}  ".format(signature, signature_width)
            wrapped = textwrap.wrap(
                description,
                width=_HELP_WIDTH,
                subsequent_indent=" " * prefix_width,
                break_on_hyphens=False,
                break_long_words=False,
            ) or [""]
            lines.append(prefix + wrapped[0])
            lines.extend(wrapped[1:])
        return lines

    @staticmethod
    def _render_description(text: str) -> str:
        return textwrap.fill(
            " ".join(text.split()),
            width=_HELP_WIDTH,
            initial_indent="  ",
            subsequent_indent="  ",
            break_on_hyphens=False,
            break_long_words=False,
        )

    def _render_examples(self) -> str:
        lines: List[str] = []
        for comment, command in self.examples:
            if lines:
                lines.append("")
            lines.append("  # " + comment)
            lines.append("  " + command)
        return "\n".join(lines)

    # -- argparse overrides ------------------------------------------------
    def format_usage(self) -> str:
        return "Usage:\n  {}\n".format(self._usage_line())

    def format_help(self) -> str:
        blocks: List[str] = []
        if self.description:
            blocks.append("Description:\n" + self._render_description(self.description))
        blocks.append("Usage:\n  " + self._usage_line())

        required = self._render_arguments(self._visible(True))
        if required:
            blocks.append("Required arguments:\n" + "\n".join(required))
        optional = self._render_arguments(self._visible(False))
        if optional:
            blocks.append("Optional arguments:\n" + "\n".join(optional))
        if self.examples:
            blocks.append("Examples:\n" + self._render_examples())
        if self.output_lines:
            blocks.append("Output:\n" + "\n".join("  " + line for line in self.output_lines))
        if self.hint:
            blocks.append("  " + self.hint)
        return "\n\n".join(blocks) + "\n"


def _add_option(parser: argparse.ArgumentParser, *flags: str, **kwargs) -> argparse.Action:
    """``add_argument`` wrapper carrying the ``[default: xxx]`` help annotation."""
    default_text = kwargs.pop("default_text", None)
    action = parser.add_argument(*flags, **kwargs)
    if default_text is not None:
        action.help_default = default_text
    return action


def _add_collector_arguments(parser: argparse.ArgumentParser) -> None:
    _add_option(
        parser,
        "--output-dir",
        required=True,
        default=None,
        type=_bounded_text,
        metavar="<DIR>",
        help="collection output directory; reused by 'collect start'",
    )
    _add_option(
        parser,
        "--job-id",
        default=None,
        type=_bounded_text,
        metavar="<ID>",
        help="job identifier; reused by 'collect start'",
    )
    _add_option(
        parser,
        "--trace-steps-per-file",
        type=_int32,
        default=None,
        metavar="<STEPS>",
        help="maximum steps per rank's trace file; 0 writes one step per file",
        default_text=str(_DEFAULTS.trace_steps_per_file),
    )
    _add_option(
        parser,
        "--trace-keep-files",
        type=_int32,
        default=None,
        metavar="<COUNT>",
        help="trace files to keep; the data processing component removes the oldest ones",
        default_text=str(_DEFAULTS.trace_keep_files),
    )
    _add_option(
        parser,
        "--server-host",
        default=None,
        type=_bounded_text,
        metavar="<HOST>",
        help="optional data center host metadata; the Remote Write URL sets the destination",
    )
    _add_option(
        parser,
        "--server-port",
        type=_port,
        default=None,
        metavar="<PORT>",
        help="optional data center port metadata; the Remote Write URL sets the destination",
    )
    _add_option(
        parser,
        "--remote-write-url",
        default=None,
        type=_bounded_text,
        metavar="<URL>",
        help="full HTTPS URL used to write processed metrics to the data center",
    )
    _add_option(
        parser,
        "--token-file",
        default=None,
        type=_bounded_text,
        metavar="<FILE>",
        help="path to the authentication token file",
    )
    _add_option(
        parser,
        "--token",
        default=None,
        type=_bounded_text,
        metavar="<TOKEN>",
        help="plaintext authentication token; mutually exclusive with --token-file",
    )
    _add_option(
        parser,
        "--ca-file",
        default=None,
        type=_bounded_text,
        metavar="<FILE>",
        help="CA certificate for private PKI; defaults to the system trust store",
    )
    _add_option(
        parser,
        "--monitor-target",
        type=_bounded_text,
        default=None,
        metavar="<TARGET>[,<TARGET>...]",
        help="output targets, comma separated: trace writes files to --output-dir, "
        "metric reports to the data center; selecting metric requires the center options",
        default_text=",".join(_DEFAULTS.monitor_target),
    )


def _add_collect_arguments(parser: argparse.ArgumentParser) -> None:
    _add_option(
        parser,
        "--mspti-activity-kind",
        type=_bounded_text,
        default=None,
        metavar="<KIND>[,<KIND>...]",
        help="activity kinds, comma separated: {}".format(", ".join(JSONL_EXPORTABLE_KINDS)),
        default_text=",".join(_DEFAULTS.activity_kind),
    )
    _add_option(
        parser,
        "--report-interval-s",
        type=_uint32,
        default=None,
        metavar="<SEC>",
        help="monitor report interval in seconds (uint32), mapped to REPORT_INTERVAL_S",
        default_text=str(_DEFAULTS.report_interval_s),
    )
    _add_option(
        parser,
        "--json-rotate-log-lines",
        type=_rotate_lines,
        default=None,
        metavar="<LINES>",
        help="maximum lines per JSONL file, range [{}, {}]; left to the "
        "training side when omitted; only effective for Jsonl "
        "export".format(ROTATE_LINES_MIN, ROTATE_LINES_MAX),
    )
    _add_option(
        parser,
        "--json-rotate-log-files",
        type=_rotate_files,
        default=None,
        metavar="<COUNT>",
        help="JSONL files to keep, -1 means no cleanup; left to the "
        "training side when omitted; only effective for Jsonl export",
    )


_OUTPUT_FILES = ("<output-dir>/msmonitor_<pid>_<timestamp>_<rank>.jsonl  collected events per rank",)


def build_parser() -> argparse.ArgumentParser:
    parser = UnifiedHelpParser(
        prog="msmonitor",
        description="Lightweight online performance collection CLI for Ascend "
        "NPU. It runs a background collector that delivers "
        "collection config to training processes, plus a data "
        "processing process that exports per-rank results.",
        examples=(
            (
                "Start the collector and the data processing process",
                "msmonitor start --output-dir /data/msmonitor --monitor-target trace",
            ),
            (
                "Send collection config to running training processes",
                "msmonitor collect start --mspti-activity-kind Marker,Kernel",
            ),
            (
                "Stop collection only; the collector keeps running",
                "msmonitor collect stop",
            ),
            ("Stop the collector and the data processing process", "msmonitor stop"),
        ),
        hint="use 'msmonitor start --help' or 'msmonitor collect --help' for details",
    )
    subparsers = parser.add_subparsers(dest="command", required=True, help="Subcommand to run")

    start = subparsers.add_parser(
        "start",
        description="Start the collector and the data processing process. No "
        "collection config is sent; run 'msmonitor collect start' "
        "afterwards.",
        examples=(
            (
                "Start and only write trace files locally",
                "msmonitor start --output-dir /data/msmonitor --monitor-target trace",
            ),
            (
                "Start and report trace and metrics to the data center",
                "msmonitor start --output-dir /data/msmonitor --monitor-target trace,metric "
                "--server-host 10.0.0.1 --server-port 8443 "
                "--remote-write-url https://center/api/write "
                "--token-file /etc/msmonitor/token --ca-file /etc/msmonitor/ca.pem",
            ),
        ),
        output_lines=_OUTPUT_FILES
        + (
            "<output-dir>/log/msmonitor_collector.<host>.<user>.log.<timestamp>  collector log",
            "<output-dir>/log/msmonitor_processor.<host>.<user>.log.<timestamp>  data processing log",
        ),
        help="start collector and data processing process",
    )
    _add_collector_arguments(start)
    start.set_defaults(handler=cmd_start)

    stop = subparsers.add_parser(
        "stop",
        description="Stop the data processing process, then the collector. Pending collection config is discarded.",
        examples=(("Stop the collector and the data processing process", "msmonitor stop"),),
        help="stop collector and data processing process",
    )
    stop.set_defaults(handler=cmd_stop)

    collect = subparsers.add_parser(
        "collect",
        description="Manage the collection config delivered to training processes.",
        examples=(
            (
                "Send collection config to running training processes",
                "msmonitor collect start",
            ),
            ("Stop collection; the collector keeps running", "msmonitor collect stop"),
        ),
        hint="use 'msmonitor collect start --help' for details",
        help="collection configuration",
    )
    collect_sub = collect.add_subparsers(dest="collect_command", required=True, help="Subcommand to run")

    collect_start = collect_sub.add_parser(
        "start",
        description="Send collection config to running training processes. The "
        "collector is started first when it is not running; run "
        "'msmonitor start --output-dir <DIR>' before this command.",
        examples=(
            ("Collect the default activity kinds", "msmonitor collect start"),
            (
                "Collect Kernel activities",
                "msmonitor collect start --mspti-activity-kind Kernel",
            ),
            (
                "Tune the report interval and JSONL rotation",
                "msmonitor collect start --mspti-activity-kind Kernel "
                "--report-interval-s 10 --json-rotate-log-lines 5000 "
                "--json-rotate-log-files 5",
            ),
        ),
        output_lines=_OUTPUT_FILES,
        help="send collection configuration",
    )
    _add_collect_arguments(collect_start)
    collect_start.set_defaults(handler=cmd_collect_start)

    collect_stop = collect_sub.add_parser(
        "stop",
        description="Send stop config to running training processes. The collector keeps running.",
        examples=(("Stop collection; the collector keeps running", "msmonitor collect stop"),),
        help="send stop configuration (collector keeps running)",
    )
    collect_stop.set_defaults(handler=cmd_collect_stop)

    return parser


def main(argv: Optional[List[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return args.handler(args)
    except ConfigError as exc:
        print("config error: {}".format(exc), file=sys.stderr)
        return EXIT_CONFIG
    except KeyboardInterrupt:
        print("interrupted", file=sys.stderr)
        return EXIT_ERROR


if __name__ == "__main__":
    sys.exit(main())
