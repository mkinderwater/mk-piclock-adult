#!/usr/bin/env python3
import copy
import json
import os
import re
import shlex
import signal
import socket
import subprocess
import struct
import sys
import threading
import time

BT = "/usr/bin/bluetoothctl"
BUSCTL = "/usr/bin/busctl"
SOCKET_PATH = "/run/mk-clock-bluetooth/control.sock"
CORE_SOCKET_PATH = "/run/mk-piclock/core.sock"
IPC_MAGIC = 0x4D4B5043
IPC_VERSION = 28
IPC_OP_BLUETOOTH_STATE = 22
IPC_BT_TEXT_MAX = 160
IPC_BT_PASSKEY_MAX = 16
MAC_RE = re.compile(r"^(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$")
STOP = threading.Event()
AGENT = None
PAIR_TIMER = None
PAIRING_ACTIVE = threading.Event()
PAIRING_STATE_LOCK = threading.Lock()
PAIRING_PASSKEY = ""
CORE_SYNC_LOCK = threading.Lock()
CORE_LAST_STATE = None
CORE_LAST_SENT_AT = 0.0
MEDIA_STATE_LOCK = threading.Lock()
MEDIA_STATE = {"playing": False, "title": "", "artist": ""}
# Bluetooth display intentionally uses only the AVRCP Title field.  Keep the
# last non-empty title for each connected player because iPhone Track updates
# may briefly publish an empty Title while metadata settles.  Artist and all
# other Track fields are ignored for display simplicity.
MEDIA_METADATA_CACHE = {}
SNAPSHOT_LOCK = threading.Lock()
SNAPSHOT = {
    "ok": True,
    "available": False,
    "devices": [],
    "error": "Bluetooth controller initializing",
}
SNAPSHOT_UPDATED_AT = 0.0
REFRESH_EVENT = threading.Event()
REFRESH_LOCK = threading.Lock()
FALLBACK_REFRESH_SECONDS = 60.0
EVENT_DEBOUNCE_SECONDS = 0.35

# Pairing is the one time we intentionally trade CPU for certainty.  Normal
# operation stays event-driven, but while a pairing attempt is active we poll
# the complete BlueZ device/media snapshot aggressively.  Once a new bond is
# observed, rapid polling continues for a short settle window so trust,
# connection and service-resolution changes are captured before returning to
# the normal low-CPU path.
PAIRING_RAPID_POLL_SECONDS = 0.5
PAIRING_SETTLE_SECONDS = 12.0
PAIRING_RAPID_EVENT = threading.Event()
PAIRING_RAPID_LOCK = threading.Lock()
PAIRING_RAPID_UNTIL = 0.0
PAIRING_BASELINE_PAIRED = set()
PAIRING_BASELINE_VALID = False
PAIRING_COMPLETION_SEEN = False

# iPhone/AVRCP metadata can arrive slightly after A2DP playback becomes active.
# Keep normal operation event-driven, but briefly re-read the media snapshot
# after playback/track transitions so a late Title property is captured
# without restoring the old continuous polling load.
MEDIA_RAPID_POLL_SECONDS = 0.5
MEDIA_SETTLE_SECONDS = 5.0
MEDIA_RAPID_EVENT = threading.Event()
MEDIA_RAPID_LOCK = threading.Lock()
MEDIA_RAPID_UNTIL = 0.0


def encode_ipc_text(value, length):
    limit = max(0, length - 1)
    data = str(value or "").encode("utf-8", "replace")
    if len(data) > limit:
        data = data[:limit].decode("utf-8", "ignore").encode("utf-8")
    return data + (b"\0" * (length - len(data)))


def pairing_passkey():
    with PAIRING_STATE_LOCK:
        return PAIRING_PASSKEY


def set_pairing_passkey(value):
    global PAIRING_PASSKEY
    value = str(value or "")
    if value and not re.fullmatch(r"\d{6}", value):
        value = ""
    with PAIRING_STATE_LOCK:
        changed = value != PAIRING_PASSKEY
        PAIRING_PASSKEY = value
    if changed:
        sync_core_state(force=True)


