#!/usr/bin/env python3
"""Build draft CoT fixtures for the four AR message kinds and measure every candidate wire encoding.

Usage: python tools/cot_fixtures.py <takproto_dir> [out_prefix]
  takproto_dir  the TAK Protocol .proto files (takmessage.proto, cotevent.proto, detail.proto, ...) from the public
                ATAK-CIV repository, directory takproto/. They are read, never copied into this tree.
  out_prefix    default "cot": writes cot_sizes.md, cot_sizes.csv and cot_fixtures/*.xml

The profile (NET-003, "protocol") requires every exchange to be a CoT event, XML 2.0 first, and lists the identity,
timing, spatial and transaction fields an AR event must carry. It does not supply the schema or type catalog
("provide selected actual profile before encoded-wire qualification"), so the <ar> detail elements below are a DRAFT
schema written for this measurement. Every field the profile names is present; values are synthetic but full width.

Two field styles:
  as_specified  profile field names, UUID identifiers, ISO 8601 microsecond timestamps, XML declaration
  lean          short attribute names, 8-hex identifiers, integer microsecond times, no XML declaration
Five encodings:
  xml           CoT XML 2.0 as UTF-8, no insignificant whitespace (TAK Protocol version 0)
  xml_deflate   raw DEFLATE of the XML, no dictionary
  xml_dict      raw DEFLATE with a preset dictionary built at session join from a same-kind message
  tak_v1        TAK Protocol version 1 mesh message: 0xBF 0x01 0xBF + protobuf TakMessage; the AR detail rides as the
                xmlDetail string, because version 1 lets a client add only fields it may ignore
  ar_binary     the same protobuf with the AR detail as a binary message (field 8 of Detail): a protocol change that
                needs a new TAK Protocol version, shown to bound what binary encoding can buy
Charged cost adds UDP/IPv4 (28 B) and an assumed authentication overhead (28 B: 12 B nonce + 16 B tag); the PHY and
MAC are not selected, so their overhead is not included.
"""
import csv
import os
import re
import struct
import subprocess
import sys
import tempfile
import uuid
import xml.etree.ElementTree as ET
import zlib
from datetime import datetime, timedelta, timezone

PROTO_DIR = os.path.abspath(sys.argv[1])
PREFIX = sys.argv[2] if len(sys.argv) > 2 else "cot"
UDP_IP_BYTES = 28
AUTH_BYTES = 28

# ---------------------------------------------------------------------------------------------------------------
# Protobuf code: compile the upstream TAK protos, plus a copy whose Detail gains field 8 (binary AR detail).
build = tempfile.mkdtemp(prefix="takproto_")
v1_dir = os.path.join(build, "v1")
v2_dir = os.path.join(build, "v2")
os.makedirs(v1_dir)
os.makedirs(v2_dir)
protos = [f for f in os.listdir(PROTO_DIR) if f.endswith(".proto")]
for name in protos:
    text = open(os.path.join(PROTO_DIR, name)).read()
    open(os.path.join(v1_dir, name), "w").write(text)
    if name == "detail.proto":
        text = text.replace('import "track.proto";', 'import "track.proto";\nimport "ardetail.proto";')
        text = re.sub(r"(Track track = 7;)", r"\1\n    ArDetail ar = 8;", text)
    # Distinct file names and package, so both versions can load into one descriptor pool.
    text = re.sub(r'import "([a-z]+)\.proto";', r'import "v2x_\1.proto";', text)
    open(os.path.join(v2_dir, "v2x_" + name), "w").write(text.replace("package atakmap.commoncommo.protobuf.v1;",
                                                                    "package atakmap.commoncommo.protobuf.v2x;"))
