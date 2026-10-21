#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
VMware Ubuntu VM control helper (SSH via paramiko).

Usage:
    python vm.py exec "<shell command>"        # run a command, print stdout/stderr
    python vm.py sudo "<shell command>"        # run via sudo (password piped to sudo -S)
    python vm.py put  <local> <remote>         # upload a file
    python vm.py get  <remote> <local>         # download a file
    python vm.py shell                         # interactive shell
    python vm.py info                          # quick VM summary

Host/port/user/password come from vm_config.json next to this script.
"""
import base64
import json
import os
import sys

import paramiko

HERE = os.path.dirname(os.path.abspath(__file__))
CFG_PATH = os.path.join(HERE, "vm_config.json")


def load_cfg():
    with open(CFG_PATH, "r", encoding="utf-8") as fh:
        return json.load(fh)


def connect(cfg):
    cli = paramiko.SSHClient()
    cli.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    cli.connect(
        hostname=cfg["host"],
        port=int(cfg.get("port", 22)),
        username=cfg["user"],
        password=cfg["password"],
        timeout=15,
        banner_timeout=20,
        auth_timeout=20,
        look_for_keys=False,
        allow_agent=False,
    )
    return cli


def run(cli, cmd, timeout=300, sudo_password=None):
    """Execute cmd, return (exit_status, stdout, stderr)."""
    # Normalize CRLF: the remote shell is dash, which chokes on \r.
    cmd = cmd.replace("\r\n", "\n").replace("\r", "\n")
    stdin, stdout, stderr = cli.exec_command(cmd, timeout=timeout, get_pty=False)
    if sudo_password is not None:
        stdin.write(sudo_password + "\n")
        stdin.flush()
    out = stdout.read().decode("utf-8", "replace")
    err = stderr.read().decode("utf-8", "replace")
    rc = stdout.channel.recv_exit_status()
    return rc, out, err


def run_script(cli, body, timeout=1800):
    """Run a local script body on the remote host via `bash -s` (stdin)."""
    stdin, stdout, stderr = cli.exec_command("bash -s", timeout=timeout, get_pty=False)
    stdin.write(body)
    stdin.channel.shutdown_write()
    out = stdout.read().decode("utf-8", "replace")
    err = stderr.read().decode("utf-8", "replace")
    rc = stdout.channel.recv_exit_status()
    return rc, out, err


def emit(rc, out, err):
    if out:
        sys.stdout.write(out)
        if not out.endswith("\n"):
            sys.stdout.write("\n")
    if err:
        sys.stderr.write(err)
        if not err.endswith("\n"):
            sys.stderr.write("\n")
    return rc


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2

    cfg = load_cfg()
    action = sys.argv[1]
    cli = connect(cfg)

    try:
        if action == "exec":
            arg = sys.argv[2]
            if arg.startswith("@"):
                # Feed a local script file to the remote shell via stdin.
                # Avoids all host/remote quoting problems.
                with open(arg[1:], "r", encoding="utf-8") as fh:
                    body = fh.read().replace("\r\n", "\n").replace("\r", "\n")
                rc, out, err = run_script(cli, body)
                return emit(rc, out, err)
            rc, out, err = run(cli, arg)
            return emit(rc, out, err)

        if action == "sudo":
            arg = sys.argv[2]
            pw = cfg.get("sudo_password") or cfg["password"]
            if arg.startswith("@"):
                # Privileged run of a local script file. The body travels as
                # base64 through the command line, so stdin stays free for the
                # sudo password prompt and no quoting/heredoc issues arise.
                with open(arg[1:], "r", encoding="utf-8") as fh:
                    body = fh.read().replace("\r\n", "\n").replace("\r", "\n")
                b64 = base64.b64encode(body.encode("utf-8")).decode("ascii")
                cmd = "sudo -S -p '' bash -c " + json.dumps(
                    "echo " + b64 + " | base64 -d | bash"
                )
            else:
                cmd = "sudo -S -p '' bash -lc " + json.dumps(arg)
            rc, out, err = run(cli, cmd, sudo_password=pw)
            return emit(rc, out, err)

        if action == "put":
            sftp = cli.open_sftp()
            sftp.put(sys.argv[2], sys.argv[3])
            sftp.close()
            print("uploaded %s -> %s" % (sys.argv[2], sys.argv[3]))
            return 0

        if action == "get":
            sftp = cli.open_sftp()
            sftp.get(sys.argv[2], sys.argv[3])
            sftp.close()
            print("downloaded %s -> %s" % (sys.argv[2], sys.argv[3]))
            return 0

        if action == "shell":
            chan = cli.invoke_shell(term="xterm", width=200, height=50)
            import select
            import threading

            def reader():
                while True:
                    if chan.recv_ready():
                        sys.stdout.write(chan.recv(4096).decode("utf-8", "replace"))
                        sys.stdout.flush()
                    elif chan.exit_status_ready() and not chan.recv_ready():
                        break

            th = threading.Thread(target=reader, daemon=True)
            th.start()
            while True:
                line = sys.stdin.readline()
                if not line:
                    break
                chan.send(line)
            return 0

        if action == "info":
            cmds = [
                ("hostname", "hostname"),
                ("os", "cat /etc/os-release | head -3"),
                ("kernel", "uname -r"),
                ("arch", "uname -m"),
                ("cpu", "nproc"),
                ("mem", "free -h | head -2"),
                ("disk", "df -h / | tail -1"),
                ("ip", "hostname -I"),
                ("whoami", "whoami"),
            ]
            for label, c in cmds:
                rc, out, err = run(cli, c)
                print("%-9s: %s" % (label, out.strip() or err.strip()))
            return 0

        print("unknown action: %s" % action)
        return 2
    finally:
        cli.close()


if __name__ == "__main__":
    sys.exit(main())
