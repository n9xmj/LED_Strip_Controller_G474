"""Shell command handlers."""

from __future__ import annotations

import os
import shutil
import sys
from pathlib import Path

from . import helptext, pathutil, xfer
from .device_ops import DeviceFS
from .parser import Parsed
from .pathutil import PathState
from .transport import Transport


class ShellContext:
    def __init__(
        self,
        t: Transport,
        *,
        auto_yes: bool = False,
        host_cwd: Path | None = None,
        batch: bool = False,
    ) -> None:
        self.t = t
        self.dev = DeviceFS(t)
        self.state = PathState(host_cwd=host_cwd)
        self.should_exit = False
        self.auto_yes = auto_yes
        self.batch = batch
        self.errors = 0

    def fail(self, msg: str) -> None:
        print(msg)
        self.errors += 1


def _confirm(ctx: ShellContext, msg: str, yes: bool) -> bool:
    if yes or ctx.auto_yes:
        return True
    if ctx.batch or not sys.stdin.isatty():
        # Headless: do not hang; treat as decline unless -y was given
        ctx.fail(f"{msg} [y/N] — declined (batch/non-TTY; use -y or --yes)")
        return False
    try:
        ans = input(f"{msg} [y/N] ").strip().lower()
    except EOFError:
        return False
    return ans in ("y", "yes")


def _arf(ctx: ShellContext, label: str, yes: bool) -> str:
    """Return 'retry' | 'skip' | 'abort'."""
    if yes or ctx.auto_yes:
        print(f"  skipped: {label}")
        return "skip"
    if ctx.batch or not sys.stdin.isatty():
        print(f"  skipped (batch): {label}")
        return "skip"
    while True:
        try:
            ans = input(f"Failed: {label}\n[R]etry / [S]kip / [A]bort? ").strip().lower()
        except EOFError:
            return "abort"
        if ans in ("r", "retry"):
            return "retry"
        if ans in ("s", "skip", ""):
            return "skip"
        if ans in ("a", "abort"):
            return "abort"


def run(ctx: ShellContext, p: Parsed) -> bool:
    """Run one parsed command. Returns True if no error recorded for this line."""
    before = ctx.errors
    if p.error:
        ctx.fail(f"parse error: {p.error}")
        return False
    if not p.verb:
        return True
    if p.local and p.remote_explicit:
        ctx.fail("error: -L/--local and --remote are mutually exclusive")
        return False

    verb = helptext.resolve_verb(p.verb)

    if p.help or verb in ("help",):
        topic = p.args[0] if p.args else (None if verb == "help" else verb)
        if verb == "help":
            print(helptext.help_for(topic), end="")
        else:
            print(helptext.help_for(verb), end="")
        return True

    handlers = {
        "ls": cmd_ls,
        "cd": cmd_cd,
        "pwd": cmd_pwd,
        "cp": cmd_cp,
        "mv": cmd_mv,
        "rm": cmd_rm,
        "mkdir": cmd_mkdir,
        "rmdir": cmd_rmdir,
        "cat": cmd_cat,
        "more": cmd_more,
        "get": cmd_get,
        "put": cmd_put,
        "mount": cmd_mount,
        "umount": cmd_umount,
        "format": cmd_format,
        "prompt": cmd_prompt,
        "exit": cmd_exit,
        "stat": cmd_stat,
        "echo": cmd_echo,
    }
    fn = handlers.get(verb)
    if not fn:
        ctx.fail(f"unknown command: {p.verb}  (try 'help')")
        return False
    # get/put reject world flags
    if verb in ("get", "put") and (p.local or p.remote_explicit):
        ctx.fail(f"syntax error: world flags not allowed on '{verb}'")
        return False
    try:
        fn(ctx, p)
    except Exception as ex:
        ctx.fail(f"error: {ex}")
        return False
    return ctx.errors == before


def cmd_exit(ctx: ShellContext, p: Parsed) -> None:
    """Leave fileops REPL → harness → debug menu, then end host shell."""
    try:
        ctx.t.quit_session()  # Q/FSEND, 0xA5, ESC climb to menu
        print("Left fileops + harness; board at debug menu.")
    except Exception as ex:
        print(f"exit unwind: {ex}")
    ctx.should_exit = True