def core_media_state(devices=None):
    if devices is not None:
        playing_device = next((item for item in devices
                               if item.get("connected") and item.get("media", {}).get("playing")), None)
        media = playing_device.get("media", {}) if playing_device else {}
        current = {
            "playing": bool(playing_device),
            "title": str(media.get("title", "")),
            "artist": str(media.get("Artist", media.get("artist", ""))),
        }
        with MEDIA_STATE_LOCK:
            MEDIA_STATE.update(current)
    else:
        with MEDIA_STATE_LOCK:
            current = dict(MEDIA_STATE)
    current["passkey"] = pairing_passkey()
    return current


def sync_core_state(devices=None, force=False):
    global CORE_LAST_STATE, CORE_LAST_SENT_AT
    with CORE_SYNC_LOCK:
        try:
            state = core_media_state(devices)
        except Exception:
            return False
        state_key = (state["playing"], state["title"], state["artist"], state["passkey"])
        now = time.monotonic()
        if not force and state_key == CORE_LAST_STATE and now - CORE_LAST_SENT_AT < 5.0:
            return True

        payload = struct.pack(
            "=BB2x160s160s16s",
            1 if state["playing"] else 0,
            1 if state["passkey"] else 0,
            encode_ipc_text(state["title"], IPC_BT_TEXT_MAX),
            encode_ipc_text(state["artist"], IPC_BT_TEXT_MAX),
            encode_ipc_text(state["passkey"], IPC_BT_PASSKEY_MAX),
        )
        header = struct.pack("=IHHI", IPC_MAGIC, IPC_VERSION,
                             IPC_OP_BLUETOOTH_STATE, len(payload))
        packet = header + payload
        try:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
            sock.settimeout(1.0)
            try:
                sock.connect(CORE_SOCKET_PATH)
                sent = sock.send(packet)
                if sent != len(packet):
                    return False
                response = sock.recv(256)
                if len(response) < 16:
                    return False
                magic, version, status, _body_len, _ctype, _reserved = struct.unpack(
                    "=IHHIHH", response[:16])
                if magic != IPC_MAGIC or version != IPC_VERSION or status != 200:
                    return False
            finally:
                sock.close()
        except OSError:
            return False

        CORE_LAST_STATE = state_key
        CORE_LAST_SENT_AT = now
        return True


def run_bt(*args, check=False, timeout=10):
    try:
        proc = subprocess.run([BT, *args], text=True, encoding="utf-8", errors="replace",
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired) as exc:
        if check:
            raise RuntimeError(str(exc))
        return ""
    output = proc.stdout or ""
    if check and proc.returncode != 0:
        raise RuntimeError(output.strip() or f"bluetoothctl {' '.join(args)} failed")
    return output


def run_bus(*args, timeout=4):
    try:
        proc = subprocess.run([BUSCTL, *args], text=True, encoding="utf-8", errors="replace",
                              stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired):
        return ""
    return proc.stdout or "" if proc.returncode == 0 else ""


def decode_busctl_string(value):
    """Decode busctl's escaped UTF-8 byte form (for example \\303\\253 -> ë)."""
    value = str(value or "")

    def decode_run(match):
        raw = bytes(int(part, 8) for part in re.findall(r"\\([0-7]{3})", match.group(0)))
        return raw.decode("utf-8", "replace")

    return re.sub(r"(?:\\[0-7]{3})+", decode_run, value)


def parse_bus_scalar(text):
    try:
        tokens = shlex.split(text)
    except ValueError:
        return ""
    return decode_busctl_string(tokens[1]) if len(tokens) >= 2 else ""


def parse_track(text):
    try:
        tokens = shlex.split(text)
    except ValueError:
        return {}
    if len(tokens) < 2 or tokens[0] != "a{sv}":
        return {}
    try:
        count = int(tokens[1])
    except ValueError:
        return {}
    result = {}
    index = 2
    for _ in range(count):
        if index + 2 >= len(tokens):
            break
        key, signature = tokens[index], tokens[index + 1]
        index += 2
        # BlueZ MediaPlayer1.Track uses strings plus integer metadata values.
        if signature in {"s", "u", "q", "t", "i", "n", "x", "y", "b"}:
            value = tokens[index]
            index += 1
        else:
            break
        if signature == "s" and key == "Title":
            result[key] = decode_busctl_string(value)
    return result


