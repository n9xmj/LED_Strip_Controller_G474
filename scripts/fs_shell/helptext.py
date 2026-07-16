"""Short command list + mini-manpages (T2)."""

from __future__ import annotations

ALIASES = {
    "dir": "ls",
    "chdir": "cd",
    "rcd": "cd",
    "rpwd": "pwd",
    "copy": "cp",
    "move": "mv",
    "ren": "mv",
    "rename": "mv",
    "del": "rm",
    "erase": "rm",
    "md": "mkdir",
    "rd": "rmdir",
    "type": "cat",
    "download": "get",
    "upload": "put",
    "unmount": "umount",
    "man": "help",
    "?": "help",
    "quit": "exit",
    "print": "echo",
    "resync": "sync",
}

SHORT = """\
Commands (default world = remote; -L/--local = host):
  ls/dir      cd/chdir    pwd         cp/copy     mv/move/ren
  rm/del      mkdir/md    rmdir/rd    cat/type    more
  get/download  put/upload   mount  umount  format
  echo/print  prompt      sync/resync help/?/man  exit/quit
Flags: -L/--local  --remote  -l/--long (ls)  -y/--yes/-f  -h/--help
Use: help <command>   or   man <command>
Comments: full-line or trailing  # comment  (not inside quotes)

Batch / headless (host process):
  python scripts/fs_shell_smoke.py
      # host unit + HIL asserts (exit code, latency, put/get, batch script)
  python scripts/fs_shell.py --reset -y --host-cwd test-sandbox scripts/fs_shell_smoke.txt
  python scripts/fs_shell.py script.txt
  Prefer untracked ./test-sandbox (repo root) as --host-cwd — not scripts/.
  Lines are commands; blank and # comments ignored. Exit code 1 if any error.
"""

MAN: dict[str, str] = {
    "ls": """\
ls [path]  (alias: dir)
  List directory. Default remote cwd; -L/--local lists host.
  Path may include DOS wildcards (* ?) — expanded on the host.
  -l / --long: type, size, name (littlefs type+size; host uses OS stat).
  Note: -l is long form; -L is local world (case-sensitive).
  Options: -l/--long, -L/--local, -h/--help
""",
    "cd": """\
cd [path]  (aliases: chdir, rcd)
  Change working directory. Default remote; -L host.
  Does not change the prompt display (use 'prompt').
  Options: -L/--local, -h/--help
""",
    "pwd": """\
pwd
  Print working directory for remote (default) or host (-L).
  Does not change the prompt.
  Options: -L/--local, -h/--help
""",
    "cp": """\
cp <src> <dst>  (alias: copy)
  Copy within one world (remote-remote or host-host with -L).
  Cross-world: use get/put. Globs expanded on host; multi-item uses
  retry/skip/abort on failure unless -y.
  Options: -L/--local, -y/-f, -h/--help
""",
    "mv": """\
mv <src> <dst>  (aliases: move, ren, rename)
  Rename/move within one world.
  Options: -L/--local, -y/-f, -h/--help
""",
    "rm": """\
rm <path...>  (aliases: del, erase)
  Delete file(s). Confirms unless -y/-f. Globs host-expanded.
  Options: -L/--local, -y/--yes/-f, -h/--help
""",
    "mkdir": """\
mkdir <path>  (alias: md)
  Create directory.
  Options: -L/--local, -h/--help
""",
    "rmdir": """\
rmdir <path>  (alias: rd)
  Remove empty directory (remote: uses remove; host: os.rmdir).
  Options: -L/--local, -y/-f, -h/--help
""",
    "cat": """\
cat <path>  (alias: type)
  Print text file to console.
  Options: -L/--local, -h/--help
""",
    "more": """\
more <path>
  Page text file (simple --more-- pager).
  Options: -L/--local, -h/--help
""",
    "get": """\
get <remote> [host]
  Copy remote → host. Dest defaults to basename in host cwd.
  Remote globs (* ?) expanded on the PC via directory list (non-recursive), e.g.
    get test/*          → each file under remote cwd/test into host cwd
    get /lfs0/test/*.log  outdir/
  Multiple sources require a host directory dest (existing dir, trailing /,
  or omitted dest). Directories in a glob match are skipped.
  If host file exists: prompt to overwrite; get -y / --force overwrites.
  World flags (-L/--remote) are syntax errors.
  Options: -y/--yes/-f/--force, -h/--help
""",
    "put": """\
put <host> [remote]
  Copy host → remote. Dest defaults to basename in remote cwd.
  Host globs (* ?) expanded on the PC (non-recursive), e.g.
    put LED_Strip_Controller_G474/*.log /lfs0/test/
  → each match as /lfs0/test/<basename>. Multiple sources require a
  directory dest (existing remote dir, trailing /, or omitted dest).
  If remote file exists: prompt to overwrite; put -y / --force overwrites.
  World flags (-L/--remote) are syntax errors.
  Options: -y/--yes/-f/--force, -h/--help
""",
    "mount": """\
mount <label>
  Mount littlefs partition by label (e.g. lfs0).
""",
    "umount": """\
umount <label>  (alias: unmount)
  Unmount littlefs partition by label.
""",
    "format": """\
format <label>
  Format littlefs partition (destructive). Confirms unless -y.
  Options: -y/-f, -h/--help
""",
    "prompt": """\
prompt local|remote|none
  Set prompt display only (does not change command default world).
  Forms: [L] path >  |  [R] path >  |  bare >
""",
    "help": """\
help [command]  (aliases: ?, man)
  With no args: short command list.
  With command: mini-manpage for that verb.
""",
    "exit": """\
exit  (alias: quit)
  Leave harness cleanly and quit the shell.
""",
    "sync": """\
sync  (alias: resync)
  Probe the remote layer and re-enter the fileops REPL if needed.
  Device fileops idle timeout is 10 minutes (returns to outer harness);
  outer harness idle is ~15 s (returns to debug menu). After a long
  pause, run sync before remote ops — or rely on auto-recover after a
  failed remote command.
  Does not change host/remote cwd. No options.
""",
    "echo": """\
echo [args...]  (alias: print)
  Write args to output, space-separated, then newline.
  Useful in scripts:  echo -- test start --
""",
}


def resolve_verb(verb: str) -> str:
    v = verb.lower()
    return ALIASES.get(v, v)


def help_for(verb: str | None) -> str:
    if not verb:
        return SHORT
    v = resolve_verb(verb)
    return MAN.get(v, f"No help for '{verb}'. Try 'help' for a list.\n")
