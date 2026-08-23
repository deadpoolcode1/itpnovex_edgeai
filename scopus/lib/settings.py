"""Bench settings — one place for every address, port and password.

ScopusQA #11 asked that no IPs, ports, usernames or credentials be committed,
and that anything still needed for development live in a separate file that
ships as a template rather than as real data. This is that file's reader.

    scopus/bench.ini.template   committed, placeholders only
    scopus/bench.ini            NOT committed (.gitignore), the real values

Resolution order for every value, first hit wins:

    1. an environment variable  — SERVER_HOST, MODEM_PASSWORD, …
    2. scopus/bench.ini
    3. scopus/bench.ini.template

Falling back to the template rather than to a hardcoded default is the point:
the template's values are deliberately wrong, so a missing bench.ini surfaces
as one clear message naming the setting and the file, instead of as a tool
quietly trying to reach 203.0.113.10 and timing out.

Two settings keep real values in the committed template, and the template says
why: the modem's ECM-link addresses 192.168.2.2 / .3. They are fixed properties
of the Sierra WP76 interface, the same on every unit, and treating them as
secrets would mean every reader of the repo has to be told a constant.

Usage:

    from settings import S
    ip = S.get("modem", "ip")                  # str
    p  = S.getint("server", "upload_port")     # int
    pw = S.require("modem", "password")        # raises if still CHANGEME
"""
import configparser
import os
import sys

HERE     = os.path.dirname(os.path.abspath(__file__))
SCOPUS   = os.path.dirname(HERE)
INI      = os.path.join(SCOPUS, "bench.ini")
TEMPLATE = os.path.join(SCOPUS, "bench.ini.template")

# Values that mean "nobody has filled this in".
PLACEHOLDERS = {"CHANGEME", "203.0.113.10", ""}


class _Settings:
    def __init__(self):
        self._cp = configparser.ConfigParser()
        # Template first so it supplies defaults, then bench.ini over the top.
        # A bench.ini that sets only the two things this site changes is a
        # perfectly good bench.ini.
        read = self._cp.read([TEMPLATE, INI])
        if TEMPLATE not in read:
            raise RuntimeError(f"missing {TEMPLATE} — the repository is incomplete")
        self.have_ini = INI in read

    # ── plain lookups ───────────────────────────────────────────────────
    def get(self, section, key, default=None):
        env = os.environ.get(key.upper())
        if env not in (None, ""):
            return env
        try:
            return self._cp.get(section, key)
        except (configparser.NoSectionError, configparser.NoOptionError):
            return default

    def getint(self, section, key, default=None):
        v = self.get(section, key, None)
        if v is None:
            return default
        try:
            return int(str(v).strip())
        except ValueError:
            raise SystemExit(self._bad(section, key, f"{v!r} is not a number"))

    def getpath(self, section, key, default=None):
        v = self.get(section, key, default)
        return os.path.expanduser(v) if v else v

    # ── a value the caller cannot proceed without ───────────────────────
    def require(self, section, key):
        v = self.get(section, key)
        if v is None or str(v).strip() in PLACEHOLDERS:
            raise SystemExit(self._bad(section, key, "is still the placeholder"))
        return v

    def _bad(self, section, key, why):
        lines = [
            "",
            f"Bench setting [{section}] {key} {why}.",
            "",
        ]
        if not self.have_ini:
            lines += [
                f"There is no {os.path.relpath(INI)} on this machine. Create one:",
                "",
                f"    cp {os.path.relpath(TEMPLATE)} {os.path.relpath(INI)}",
                f"    $EDITOR {os.path.relpath(INI)}",
                "",
                "It is not tracked by git on purpose — it holds this site's",
                "addresses and passwords. The template beside it is committed",
                "with placeholder values only.",
            ]
        else:
            lines += [
                f"Set it in {os.path.relpath(INI)}, or pass it for one run as",
                f"the environment variable {key.upper()}.",
            ]
        lines.append("")
        return "\n".join(lines)


S = _Settings()


def summary() -> str:
    """One line for a preflight/report header. Never prints a password."""
    src = "bench.ini" if S.have_ini else "bench.ini.template (NOT configured)"
    return (f"settings from {src}: modem={S.get('modem','ip')} "
            f"server={S.get('server','host')}:{S.get('server','upload_port')}")


if __name__ == "__main__":
    print(summary())
    for sec in ("modem", "server", "test_server", "bench"):
        for k, v in S._cp.items(sec):
            shown = "***" if "password" in k else v
            print(f"  [{sec}] {k} = {shown}")
    sys.exit(0)