open(os.path.join(v2_dir, "v2x_ardetail.proto"), "w").write('''syntax = "proto3";
package atakmap.commoncommo.protobuf.v2x;
// Draft binary AR detail: every field the profile lists, at its natural width.
message ArDetail {
   bytes  session_id = 1;  bytes origin_owner_id = 2;  bytes sender_id = 3;  bytes entity_uid = 4;  bytes event_uid = 5;
   uint32 authority_epoch = 6;  uint32 revision = 7;  uint32 sequence = 8;
   fixed64 source_sample_time = 9;  fixed64 input_onset_time = 10;  uint32 clock_id = 11;  uint32 clock_mapping_epoch = 12;
   uint32 clock_uncertainty = 13;  fixed64 response_deadline = 14;
   uint32 mission_frame_id = 15;  uint32 frame_epoch = 16;  uint32 calibration_epoch = 17;
   repeated float pose = 18;  uint32 units = 19;  uint32 quaternion_order = 20;
   uint32 operation = 21;  bytes target_entity_uid = 22;  bytes ack_event_uid = 23;  fixed64 expiry = 24;
   uint32 observation_provenance = 25;  fixed64 value = 26;
   uint32 membership_epoch = 27;  sint32 clock_offset = 28;  uint32 highest_sequence = 29;  repeated uint32 missing = 30;
   repeated float velocity = 31;  bytes owner_id = 32;
}
''')
for d in (v1_dir, v2_dir):
    files = [f for f in os.listdir(d) if f.endswith(".proto")]
    subprocess.run([sys.executable, "-m", "grpc_tools.protoc", f"-I{d}", f"--python_out={d}"] + files, check=True)
sys.path.insert(0, v1_dir)
import takmessage_pb2 as v1                                    # noqa: E402
sys.path.remove(v1_dir)
for mod in [m for m in list(sys.modules) if m.endswith("_pb2")]:
    del sys.modules[mod]
sys.path.insert(0, v2_dir)
import v2x_takmessage_pb2 as v2                                # noqa: E402

# ---------------------------------------------------------------------------------------------------------------
# Synthetic session. Seeded so the fixtures are reproducible.
import random                                                  # noqa: E402
rng = random.Random(20261113)

def uid():
    return str(uuid.UUID(int=rng.getrandbits(128), version=4))

def short():
    return f"{rng.getrandbits(32):08x}"

T0 = datetime(2026, 11, 13, 13, 47, 12, 345678, tzinfo=timezone.utc)

def iso_us(t):
    return t.strftime("%Y-%m-%dT%H:%M:%S.%fZ")

def iso_ms(t):
    return t.strftime("%Y-%m-%dT%H:%M:%S.") + f"{t.microsecond // 1000:03d}Z"

def epoch_us(t):
    return int(t.timestamp() * 1_000_000)

# Synthetic, explicitly labeled georeference (profile georeference_rule). Positions, poses and velocities
# vary per instance, so no dictionary can carry them.
LAT, LON, HAE = 35.0123457, -116.4567891, 712.3

def session(style):
    """Values that stay fixed for a whole session: candidates for the preset dictionary."""
    if style == "as_specified":
        return dict(session_id=uid(), owner=f"ANDROID-{rng.getrandbits(64):016x}", mission_frame_id="synthetic-mission-frame-1",
                    clock_id="clk-" + f"{rng.getrandbits(32):08x}")
    return dict(session_id=short(), owner=short(), mission_frame_id="1", clock_id="1")

def quat():
    q = [rng.gauss(0, 1) for _ in range(4)]
    n = sum(x * x for x in q) ** 0.5
    return ",".join(f"{x / n:.7f}" for x in q)

def vec(n, scale, digits):
    return ",".join(f"{rng.uniform(-scale, scale):.{digits}f}" for _ in range(n))