def cmd_echo(ctx: ShellContext, p: Parsed) -> None:
    """Print arguments (alias: print). Flags are not part of the text."""
    print(" ".join(p.args))


def cmd_prompt(ctx: ShellContext, p: Parsed) -> None:
    if not p.args or p.args[0] not in ("local", "remote", "none"):
        print("usage: prompt local|remote|none")
        return
    ctx.state.prompt_mode = p.args[0]


def cmd_pwd(ctx: ShellContext, p: Parsed) -> None:
    if p.local:
        print(ctx.state.host_cwd)
    else:
        print(ctx.state.remote_cwd)


def cmd_cd(ctx: ShellContext, p: Parsed) -> None:
    if p.local:
        target = pathutil.join_host(ctx.state.host_cwd, p.args[0] if p.args else str(ctx.state.host_cwd))
        if not p.args:
            target = Path.home()
        if not target.is_dir():
            ctx.fail(f"not a directory: {target}")
            return
        ctx.state.host_cwd = target.resolve()
    else:
        if not p.args:
            ctx.fail("usage: cd <path>")
            return
        path = pathutil.join_remote(ctx.state.remote_cwd, p.args[0])
        # verify exists as dir via ST or LS
        d = ctx.dev.stat(path)
        if d.get("rc") != "0":
            # might be volume root
            rc, _ents, raw = ctx.dev.ls(path)
            if rc != 0:
                ctx.fail(f"cd failed: {path} (rc={d.get('rc', rc)})")
                if raw.strip():
                    print(f"  raw: {raw.strip()[:300]}")
                return
        elif d.get("type") not in ("2",):  # LFS_TYPE_DIR == 2 typically
            # littlefs: LFS_TYPE_REG=1, LFS_TYPE_DIR=2
            if d.get("type") == "1":
                ctx.fail(f"not a directory: {path}")
                return
        ctx.state.remote_cwd = path


def _lfs_type_name(type_s: str) -> str:
    """littlefs lfs_type: 1=reg, 2=dir (other = raw)."""
    if type_s == "1":
        return "file"
    if type_s == "2":
        return "dir"
    return f"t{type_s}"


def _fmt_size(n: int | str) -> str:
    try:
        v = int(n)
    except (TypeError, ValueError):
        return str(n)
    return f"{v:>10}"


def _print_remote_ent(e: dict, *, long: bool) -> None:
    name = e.get("name", "")
    if name in (".", ".."):
        return
    if not long:
        print(name + ("/" if e.get("type") == "2" else ""))
        return
    # type  size  name
    tname = _lfs_type_name(e.get("type", "?"))
    size = e.get("size", "0") if e.get("type") != "2" else "-"
    if e.get("type") == "2":
        print(f"{tname:4}  {_fmt_size(size)}  {name}/")
    else:
        print(f"{tname:4}  {_fmt_size(size)}  {name}")


def _print_host_path(path: Path, *, long: bool, display_name: str | None = None) -> None:
    name = display_name if display_name is not None else path.name
    if not long:
        print(name + ("/" if path.is_dir() else ""))
        return
    try:
        st = path.stat()
        if path.is_dir():
            print(f"{'dir':4}  {'-':>10}  {name}/")
        else:
            print(f"{'file':4}  {_fmt_size(st.st_size)}  {name}")
    except OSError as ex:
        print(f"{'?':4}  {'?':>10}  {name}  ({ex})")


