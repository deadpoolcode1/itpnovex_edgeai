#!/usr/bin/env python3
"""Put the modem's SDVR application back, and stop it going missing again.

    python3 scopus/modem_restore.py                  # repair, then arm the self-heal
    python3 scopus/modem_restore.py --check          # say what is wrong, change nothing
    python3 scopus/modem_restore.py --bundle <file>  # a specific build

Why this exists (ScopusQA #9). Legato counts boots in /legato/bootCount and
only clears the count once the framework has stayed up for 60 s. Power-cycling
the unit a few times in quick succession — "disconnect it and reconnect it" —
never clears it, so Legato concludes the system is in a reboot loop and
reinstalls the *factory* one:

    start.c CheckAndInstallCurrentSystem() | A good system has entered a
    reboot loop -- reinstalling from golden.

sdvrApp is installed on top of the factory system, not built into it, so it
goes too. The unit then answers every AT+SDVR... with a login prompt, because
with no app to claim ttyHSL1 through le_port, getty keeps the port. Nothing
about that looks like "the application was uninstalled", which is why it has
been diagnosed from scratch three times.

Marking the system good does not help: the guard fires on good systems by
design. So this installs a boot hook that puts the app back by itself, in
/etc (an overlay on /mnt/flash/ufs/etc) with the bundle in /mnt/flash — both
outside /legato, the only thing the factory reinstall replaces.
"""
import argparse
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(HERE, "lib"))

from devices import ModemSsh                                  # noqa: E402

G, R, Y, Z = "\033[92m", "\033[91m", "\033[93m", "\033[0m"

TARGET_DIR = "/mnt/flash/scopus"
TARGET_BUNDLE = f"{TARGET_DIR}/sdvrApp.update"
INIT_SCRIPT = "/etc/init.d/scopus-sdvr-restore"
STALE_RC_LINK = "/etc/rcS.d/S99scopus-sdvr-restore"

# Where the hook is called from, and the line that calls it. Not a drop-in
# link in /etc/rcS.d: rcS expands `for s in /etc/rcS.d/S*` before
# S07mount_unionfs has mounted the overlay that holds anything we add to /etc,
# so a new link there is invisible for the entire boot. startlegato.sh is in
# the read-only lower layer, so it is in that list already; editing it copies
# it up and the copy is what runs.
ANCHOR = "/etc/init.d/startlegato.sh"
ANCHOR_AFTER = "        test -x $LEGATO_START && $LEGATO_START"
HOOK_CALL = ("        test -x /etc/init.d/scopus-sdvr-restore && "
             "/etc/init.d/scopus-sdvr-restore start")
APP = "/legato/systems/current/bin/app"
UPDATE = "/legato/systems/current/bin/update"

# Shipped next to the modem sources; this file is the on-target boot hook.
HOOK_SRC = os.path.join(
    os.path.dirname(REPO), "V20_SDVR", "sdvr-app", "tools", "scopus-sdvr-restore")


def say(ok, name, detail=""):
    mark = f"{G}ok{Z}" if ok else f"{R}!!{Z}"
    print(f"  [{mark}] {name}" + (f"  —  {detail}" if detail else ""))


def find_bundle():
    """Newest locally built sdvrApp bundle.

    The bench has no toolchain, so these are always built on a workstation and
    copied over — see V20_SDVR/CLAUDE.md.
    """
    pats = [
        os.path.join(os.path.dirname(REPO), "V20_SDVR",
                     "_build_sdvr*", "wp76xx", "app", "sdvrApp",
                     "sdvrApp.wp76xx.update"),
        os.path.join(REPO, "scopus", "*.update"),
        os.path.expanduser("~/sdvrApp*.update"),
    ]
    found = []
    for p in pats:
        found.extend(glob.glob(p))
    return max(found, key=os.path.getmtime) if found else None