def ar_fields(kind, s, k, style):
    """The AR detail attributes for message kind and instance k, in profile order."""
    t = T0 + timedelta(milliseconds=97 * k)
    ident = (lambda: uid()) if style == "as_specified" else (lambda: short())
    tim = (lambda x: iso_us(x)) if style == "as_specified" else (lambda x: str(epoch_us(x)))
    seq = 4180 + k
    common = [("session_id", s["session_id"]), ("origin_owner_id", s["owner"]), ("sender_id", s["owner"])]
    if kind in ("urgent", "repair_data"):
        entity = ident()
        f = common + [("entity_uid", entity), ("event_uid", ident()), ("authority_epoch", "3"), ("revision", "1"),
                      ("sequence", str(seq)),
                      ("source_sample_time", tim(t - timedelta(microseconds=1730))), ("input_onset_time", tim(t - timedelta(microseconds=9400))),
                      ("clock_id", s["clock_id"]), ("clock_mapping_epoch", "17"), ("clock_uncertainty", "250"),
                      ("response_deadline", tim(t + timedelta(microseconds=90600))),
                      ("mission_frame_id", s["mission_frame_id"]), ("frame_epoch", "2"), ("calibration_epoch", "5"),
                      ("pose_direction", quat()), ("units", "m"), ("quaternion_order", "wxyz"),
                      ("operation", "create"), ("target_entity_uid", entity), ("ack_event_uid", ""),
                      ("expiry", tim(t + timedelta(seconds=30))), ("observation_provenance", "local_input"),
                      ("value", f"{rng.getrandbits(64):016x}")]
        return entity, f
    if kind == "state":
        entity = s.setdefault("avatar", ident())
        f = common + [("entity_uid", entity), ("authority_epoch", "3"), ("revision", str(90210 + k)), ("sequence", str(seq)),
                      ("source_sample_time", tim(t - timedelta(microseconds=1120))), ("clock_id", s["clock_id"]),
                      ("clock_mapping_epoch", "17"), ("clock_uncertainty", "250"),
                      ("mission_frame_id", s["mission_frame_id"]), ("frame_epoch", "2"), ("calibration_epoch", "5"),
                      ("pose", vec(3, 50, 4) + "," + quat()),
                      ("velocity", vec(3, 3, 3)), ("units", "m"), ("quaternion_order", "wxyz")]
        return entity, f
    if kind == "control":
        f = common + [("clock_id", s["clock_id"]), ("clock_mapping_epoch", "17"), ("clock_offset", "-1834"),
                      ("clock_uncertainty", "250"), ("membership_epoch", "4"), ("highest_sequence", str(seq)),
                      ("ack_event_uid", ident())]
        return s["owner"] + "-ctl", f
    if kind == "repair_request":
        f = common + [("owner_id", s["owner"]), ("missing", f"{seq - 2},{seq - 1}")]
        return s["owner"] + "-rq", f
    raise ValueError(kind)

LEAN_NAMES = {}

def short_name(name):
    if name not in LEAN_NAMES:
        LEAN_NAMES[name] = "".join(p[0] for p in name.split("_")) + str(len(LEAN_NAMES) % 10)
    return LEAN_NAMES[name]

TYPES = {"urgent": ("b-m-p-s-m", "h-e"), "repair_data": ("b-m-p-s-m", "h-e"), "state": ("a-f-G-U-C", "m-g"),
         "control": ("t-x-ar-c", "m-g"), "repair_request": ("t-x-ar-r", "m-g")}
ELEMENT = {"urgent": "ar_event", "repair_data": "ar_event", "state": "ar_state", "control": "ar_control",
           "repair_request": "ar_repair"}