def media_player_paths():
    output = run_bus("--list", "tree", "org.bluez")
    return [line.strip() for line in output.splitlines()
            if re.search(r"/dev_[0-9A-Fa-f_]{17}/player[0-9]+$", line.strip())]


def media_status(address, players):
    device = "dev_" + address.replace(":", "_")
    path = next((item for item in players if f"/{device}/player" in item), "")
    if not path:
        # No active MediaPlayer means there is nothing current to preserve for
        # this device.  This also prevents metadata leaking between sessions.
        with MEDIA_STATE_LOCK:
            MEDIA_METADATA_CACHE.pop(address, None)
        return {"available": False, "playing": False, "status": "", "title": "", "artist": ""}

    state = parse_bus_scalar(run_bus("get-property", "org.bluez", path,
                                     "org.bluez.MediaPlayer1", "Status"))
    track = parse_track(run_bus("get-property", "org.bluez", path,
                                "org.bluez.MediaPlayer1", "Track"))
    incoming_title = str(track.get("Title", ""))

    # Title is the single Bluetooth display metadata source.  Preserve the last
    # non-empty value across partial iPhone Track updates; deliberately ignore
    # Artist so display output is stable and deterministic.
    with MEDIA_STATE_LOCK:
        cached = MEDIA_METADATA_CACHE.setdefault(address, {"title": ""})
        if incoming_title:
            cached["title"] = incoming_title
        title = cached["title"]

    return {
        "available": True,
        "playing": state == "playing",
        "status": state,
        "title": title,
        "artist": "",
    }


def prop(text, key):
    prefix = key + ":"
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith(prefix):
            return line[len(prefix):].strip()
    return ""


def yes(value):
    return value.strip().lower() == "yes"


def controller_address(show):
    match = re.search(r"^Controller\s+([0-9A-Fa-f:]{17})\b", show, re.MULTILINE)
    if match:
        return match.group(1).upper()
    value = prop(show, "Address")
    return value.upper() if MAC_RE.match(value) else ""



def read_first_text(paths):
    for path in paths:
        try:
            value = open(path, "rb").read().replace(b"\x00", b"").decode("utf-8", "replace").strip()
        except OSError:
            continue
        if value:
            return value
    return ""


def board_serial():
    value = read_first_text((
        "/sys/firmware/devicetree/base/serial-number",
        "/proc/device-tree/serial-number",
    ))
    if not value:
        try:
            with open("/proc/cpuinfo", "r", encoding="utf-8", errors="replace") as handle:
                for raw in handle:
                    if ":" not in raw:
                        continue
                    key, candidate = raw.split(":", 1)
                    if key.strip() == "Serial":
                        value = candidate.strip()
                        break
        except OSError:
            pass
    if value.lower().startswith("0x"):
        value = value[2:]
    return value


def inventory_id(serial=None):
    if serial is None:
        serial = board_serial()
    compact = "".join(ch.upper() for ch in (serial or "") if ch.isalnum())
    if len(compact) >= 8:
        suffix = compact[-8:]
        return f"MK-{suffix[:4]}-{suffix[4:]}"
    return f"MK-{compact}" if compact else ""


def advertised_name():
    return inventory_id() or "MK Clock"