def diagnose(m):
    """What state is the modem in? Returns (app_present, note)."""
    rc, idx, _ = m.run(f"cat /legato/systems/current/index")
    rc2, props, _ = m.run("cat /legato/systems/current/info.properties")
    rc3, status, _ = m.run(f"{APP} status")

    present = "sdvrApp" in status
    factory = "system.md5=modified" not in props
    idx = idx.strip()

    say(True, "Modem answers over Ethernet", "192.168.2.2")
    say(present, "sdvrApp installed",
        "running" if "[running] sdvrApp" in status
        else ("installed but not running" if present else "MISSING"))
    say(not factory or present, "Legato system",
        f"index {idx}, "
        + ("factory (this is the reboot-loop reset)" if factory
           else "carries our app"))

    if not present:
        rc4, who, _ = m.run("ps w | grep -E 'getty|login' | grep -v grep")
        if "ttyHSL1" in who:
            say(False, "ttyHSL1", "held by getty — AT+SDVR gets a login prompt")
    return present, idx


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true",
                    help="report only; change nothing")
    ap.add_argument("--bundle", help="sdvrApp .update to install")
    args = ap.parse_args()

    print("Scopus modem restore\n")
    m = ModemSsh()
    if not m.reachable():
        print(f"{R}The modem does not answer on 192.168.2.2.{Z}")
        print("Check the Ethernet cable and that the modem is powered, then")
        print("run scopus/preflight.py.")
        return 1

    present, idx = diagnose(m)

    if args.check:
        print()
        print("Nothing changed (--check)." if present else
              f"{Y}sdvrApp is missing. Re-run without --check to put it back.{Z}")
        return 0 if present else 1

    bundle = args.bundle or find_bundle()
    if not bundle or not os.path.exists(bundle):
        print(f"\n{R}No sdvrApp .update found to install.{Z}")
        print("Build it on a workstation (V20_SDVR, see its CLAUDE.md) and pass")
        print("it with --bundle.")
        return 1
    print(f"\n  bundle: {bundle} ({os.path.getsize(bundle)} bytes)")

    # ── repair, if it is broken ────────────────────────────────────────
    if not present:
        print("\nReinstalling sdvrApp…")
        with open(bundle, "rb") as f:
            p = subprocess.run(
                ["sshpass", "-p", m.password, "ssh", *ModemSsh.OPTS,
                 f"root@{m.ip}", UPDATE],
                stdin=f, capture_output=True, text=True, timeout=600)
        if p.returncode != 0 or "SUCCESS" not in p.stdout + p.stderr:
            print(f"{R}install failed{Z}\n{p.stdout[-500:]}{p.stderr[-500:]}")
            return 1
        say(True, "installed", "SUCCESS")

        # Install → wait → mark good, as one step: a system left on probation
        # is silently rolled back by the next reboot.
        m.run(f"for i in $(seq 1 24); do {APP} status | grep -q sdvrApp && break;"
              f" sleep 5; done", timeout=180)
        rc, out, _ = m.run(f"{UPDATE} --mark-good", timeout=60)
        say("Good" in out, "marked good", out.strip() or "-")

        # le_port only takes ttyHSL1 off getty after one stop/start.
        m.run(f"{APP} stop sdvrApp", timeout=60)
        m.run(f"sleep 3; {APP} start sdvrApp", timeout=60)
        rc, out, _ = m.run(f"{APP} status | grep sdvr")
        say("[running]" in out, "sdvrApp", out.strip() or "-")

    # ── arm the self-heal, whether or not it was broken ────────────────
    print("\nArming the boot-time self-heal…")
    if not os.path.exists(HOOK_SRC):
        print(f"{R}missing {HOOK_SRC}{Z}")
        return 1

    m.run(f"mkdir -p {TARGET_DIR}")
    for src, dst in ((bundle, TARGET_BUNDLE), (HOOK_SRC, INIT_SCRIPT)):
        with open(src, "rb") as f:
            p = subprocess.run(
                ["sshpass", "-p", m.password, "ssh", *ModemSsh.OPTS,
                 f"root@{m.ip}", f"cat > {dst}"],
                stdin=f, capture_output=True, text=True, timeout=300)
        say(p.returncode == 0, f"copied {os.path.basename(src)}", dst)

    m.run(f"chmod +x {INIT_SCRIPT}")

    # A link dropped in /etc/rcS.d never runs (see ANCHOR above); clear one up
    # if an earlier attempt left it there, so it cannot mislead the next reader.
    m.run(f"rm -f {STALE_RC_LINK}")

    rc, anchor, _ = m.run(f"cat {ANCHOR}")
    if rc != 0 or not anchor.strip():
        print(f"{R}cannot read {ANCHOR}{Z}")
        return 1

    if "scopus-sdvr-restore" in anchor:
        say(True, "boot hook armed", f"already called from {ANCHOR}")
    elif ANCHOR_AFTER not in anchor:
        say(False, "boot hook", f"no anchor line in {ANCHOR} — add by hand:")
        print(f"         {Y}{HOOK_CALL.strip()}{Z}")
        return 1
    else:
        patched = anchor.replace(ANCHOR_AFTER,
                                 ANCHOR_AFTER + "\n" + HOOK_CALL, 1)
        p = subprocess.run(
            ["sshpass", "-p", m.password, "ssh", *ModemSsh.OPTS,
             f"root@{m.ip}", f"cat > {ANCHOR}"],
            input=patched, capture_output=True, text=True, timeout=60)
        # This file starts Legato. Prove it still parses before walking away.
        rc, out, err = m.run(f"sh -n {ANCHOR} && grep -c scopus-sdvr-restore {ANCHOR}")
        ok = p.returncode == 0 and rc == 0 and out.strip() == "1"
        say(ok, "boot hook armed", f"called from {ANCHOR}"
            if ok else f"FAILED — check {ANCHOR} by hand ({err.strip()})")
        if not ok:
            return 1

    print(f"\n{G}Done.{Z} The modem now puts sdvrApp back by itself after a")
    print("factory reset. Confirm the bench with:  python3 scopus/preflight.py")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