def cmd_ls(ctx: ShellContext, p: Parsed) -> None:
    long = p.long_list

    if p.local:
        path = pathutil.join_host(ctx.state.host_cwd, p.args[0] if p.args else ".")
        if pathutil.is_glob(p.args[0] if p.args else ""):
            paths = pathutil.expand_host_glob(ctx.state.host_cwd, p.args[0])
            if long and paths:
                print(f"{'type':4}  {'size':>10}  name")
            for x in paths:
                _print_host_path(x, long=long)
            return
        if not path.exists():
            ctx.fail(f"not found: {path}")
            return
        if path.is_file():
            if long:
                print(f"{'type':4}  {'size':>10}  name")
            _print_host_path(path, long=long)
            return
        names = sorted(os.listdir(path))
        if long and names:
            print(f"{'type':4}  {'size':>10}  name")
        for name in names:
            _print_host_path(path / name, long=long)
        return

    # remote
    if p.args and pathutil.is_glob(p.args[0]):
        joined = pathutil.join_remote(ctx.state.remote_cwd, p.args[0])
        parent = pathutil.parent_remote(joined)
        rc, ents, raw = ctx.dev.ls(parent)
        if rc != 0:
            ctx.fail(f"ls failed rc={rc}")
            if raw.strip():
                print(f"  raw: {raw.strip()[:300]}")
            return
        names = [e.get("name", "") for e in ents if e.get("name") not in (".", "..")]
        match = set(pathutil.expand_remote_glob(names, pathutil.basename_remote(joined)))
        shown = [e for e in ents if e.get("name") in match]
        if long and shown:
            print(f"{'type':4}  {'size':>10}  name")
        for e in shown:
            _print_remote_ent(e, long=long)
        return

    path = pathutil.join_remote(ctx.state.remote_cwd, p.args[0] if p.args else ".")
    if p.args and p.args[0] in (".",):
        path = ctx.state.remote_cwd
    elif not p.args:
        path = ctx.state.remote_cwd
    rc, ents, raw = ctx.dev.ls(path)
    if rc != 0:
        ctx.fail(f"ls failed path={path} rc={rc}")
        if raw.strip():
            print(f"  raw: {raw.strip()[:400]}")
        if "ERR cmd=R" in raw or "cmd=R" in raw:
            print("  hint: firmware has no harness R op — flash the build that includes fs_shell_hrn.c")
        if "{Ready}:" in raw or "not recognized" in raw or "PLAY>" in raw:
            print("  hint: not in test harness — exit and re-run with: python fs_shell.py --reset")
        return
    visible = [e for e in ents if e.get("name") not in (".", "..")]
    if long and visible:
        print(f"{'type':4}  {'size':>10}  name")
    for e in ents:
        _print_remote_ent(e, long=long)


def cmd_stat(ctx: ShellContext, p: Parsed) -> None:
    if not p.args:
        print("usage: stat <path>")
        return
    if p.local:
        path = pathutil.join_host(ctx.state.host_cwd, p.args[0])
        if not path.exists():
            print(f"not found: {path}")
            return
        st = path.stat()
        print(f"size={st.st_size} file={path.is_file()} dir={path.is_dir()}")
        return
    path = pathutil.join_remote(ctx.state.remote_cwd, p.args[0])
    d = ctx.dev.stat(path)
    print(d)


def cmd_mkdir(ctx: ShellContext, p: Parsed) -> None:
    if not p.args:
        print("usage: mkdir <path>")
        return
    if p.local:
        path = pathutil.join_host(ctx.state.host_cwd, p.args[0])
        path.mkdir(parents=False, exist_ok=False)
        print(f"created {path}")
        return
    path = pathutil.join_remote(ctx.state.remote_cwd, p.args[0])
    d = ctx.dev.mkdir(path)
    print(f"rc={d.get('rc')} path={path}")


def cmd_rm(ctx: ShellContext, p: Parsed) -> None:
    if not p.args:
        print("usage: rm <path...>")
        return
    targets = _expand_targets(ctx, p)
    for t in targets:
        label = str(t)
        while True:
            if not _confirm(ctx, f"Delete {label}?", p.yes):
                print("aborted")
                return
            try:
                if p.local:
                    Path(t).unlink()
                    print(f"deleted {t}")
                else:
                    d = ctx.dev.rm(str(t))
                    if d.get("rc") != "0":
                        raise RuntimeError(f"rc={d.get('rc')}")
                    print(f"deleted {t}")
                break
            except Exception as ex:
                print(f"error: {ex}")
                act = _arf(ctx, label, p.yes)
                if act == "retry":
                    continue
                if act == "skip":
                    break
                return


