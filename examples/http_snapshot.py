"""One current snapshot; Python 3.10+, standard library only. Does not start the host."""
import json
import sys
from datetime import datetime, timezone
from urllib.error import HTTPError, URLError
from urllib.request import urlopen


def get_json(url):
    try:
        with urlopen(url, timeout=3) as response:
            return response.status, json.load(response)
    except HTTPError as error:
        with error:
            return error.code, json.load(error)


def main(base_url="http://127.0.0.1:27311"):
    base_url = base_url.rstrip("/")
    try:
        _, health = get_json(base_url + "/v1/health")
        if health.get("status") != "playing":
            print("Unavailable:", health.get("status"), health.get("error"), file=sys.stderr)
            return 1
        status, snapshot = get_json(base_url + "/v1/snapshot")
        if status != 200:  # A 503 body is health, not a snapshot.
            print("Unavailable:", status, snapshot.get("status"), file=sys.stderr)
            return 1
        if str(snapshot.get("schemaVersion", "")).split(".")[0] != "1":
            raise ValueError("Unsupported snapshot schema major version")
        player = snapshot.get("player")
        camera = snapshot.get("camera")
        capabilities = snapshot.get("capabilities", [])
        if (snapshot.get("game", {}).get("state") != "playing" or not player or not camera
                or not {"player.position", "camera.transform"}.issubset(capabilities)):
            print("Unavailable snapshot", file=sys.stderr)
            return 1
        captured = datetime.fromisoformat(snapshot["capturedAt"].replace("Z", "+00:00"))
        age = (datetime.now(timezone.utc) - captured).total_seconds()
        timeout = max(1.5, 3.0 / max(1, health["sampleRateHz"]))
        if abs(age) > timeout:
            print("Stale snapshot or clock mismatch", file=sys.stderr)
            return 1
        orientation = player.get("orientation") if "player.orientation" in capabilities else None
        print(json.dumps({
            "sequence": snapshot["sequence"],
            "playerPosition": player["position"],
            "playerHeadingDegrees": orientation.get("headingDegrees") if orientation else None,
            "cameraForward": camera["forward"],
        }, indent=2))
        return 0
    except (URLError, TimeoutError, OSError, ValueError, KeyError, TypeError) as error:
        print("Telemetry unavailable:", error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:27311"))