def fixture(kind, s, k, style):
    """Return (xml_text, envelope dict, detail xml string, field list)."""
    entity, fields = ar_fields(kind, s, k, style)
    t = T0 + timedelta(milliseconds=97 * k)
    ctype, how = TYPES[kind]
    spatial = kind in ("urgent", "repair_data", "state")
    stale = t + timedelta(seconds=30 if kind != "state" else 2)
    env = dict(version="2.0", uid=entity, type=ctype, how=how, time=iso_ms(t), start=iso_ms(t), stale=iso_ms(stale))
    point = dict(lat=f"{LAT + rng.uniform(-5e-4, 5e-4):.7f}", lon=f"{LON + rng.uniform(-5e-4, 5e-4):.7f}",
                 hae=f"{HAE + rng.uniform(-2, 2):.1f}", ce="1.5", le="2.0") if spatial else \
        dict(lat="0.0", lon="0.0", hae="0.0", ce="9999999.0", le="9999999.0")
    names = [(n if style == "as_specified" else short_name(n), v) for n, v in fields]
    element = ELEMENT[kind] if style == "as_specified" else ELEMENT[kind][:2] + kind[0]
    detail = f"<{element} " + " ".join(f'{n}="{v}"' for n, v in names) + "/>"
    head = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n' if style == "as_specified" else ""
    xml = (head + "<event " + " ".join(f'{a}="{env[a]}"' for a in ("version", "uid", "type", "how", "time", "start", "stale"))
           + "><point " + " ".join(f'{a}="{point[a]}"' for a in ("lat", "lon", "hae", "ce", "le")) + "/><detail>" + detail
           + "</detail></event>")
    ET.fromstring(xml.split("\n", 1)[-1] if head else xml)              # well-formed check
    return xml, env, point, detail, fields

def ms(text):
    return int(datetime.strptime(text, "%Y-%m-%dT%H:%M:%S.%fZ").replace(tzinfo=timezone.utc).timestamp() * 1000)

def tak_message(module, env, point, detail_xml):
    m = module.TakMessage()
    e = m.cotEvent
    e.type, e.uid, e.how = env["type"], env["uid"], env["how"]
    e.sendTime, e.startTime, e.staleTime = ms(env["time"]), ms(env["start"]), ms(env["stale"])
    e.lat, e.lon, e.hae, e.ce, e.le = (float(point[a]) for a in ("lat", "lon", "hae", "ce", "le"))
    if detail_xml is not None:
        e.detail.xmlDetail = detail_xml
    return m

def round_trip_v1(env, point, detail_xml):
    """TAK v1 must give back the same event: envelope values and the detail text exactly."""
    raw = b"\xbf\x01\xbf" + tak_message(v1, env, point, detail_xml).SerializeToString()
    assert raw[:3] == b"\xbf\x01\xbf"
    back = v1.TakMessage.FromString(raw[3:]).cotEvent
    ok = (back.type == env["type"] and back.uid == env["uid"] and back.how == env["how"] and back.sendTime == ms(env["time"])
          and back.staleTime == ms(env["stale"]) and back.detail.xmlDetail == detail_xml
          and all(abs(getattr(back, a) - float(point[a])) == 0 for a in ("lat", "lon", "hae", "ce", "le")))
    return raw, ok

def to_bytes(v, style):
    if style == "as_specified" and re.fullmatch(r"[0-9a-f-]{36}", v):
        return uuid.UUID(v).bytes
    if re.fullmatch(r"[0-9a-f]{8}", v):
        return bytes.fromhex(v)
    if v.startswith("ANDROID-"):
        return bytes.fromhex(v[8:])
    return v.encode()

def ar_binary(env, point, fields, style):
    m = tak_message(v2, env, point, None)
    a = m.cotEvent.detail.ar
    enum = {"m": 1, "wxyz": 1, "create": 1, "local_input": 1}
    for name, value in fields:
        if not value or name == "pose_direction":
            continue                                               # pose_direction is packed into pose below
        if not hasattr(a, name):
            raise KeyError(name)
        field = a.DESCRIPTOR.fields_by_name[name]
        if field.type == field.TYPE_BYTES:
            setattr(a, name, to_bytes(value, style))
        elif name in ("pose", "velocity"):
            getattr(a, name).extend(float(x) for x in value.split(","))
        elif name == "missing":
            a.missing.extend(int(x) for x in value.split(","))
        elif name.endswith("_time") or name in ("response_deadline", "expiry"):
            setattr(a, name, epoch_us(datetime.strptime(value, "%Y-%m-%dT%H:%M:%S.%fZ").replace(tzinfo=timezone.utc))
                    if "T" in value else int(value))
        elif name == "value":
            a.value = int(value, 16)
        elif name in enum or value in enum:
            setattr(a, name, enum.get(value, 1))
        elif name in ("clock_id", "mission_frame_id"):
            setattr(a, name, zlib.crc32(value.encode()) & 0xFFFF)
        else:
            setattr(a, name, int(value))
    if any(n == "pose_direction" for n, _ in fields):
        a.pose.extend(float(x) for x in dict(fields)["pose_direction"].split(","))
    return b"\xbf\x02\xbf" + m.SerializeToString()