def paired_devices():
    devices = []
    players = media_player_paths()
    for raw in run_bt("devices", "Paired").splitlines():
        match = re.match(r"^Device\s+([0-9A-Fa-f:]{17})\s*(.*)$", raw.strip())
        if not match or not MAC_RE.match(match.group(1)):
            continue
        address = match.group(1).upper()
        info = run_bt("info", address)
        connected = yes(prop(info, "Connected"))
        if not connected:
            with MEDIA_STATE_LOCK:
                MEDIA_METADATA_CACHE.pop(address, None)
        devices.append({
            "address": address,
            "name": prop(info, "Name") or prop(info, "Alias") or match.group(2).strip() or address,
            "connected": connected,
            "trusted": yes(prop(info, "Trusted")),
            "paired": yes(prop(info, "Paired")),
            "icon": prop(info, "Icon"),
            "media": media_status(address, players) if connected else {
                "available": False, "playing": False, "status": "", "title": "", "artist": ""
            },
        })
    return devices


def build_snapshot():
    show = run_bt("show")
    if not show or "Controller " not in show:
        return {"ok": True, "available": False, "devices": [],
                "error": "Bluetooth controller unavailable"}

    devices = paired_devices()
    # Trust newly paired devices once, at the event-driven refresh point.
    # This replaces the old once-per-second trust scan.
    for item in devices:
        if item.get("paired") and not item.get("trusted"):
            output = run_bt("trust", item["address"])
            if "Failed" not in output and "not available" not in output.lower():
                item["trusted"] = True

    return {
        "ok": True,
        "available": True,
        "address": controller_address(show),
        "name": prop(show, "Alias") or prop(show, "Name") or advertised_name(),
        "powered": yes(prop(show, "Powered")),
        "discoverable": yes(prop(show, "Discoverable")),
        "pairable": yes(prop(show, "Pairable")),
        "connected_count": sum(1 for item in devices if item["connected"]),
        "devices": devices,
    }


def refresh_snapshot():
    global SNAPSHOT, SNAPSHOT_UPDATED_AT
    # Collapse bursts of BlueZ notifications into one inventory/media read.
    if not REFRESH_LOCK.acquire(blocking=False):
        REFRESH_EVENT.set()
        return False
    try:
        snapshot = build_snapshot()
        with SNAPSHOT_LOCK:
            SNAPSHOT = snapshot
            SNAPSHOT_UPDATED_AT = time.monotonic()
        sync_core_state(snapshot.get("devices", []))
        return True
    finally:
        REFRESH_LOCK.release()


def snapshot_paired_addresses():
    with SNAPSHOT_LOCK:
        devices = copy.deepcopy(SNAPSHOT.get("devices", []))
    return {str(item.get("address", "")).upper() for item in devices
            if item.get("paired") and MAC_RE.match(str(item.get("address", "")))}


def begin_pairing_rapid_poll():
    global PAIRING_RAPID_UNTIL, PAIRING_BASELINE_PAIRED
    global PAIRING_BASELINE_VALID, PAIRING_COMPLETION_SEEN
    with PAIRING_RAPID_LOCK:
        PAIRING_RAPID_UNTIL = 0.0
        PAIRING_BASELINE_PAIRED = snapshot_paired_addresses()
        PAIRING_BASELINE_VALID = True
        PAIRING_COMPLETION_SEEN = False
        PAIRING_RAPID_EVENT.set()


def keep_pairing_rapid_poll_active():
    global PAIRING_RAPID_UNTIL
    with PAIRING_RAPID_LOCK:
        if not PAIRING_COMPLETION_SEEN:
            PAIRING_RAPID_UNTIL = 0.0
            PAIRING_RAPID_EVENT.set()


def settle_pairing_rapid_poll():
    global PAIRING_RAPID_UNTIL, PAIRING_COMPLETION_SEEN
    with PAIRING_RAPID_LOCK:
        PAIRING_COMPLETION_SEEN = True
        PAIRING_RAPID_UNTIL = time.monotonic() + PAIRING_SETTLE_SECONDS
        PAIRING_RAPID_EVENT.set()


def stop_pairing_rapid_poll():
    global PAIRING_RAPID_UNTIL, PAIRING_BASELINE_PAIRED
    global PAIRING_BASELINE_VALID, PAIRING_COMPLETION_SEEN
    with PAIRING_RAPID_LOCK:
        PAIRING_RAPID_UNTIL = 0.0
        PAIRING_BASELINE_PAIRED = set()
        PAIRING_BASELINE_VALID = False
        PAIRING_COMPLETION_SEEN = False
        PAIRING_RAPID_EVENT.clear()