def cmd_rmdir(ctx: ShellContext, p: Parsed) -> None:
    if not p.args:
        print("usage: rmdir <path>")
        return
    if not _confirm(ctx, f"Remove dir {p.args[0]}?", p.yes):
        return
    if p.local:
        path = pathutil.join_host(ctx.state.host_cwd, p.args[0])
        path.rmdir()
        print(f"removed {path}")
        return
    path = pathutil.join_remote(ctx.state.remote_cwd, p.args[0])
    d = ctx.dev.rm(path)  # littlefs remove works on empty dirs
    print(f"rc={d.get('rc')} path={path}")


def cmd_mv(ctx: ShellContext, p: Parsed) -> None:
    if len(p.args) < 2:
        print("usage: mv <src> <dst>")
        return
    if p.local:
        src = pathutil.join_host(ctx.state.host_cwd, p.args[0])
        dst = pathutil.join_host(ctx.state.host_cwd, p.args[1])
        src.rename(dst)
        print(f"{src} -> {dst}")
        return
    src = pathutil.join_remote(ctx.state.remote_cwd, p.args[0])
    dst = pathutil.join_remote(ctx.state.remote_cwd, p.args[1])
    d = ctx.dev.rename(src, dst)
    print(f"rc={d.get('rc')} {src} -> {dst}")


def cmd_cp(ctx: ShellContext, p: Parsed) -> None:
    if len(p.args) < 2:
        print("usage: cp <src> <dst>  (same world; use get/put for cross)")
        return
    if p.local:
        srcs = pathutil.expand_host_glob(ctx.state.host_cwd, p.args[0])
        dst = pathutil.join_host(ctx.state.host_cwd, p.args[1])
        for src in srcs:
            while True:
                try:
                    target = dst / src.name if dst.is_dir() else dst
                    shutil.copy2(src, target)
                    print(f"copied {src} -> {target}")
                    break
                except Exception as ex:
                    act = _arf(ctx, str(src), p.yes)
                    if act == "retry":
                        continue
                    if act == "skip":
                        break
                    return
        return
    # remote: get to temp then put — or read/write via get_file put_file
    src = pathutil.join_remote(ctx.state.remote_cwd, p.args[0])
    dst = pathutil.join_remote(ctx.state.remote_cwd, p.args[1])
    ok, data = xfer.get_file(ctx.t, src)
    if not ok:
        ctx.fail(f"cp read failed: {data}")
        return
    assert isinstance(data, bytes)
    ok2, msg = xfer.put_file(ctx.t, dst, data)
    if ok2:
        print(msg)
    else:
        ctx.fail(f"cp write failed: {msg}")


def cmd_cat(ctx: ShellContext, p: Parsed) -> None:
    if not p.args:
        print("usage: cat <path>")
        return
    text = _read_text(ctx, p)
    if text is not None:
        print(text, end="" if text.endswith("\n") else "\n")


def cmd_more(ctx: ShellContext, p: Parsed) -> None:
    if not p.args:
        print("usage: more <path>")
        return
    text = _read_text(ctx, p)
    if text is None:
        return
    lines = text.splitlines()
    page = 24
    for i in range(0, len(lines), page):
        chunk = lines[i : i + page]
        print("\n".join(chunk))
        if i + page < len(lines):
            try:
                input("--more--")
            except EOFError:
                break


def _read_text(ctx: ShellContext, p: Parsed) -> str | None:
    if p.local:
        path = pathutil.join_host(ctx.state.host_cwd, p.args[0])
        try:
            return path.read_text(encoding="utf-8", errors="replace")
        except Exception as ex:
            print(f"error: {ex}")
            return None
    path = pathutil.join_remote(ctx.state.remote_cwd, p.args[0])
    ok, data = xfer.get_file(ctx.t, path)
    if not ok:
        print(f"error: {data}")
        return None
    assert isinstance(data, bytes)
    return data.decode("utf-8", errors="replace")


