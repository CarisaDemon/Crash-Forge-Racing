"""Validate the exact JSON manifest read by Crash Forge Launcher/Forge Hub.

Only public, deliberate data belongs in this repository. No secret tokens or
local filesystem paths should appear here.
"""
import json
import re
import sys
from datetime import date
from pathlib import Path
from urllib.parse import urlsplit

TYPES = {"Character", "Kart", "Wheels", "Track"}
MAP_KINDS = {"Race Track", "Battle Arena", "Hub"}
RACER_CLASSES = {"Balanced", "Speed", "Acceleration", "Turning"}
KART_DRIVES = {"Wheeled", "Hovercraft"}
WHEEL_SETUPS = {"Modular", "Included"}
ID = re.compile(r"^[a-z0-9][a-z0-9_-]{0,63}$")
SHA256 = re.compile(r"^[0-9a-fA-F]{64}$")
errors = []


def report(where, what):
    errors.append(f"{where}: {what}")


def nonempty(value, where, max_len=500):
    if not isinstance(value, str) or not 0 < len(value.strip()) <= max_len:
        report(where, f"must be a nonempty string up to {max_len} characters")
        return False
    return True


def checked_url(value, where, required=True):
    if (value is None or value == "") and not required:
        return
    if not isinstance(value, str):
        report(where, "must be an HTTPS URL")
        return
    try:
        u = urlsplit(value)
        if u.scheme != "https" or not u.hostname or u.username or u.password or u.port not in (None, 443):
            report(where, "must be HTTPS with no username/password and no custom port")
    except ValueError:
        report(where, "invalid HTTPS URL")


def valid_day(value, where):
    if not isinstance(value, str):
        report(where, "date must be ISO 8601 YYYY-MM-DD")
        return
    try:
        date.fromisoformat(value)
    except ValueError:
        report(where, "date must be ISO 8601 YYYY-MM-DD")