def rapid_pairing_poll_worker():
    global PAIRING_RAPID_UNTIL, PAIRING_COMPLETION_SEEN
    while not STOP.is_set():
        if not PAIRING_RAPID_EVENT.wait(1.0):
            continue
        if STOP.is_set():
            break

        refresh_snapshot()
        current = snapshot_paired_addresses()

        with PAIRING_RAPID_LOCK:
            if PAIRING_BASELINE_VALID and not PAIRING_COMPLETION_SEEN:
                if current - PAIRING_BASELINE_PAIRED:
                    PAIRING_COMPLETION_SEEN = True
                    PAIRING_RAPID_UNTIL = time.monotonic() + PAIRING_SETTLE_SECONDS

            until = PAIRING_RAPID_UNTIL
            if until and time.monotonic() >= until:
                PAIRING_RAPID_EVENT.clear()
                PAIRING_RAPID_UNTIL = 0.0

        if STOP.wait(PAIRING_RAPID_POLL_SECONDS):
            break


def begin_media_rapid_poll():
    global MEDIA_RAPID_UNTIL
    with MEDIA_RAPID_LOCK:
        MEDIA_RAPID_UNTIL = max(MEDIA_RAPID_UNTIL, time.monotonic() + MEDIA_SETTLE_SECONDS)
        MEDIA_RAPID_EVENT.set()


def rapid_media_poll_worker():
    global MEDIA_RAPID_UNTIL
    while not STOP.is_set():
        if not MEDIA_RAPID_EVENT.wait(1.0):
            continue
        if STOP.is_set():
            break

        with MEDIA_RAPID_LOCK:
            until = MEDIA_RAPID_UNTIL
            if not until or time.monotonic() >= until:
                MEDIA_RAPID_UNTIL = 0.0
                MEDIA_RAPID_EVENT.clear()
                continue

        refresh_snapshot()
        if STOP.wait(MEDIA_RAPID_POLL_SECONDS):
            break


def status():
    # GET /api/v1/bluetooth is intentionally cache-only. The web GUI polls
    # this endpoint every few seconds; spawning bluetoothctl/busctl here was
    # one of the causes of sustained D-Bus CPU load.
    with SNAPSHOT_LOCK:
        result = copy.deepcopy(SNAPSHOT)
        age = time.monotonic() - SNAPSHOT_UPDATED_AT if SNAPSHOT_UPDATED_AT else None
    passkey = pairing_passkey()
    result["pairing_passkey"] = passkey
    result["pairing_passkey_active"] = bool(passkey)
    if age is not None:
        result["cache_age_ms"] = int(max(0.0, age) * 1000.0)
    return result


def initialize_adapter():
    for _ in range(20):
        show = run_bt("show")
        if "Controller " in show:
            run_bt("power", "on")
            run_bt("system-alias", advertised_name())
            run_bt("pairable", "off")
            run_bt("discoverable-timeout", "120")
            run_bt("discoverable", "off")
            return True
        if STOP.wait(1):
            return False
    return False


