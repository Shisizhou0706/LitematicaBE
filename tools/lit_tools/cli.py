"""命令行入口：`python -m lit_tools <command>`。"""

from __future__ import annotations

import argparse
import sys

from . import addon, mapping, paths, version


def _cmd_version(_: argparse.Namespace) -> int:
    header = version.read_header_version()
    print(f"Version.h      : {header}")
    print(f"xmake.lua      : {version.read_xmake_version()}")
    print(f"lip.toml       : {version.read_lip_version()}")
    print(f"CHANGELOG.md   : {version.read_changelog_version()}")
    errors = version.check()
    if errors:
        for err in errors:
            print(f"[x] {err}", file=sys.stderr)
        return 1
    print("[ok] 版本一致")
    return 0


def _cmd_addon(_: argparse.Namespace) -> int:
    report = addon.check()
    print(f"BP 方块定义 : {report.bp_blocks}")
    print(f"RP 贴图     : {report.rp_textures}")
    print(f"BP 版本     : {'.'.join(map(str, report.bp_version))}")
    print(f"RP 版本     : {'.'.join(map(str, report.rp_version))}")
    for warning in report.warnings:
        print(f"[!] {warning}")
    for err in report.errors[:50]:
        print(f"[x] {err}", file=sys.stderr)
    if len(report.errors) > 50:
        print(f"[x] ... 其余 {len(report.errors) - 50} 条省略", file=sys.stderr)
    if not report.ok:
        return 1
    print("[ok] BP/RP 一致")
    return 0


def _cmd_mapping(args: argparse.Namespace) -> int:
    target = mapping.write()
    try:
        shown = target.relative_to(paths.repo_root())
    except ValueError:
        shown = target
    print(f"[ok] 已生成 {shown}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="lit_tools", description="LitematicaBE 离线工具链")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("version", help="校验 Version.h / lip.toml / CHANGELOG 版本一致").set_defaults(
        func=_cmd_version
    )
    sub.add_parser("addon", help="校验 BP/RP 的版本、identifier 与贴图引用").set_defaults(
        func=_cmd_addon
    )
    sub.add_parser("mapping", help="从 BP 生成 resources/data/generated/ghost_blocks.json").set_defaults(
        func=_cmd_mapping
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
