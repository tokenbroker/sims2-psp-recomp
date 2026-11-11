"""Independent launcher-page settings for the two supported disc regions."""
from copy import deepcopy


def add_europe_settings(manifest):
    if any(item.get("id") == "europe" for item in manifest.get("config", [])):
        return manifest
    main = manifest["config"][0]
    europe = deepcopy(main)
    europe["id"] = "europe"
    for place in europe["file"]:
        place["path"] = place["path"].replace("settings.ini", "settings-europe.ini")
    europe.setdefault("new", {}).update({"host/system_language": "1", "host/engine_fps": "0"})
    manifest["config"].append(europe)
    settings = manifest.get("settings", [])
    extras = []
    for setting in settings:
        regional = deepcopy(setting)
        regional["id"] = "europe_" + regional["id"]
        regional["in"] = "europe"
        regional["label"] = "Europe: " + regional["label"]
        extras.append(regional)
        setting["label"] = "USA: " + setting["label"]
    extras.insert(0, europe_language_setting())
    manifest["settings"] = settings + extras
    return manifest


def europe_language_setting(config_id="europe"):
    return {
        "id": config_id + "_language", "in": config_id, "label": "Europe: Game language",
        "help": "Restart the game after changing its language.",
        "choices": [{"label": label, "set": {"host/system_language": value}}
                    for label, value in [("English", "1"), ("French", "2"), ("German", "4"),
                                         ("Spanish", "3"), ("Italian", "5")]],
        "defaults": {"host/system_language": "1"},
    }