def agent_io_loop(proc):
    buffer = ""
    line_buffer = ""
    announced_passkey = ""
    try:
        while not STOP.is_set() and proc.poll() is None:
            chunk = proc.stdout.read(1)
            if not chunk:
                break

            if chunk in "\r\n":
                event_line = re.sub(r"\x1b\[[0-9;?]*[ -/]*[@-~]", "", line_buffer).strip()
                line_buffer = ""
                if bluetooth_event_relevant(event_line):
                    queue_refresh()
                if (
                        ("[CHG] Transport" in event_line and "State: active" in event_line) or
                        re.search(r"\[NEW\]\s+Player\b", event_line) or
                        ("[CHG] Player" in event_line and any(key in event_line for key in (
                            "Status: playing", "Track", "Title:"
                        )))
                ):
                    begin_media_rapid_poll()
                if PAIRING_ACTIVE.is_set() and (
                        "Pairing successful" in event_line or "Paired: yes" in event_line):
                    settle_pairing_rapid_poll()
            else:
                line_buffer = (line_buffer + chunk)[-2048:]

            buffer = (buffer + chunk)[-1024:]
            normalized = re.sub(r"\x1b\[[0-9;?]*[ -/]*[@-~]", "", buffer)

            if PAIRING_ACTIVE.is_set():
                match = re.search(r"Confirm passkey\s+(\d{6})", normalized, re.IGNORECASE)
                if match and match.group(1) != announced_passkey:
                    announced_passkey = match.group(1)
                    keep_pairing_rapid_poll_active()
                    set_pairing_passkey(announced_passkey)

                if "Authorize service" in normalized and pairing_passkey():
                    set_pairing_passkey("")

                terminal_markers = (
                    "Pairing successful", "Paired: yes", "Failed to pair",
                    "AuthenticationCanceled", "AuthenticationFailed",
                    "AuthenticationRejected", "Canceled", "Rejected",
                )
                if any(marker in normalized for marker in terminal_markers):
                    set_pairing_passkey("")
                    announced_passkey = ""

                if ("Confirm passkey" in normalized or "Authorize service" in normalized) and \
                        "(yes/no):" in normalized:
                    try:
                        proc.stdin.write("yes\n")
                        proc.stdin.flush()
                    except (OSError, BrokenPipeError):
                        break
                    buffer = ""
            elif pairing_passkey():
                set_pairing_passkey("")
                announced_passkey = ""
    except (OSError, ValueError):
        pass
    finally:
        set_pairing_passkey("")


def start_agent():
    global AGENT
    try:
        AGENT = subprocess.Popen([BT], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                                 errors="replace", bufsize=1)
        AGENT.stdin.write("agent DisplayYesNo\n")
        AGENT.stdin.write("default-agent\n")
        AGENT.stdin.flush()
        threading.Thread(target=agent_io_loop, args=(AGENT,), daemon=True).start()
    except (OSError, BrokenPipeError):
        AGENT = None

def keep_agent_alive():
    global AGENT
    while not STOP.wait(5):
        if AGENT is None or AGENT.poll() is not None:
            start_agent()


def queue_refresh():
    REFRESH_EVENT.set()


def bluetooth_event_relevant(line):
    line = str(line or "").strip()
    if not line:
        return False

    if re.search(r"\[(?:NEW|DEL)\]\s+(?:Device|Player|Transport)\b", line):
        return True

    if "[CHG] Device" in line and any(key in line for key in (
            "Connected:", "Paired:", "Trusted:", "Name:", "Alias:",
            "ServicesResolved:")):
        return True

    if "[CHG] Player" in line and any(key in line for key in (
            "Status:", "Track", "Title:", "Name:")):
        return True

    if "[CHG] Transport" in line and "State:" in line:
        return True

    if "[CHG] Controller" in line and any(key in line for key in (
            "Powered:", "Discoverable:", "Pairable:", "Alias:")):
        return True

    return False


def refresh_worker():
    # BlueZ normally supplies all relevant changes through the persistent
    # bluetoothctl agent. A 60-second fallback refresh is retained so a lost
    # notification cannot leave the cache stale indefinitely.
    while not STOP.is_set():
        triggered = REFRESH_EVENT.wait(FALLBACK_REFRESH_SECONDS)
        if STOP.is_set():
            break
        if triggered:
            REFRESH_EVENT.clear()
            if STOP.wait(EVENT_DEBOUNCE_SECONDS):
                break
            # Coalesce all notifications that arrived during the debounce window.
            REFRESH_EVENT.clear()
        refresh_snapshot()


def validate_mac(value):
    if not MAC_RE.match(value or ""):
        raise RuntimeError("Invalid Bluetooth address")
    return value.upper()