def deflate(data, zdict=None):
    c = zlib.compressobj(9, zlib.DEFLATED, -15, 9, zlib.Z_DEFAULT_STRATEGY, **({"zdict": zdict} if zdict else {}))
    out = c.compress(data) + c.flush()
    d = zlib.decompressobj(-15, **({"zdict": zdict} if zdict else {}))
    assert d.decompress(out) == data
    return out

# ---------------------------------------------------------------------------------------------------------------
KINDS = ("urgent", "state", "control", "repair_request", "repair_data")
PROFILE_BYTES = {"urgent": 800, "state": 800, "control": 400, "repair_request": 400, "repair_data": 200}
rows = []
os.makedirs(PREFIX + "_fixtures", exist_ok=True)
for style in ("as_specified", "lean"):
    s = session(style)
    for kind in KINDS:
        # The dictionary is a same-kind message from earlier in the session; measure the next five instances.
        dict_xml = fixture(kind, s, 0, style)[0].encode()
        sizes = {"xml": [], "xml_deflate": [], "xml_dict": [], "tak_v1": [], "ar_binary": []}
        trips = []
        for k in range(1, 6):
            xml, env, point, detail, fields = fixture(kind, s, k, style)
            data = xml.encode()
            if k == 1:
                open(os.path.join(PREFIX + "_fixtures", f"{style}_{kind}.xml"), "w").write(xml + "\n")
            sizes["xml"].append(len(data))
            sizes["xml_deflate"].append(len(deflate(data)))
            sizes["xml_dict"].append(len(deflate(data, dict_xml)))
            raw, ok = round_trip_v1(env, point, detail)
            trips.append(ok)
            sizes["tak_v1"].append(len(raw))
            sizes["ar_binary"].append(len(ar_binary(env, point, fields, style)))
        for enc, values in sizes.items():
            payload = max(values)
            rows.append(dict(style=style, kind=kind, encoding=enc, payload_bytes=payload,
                             charged_bytes=payload + UDP_IP_BYTES + AUTH_BYTES,
                             charged_bits=8 * (payload + UDP_IP_BYTES + AUTH_BYTES),
                             profile_abstract_bytes=PROFILE_BYTES[kind], round_trip=("pass" if all(trips) else "FAIL") if enc == "tak_v1" else ""))

with open(PREFIX + "_sizes.csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0]))
    w.writeheader()
    w.writerows(rows)
with open(PREFIX + "_sizes.md", "w") as f:
    f.write("# Measured CoT sizes (largest of five instances; charged = payload + 28 B UDP/IPv4 + 28 B authentication)\n\n")
    for style in ("as_specified", "lean"):
        f.write(f"## {style}\n\n| message | profile abstract | " + " | ".join(
            ("xml", "xml + deflate", "xml + dictionary deflate", "TAK v1 (detail as text)", "binary AR detail (new TAK version)")) + " |\n")
        f.write("|---|---|---|---|---|---|---|\n")
        for kind in KINDS:
            cells = [f"{r['payload_bytes']} / {r['charged_bytes']}" for r in rows if r["style"] == style and r["kind"] == kind]
            f.write(f"| {kind} | {PROFILE_BYTES[kind]} | " + " | ".join(cells) + " |\n")
        f.write("\nEach cell: payload bytes / charged bytes.\n\n")
    trips = sorted({r["round_trip"] for r in rows if r["encoding"] == "tak_v1"})
    f.write(f"TAK v1 round trip (envelope values and detail text identical after decode): {', '.join(trips)}\n")
print(open(PREFIX + "_sizes.md").read())