def cmd_get(ctx: ShellContext, p: Parsed) -> None:
    """Remote → host. Symmetric with put: confirm before overwriting destination."""
    if not p.args:
        print("usage: get <remote> [host]   (-y/--force to overwrite without prompt)")
        return
    rpath = pathutil.join_remote(ctx.state.remote_cwd, p.args[0])
    if len(p.args) >= 2:
        hpath = pathutil.join_host(ctx.state.host_cwd, p.args[1])
    else:
        hpath = ctx.state.host_cwd / pathutil.basename_remote(rpath)

    if hpath.exists():
        if hpath.is_dir():
            ctx.fail(f"get: host path is a directory: {hpath}")
            return
        if not _confirm(ctx, f"Overwrite host '{hpath}'?", p.yes):
            print("aborted")
            ctx.errors += 1
            return

    ok, data = xfer.get_file(ctx.t, rpath)
    if not ok:
        ctx.fail(f"get failed: {data}")
        return
    assert isinstance(data, bytes)
    hpath.parent.mkdir(parents=True, exist_ok=True)
    hpath.write_bytes(data)
    print(f"get {rpath} -> {hpath} ({len(data)} bytes)")


def cmd_put(ctx: ShellContext, p: Parsed) -> None:
    """Host → remote. Symmetric with get: confirm before overwriting destination."""
    if not p.args:
        ctx.fail("usage: put <host> [remote]   (-y/--force to overwrite without prompt)")
        return
    hpath = pathutil.join_host(ctx.state.host_cwd, p.args[0])
    if not hpath.is_file():
        ctx.fail(f"not a file: {hpath}")
        return
    if len(p.args) >= 2:
        rpath = pathutil.join_remote(ctx.state.remote_cwd, p.args[1])
    else:
        rpath = pathutil.join_remote(ctx.state.remote_cwd, hpath.name)

    # Overwrite policy (host-side): device open uses O_TRUNC when transfer runs.
    st = ctx.dev.stat(rpath)
    rc = st.get("rc")
    if rc == "0":
        if st.get("type") == "2":
            ctx.fail(f"put: remote path is a directory: {rpath}")
            return
        if not _confirm(ctx, f"Overwrite remote '{rpath}'?", p.yes):
            print("aborted")
            ctx.errors += 1
            return
    # rc != 0 → treat as missing (ENOENT); proceed to create

    data = hpath.read_bytes()
    ok, msg = xfer.put_file(ctx.t, rpath, data)
    if ok:
        print(msg)
    else:
        ctx.fail(f"put failed: {msg}")


def cmd_mount(ctx: ShellContext, p: Parsed) -> None:
    if not p.args:
        print("usage: mount <label>")
        return
    d = ctx.dev.mount(p.args[0])
    print(d)


def cmd_umount(ctx: ShellContext, p: Parsed) -> None:
    if not p.args:
        print("usage: umount <label>")
        return
    d = ctx.dev.unmount(p.args[0])
    print(d)


def cmd_format(ctx: ShellContext, p: Parsed) -> None:
    if not p.args:
        print("usage: format <label>")
        return
    if not _confirm(ctx, f"Format littlefs '{p.args[0]}'? THIS WIPES DATA", p.yes):
        print("aborted")
        return
    d = ctx.dev.format(p.args[0])
    print(d)


def _expand_targets(ctx: ShellContext, p: Parsed) -> list:
    out = []
    for a in p.args:
        if p.local:
            out.extend(pathutil.expand_host_glob(ctx.state.host_cwd, a))
        else:
            if pathutil.is_glob(a):
                joined = pathutil.join_remote(ctx.state.remote_cwd, a)
                parent = pathutil.parent_remote(joined)
                rc, ents, _raw = ctx.dev.ls(parent)
                names = [e.get("name", "") for e in ents]
                for n in pathutil.expand_remote_glob(names, pathutil.basename_remote(joined)):
                    out.append(f"{parent.rstrip('/')}/{n}")
            else:
                out.append(pathutil.join_remote(ctx.state.remote_cwd, a))
    return out