def process(command):
    parts = command.strip().split()
    if not parts or parts[0] == "status":
        return status()
    if parts[0] == "pairing" and len(parts) == 2:
        global PAIR_TIMER
        enabled = parts[1].lower() in {"1", "on", "true"}
        run_bt("power", "on", check=True)
        run_bt("system-alias", advertised_name(), check=True)
        run_bt("discoverable-timeout", "120", check=True)
        if enabled:
            PAIRING_ACTIVE.set()
            set_pairing_passkey("")
        else:
            PAIRING_ACTIVE.clear()
            set_pairing_passkey("")
            stop_pairing_rapid_poll()
        run_bt("pairable", "on" if enabled else "off", check=True)
        run_bt("discoverable", "on" if enabled else "off", check=True)
        if PAIR_TIMER is not None:
            PAIR_TIMER.cancel()
            PAIR_TIMER = None
        if enabled:
            def expire_pairing():
                PAIRING_ACTIVE.clear()
                set_pairing_passkey("")
                stop_pairing_rapid_poll()
                run_bt("discoverable", "off")
                run_bt("pairable", "off")
                queue_refresh()
            PAIR_TIMER = threading.Timer(125, expire_pairing)
            PAIR_TIMER.daemon = True
            PAIR_TIMER.start()
        refresh_snapshot()
        if enabled:
            begin_pairing_rapid_poll()
        return status()
    if parts[0] == "device" and len(parts) == 3:
        action, address = parts[1], validate_mac(parts[2])
        if action == "connect":
            run_bt("trust", address)
            run_bt("connect", address, check=True, timeout=20)
        elif action == "disconnect":
            run_bt("disconnect", address, check=True)
        elif action == "forget":
            run_bt("remove", address, check=True)
        elif action == "trust":
            run_bt("trust", address, check=True)
        else:
            raise RuntimeError("Unknown Bluetooth device action")
        # BlueZ emits the authoritative Connected/Paired/Trusted events while
        # the action runs.  Do not hold the GUI request open for another full
        # bluetoothctl/busctl inventory scan after the action succeeds; queue
        # that expensive refresh through the normal event-driven worker.
        queue_refresh()
        return status()
    raise RuntimeError("Unknown Bluetooth control command")


def client(conn):
    try:
        data = b""
        while b"\n" not in data and len(data) < 1024:
            chunk = conn.recv(1024 - len(data))
            if not chunk:
                break
            data += chunk
        command = data.decode("utf-8", "replace").split("\n", 1)[0]
        try:
            result = process(command)
        except Exception as exc:
            result = {"ok": False, "error": str(exc)}
        payload = json.dumps(result, ensure_ascii=False, separators=(",", ":")).encode("utf-8") + b"\n"
        conn.sendall(payload)
    finally:
        conn.close()


def stop(_sig=None, _frame=None):
    global PAIR_TIMER
    STOP.set()
    PAIRING_ACTIVE.clear()
    set_pairing_passkey("")
    stop_pairing_rapid_poll()
    if PAIR_TIMER is not None:
        PAIR_TIMER.cancel()
        PAIR_TIMER = None
    try:
        if AGENT is not None:
            AGENT.terminate()
    except OSError:
        pass


def main():
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    initialize_adapter()
    refresh_snapshot()
    start_agent()
    threading.Thread(target=keep_agent_alive, daemon=True).start()
    threading.Thread(target=refresh_worker, daemon=True).start()
    threading.Thread(target=rapid_pairing_poll_worker, daemon=True).start()
    threading.Thread(target=rapid_media_poll_worker, daemon=True).start()
    sync_core_state(force=True)

    os.makedirs(os.path.dirname(SOCKET_PATH), mode=0o770, exist_ok=True)
    try:
        os.unlink(SOCKET_PATH)
    except FileNotFoundError:
        pass
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(SOCKET_PATH)
    os.chmod(SOCKET_PATH, 0o660)
    server.listen(8)
    server.settimeout(1)
    try:
        while not STOP.is_set():
            try:
                conn, _ = server.accept()
            except socket.timeout:
                continue
            threading.Thread(target=client, args=(conn,), daemon=True).start()
    finally:
        server.close()
        try:
            os.unlink(SOCKET_PATH)
        except FileNotFoundError:
            pass
        stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