def validate(path):
    try:
        data = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError) as exc:
        report("catalog", f"cannot read JSON: {exc}")
        return
    if not isinstance(data, dict):
        report("catalog", "top-level JSON must be an object")
        return
    if data.get("schema_version") != 1 or isinstance(data.get("schema_version"), bool):
        report("schema_version", "must be exactly 1")
    valid_day(data.get("updated_at"), "updated_at")
    game = data.get("game", {})
    if not isinstance(game, dict):
        report("game", "must be an object (empty when no official build is published)")
    elif game:
        nonempty(game.get("version"), "game.version", 32)
        if game.get("release_url") is not None:
            checked_url(game["release_url"], "game.release_url")
    news = data.get("news")
    if not isinstance(news, list):
        report("news", "must be a list")
    else:
        for i, item in enumerate(news):
            where = f"news[{i}]"
            if not isinstance(item, dict):
                report(where, "must be an object")
                continue
            nonempty(item.get("title"), where + ".title", 90)
            nonempty(item.get("category"), where + ".category", 35)
            valid_day(item.get("date"), where + ".date")
            if item.get("url"):
                checked_url(item["url"], where + ".url")
            if item.get("description") is not None and not isinstance(item["description"], str):
                report(where + ".description", "must be a string")
    mods = data.get("mods")
    ids = set()
    if not isinstance(mods, list):
        report("mods", "must be a list")
        return
    for i, item in enumerate(mods):
        where = f"mods[{i}]"
        if not isinstance(item, dict):
            report(where, "must be an object")
            continue
        ident = item.get("id")
        if not isinstance(ident, str) or not ID.fullmatch(ident):
            report(where + ".id", "must be a unique lowercase slug (a-z, 0-9, -, _)")
        elif ident in ids:
            report(where + ".id", "duplicate id")
        else:
            ids.add(ident)
        nonempty(item.get("title"), where + ".title", 100)
        nonempty(item.get("author"), where + ".author", 75)
        nonempty(item.get("version"), where + ".version", 28)
        handle = item.get("author_github_login")
        github_id = item.get("author_github_id")
        if (handle is None) != (github_id is None):
            report(where + ".author_github_login", "GitHub login and numeric GitHub id must be provided together")
        if handle is not None and (
            not isinstance(handle, str) or
            not re.fullmatch(r"[A-Za-z0-9](?:[A-Za-z0-9-]{0,37}[A-Za-z0-9])?", handle)
        ):
            report(where + ".author_github_login", "must be a valid GitHub account login")
        if github_id is not None and (
            isinstance(github_id, bool) or not isinstance(github_id, int) or github_id <= 0
        ):
            report(where + ".author_github_id", "must be a positive integer from the real GitHub profile")
        category = item.get("type")
        if category not in TYPES:
            report(where + ".type", "must be Character, Kart, Wheels or Track")
        kinds = {key: item.get(key) for key in
                 ("map_kind", "racer_class", "kart_drive", "wheel_setup")}
        # Public releases must clearly describe their relevant subtype.
        # We do not accept invented numeric character statistics or a Custom class.
        required_fields = {
            "Track": ("map_kind",),
            "Character": ("racer_class",),
            "Kart": ("kart_drive",),
            "Wheels": (),
        }.get(category, ())
        for key in required_fields:
            if not kinds[key]:
                report(where + "." + key, "required for category " + str(category))
        if category == "Track" and kinds["map_kind"] not in MAP_KINDS:
            report(where + ".map_kind", "must be Race Track, Battle Arena or Hub")
        if category == "Character" and kinds["racer_class"] not in RACER_CLASSES:
            report(where + ".racer_class", "must be a vanilla CTR stats class")
        if category == "Kart":
            if kinds["kart_drive"] not in KART_DRIVES:
                report(where + ".kart_drive", "must be Wheeled or Hovercraft")
            elif kinds["kart_drive"] == "Wheeled" and kinds["wheel_setup"] not in WHEEL_SETUPS:
                report(where + ".wheel_setup", "wheeled karts need Modular or Included wheels")
            elif kinds["kart_drive"] == "Hovercraft" and kinds["wheel_setup"] is not None:
                report(where + ".wheel_setup", "hovercrafts have no wheels")
        allowed_fields = {
            "Track": {"map_kind"},
            "Character": {"racer_class"},
            "Kart": {"kart_drive", "wheel_setup"},
            "Wheels": set(),
        }.get(category, set())
        for key, value in kinds.items():
            if key not in allowed_fields and value is not None:
                report(where + "." + key, "not applicable to this mod category")
        for field in ("stat_speed", "stat_acceleration", "stat_turning"):
            if item.get(field) is not None:
                report(where + "." + field, "custom physics statistics are not supported")
        if item.get("compatibility") != "cfr":
            report(where + ".compatibility", "must be cfr")
        valid_day(item.get("created_at"), where + ".created_at")
        checked_url(item.get("download_url"), where + ".download_url")
        checked_url(item.get("page_url"), where + ".page_url", required=False)
        sha = item.get("sha256")
        if not isinstance(sha, str) or not SHA256.fullmatch(sha):
            report(where + ".sha256", "must be 64 hexadecimal characters")
        if item.get("description") is not None and not isinstance(item["description"], str):
            report(where + ".description", "must be a string")
        for key in ("downloads", "likes"):
            if key in item and (isinstance(item[key], bool) or
                                not isinstance(item[key], int) or item[key] < 0):
                report(where + "." + key, "must be a real nonnegative integer, or omitted")
    print(f"Checked: {len(mods)} mods, {len(news) if isinstance(news, list) else 0} news items.")
    if errors:
        for error in errors:
            print("ERROR:", error)
        print("Validation FAILED.")
    else:
        print("Validation PASS. No invented statistics are required.")


if __name__ == "__main__":
    source = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "docs" / "catalog.json"
    validate(source)
    sys.exit(1 if errors else 0)
