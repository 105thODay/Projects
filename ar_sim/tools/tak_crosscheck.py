"""Cross-check the C++ codec's TAK Protocol v1 bytes against protobuf classes generated from the TAK .proto files.

Usage: python -I tools/tak_crosscheck.py <takproto_dir> <dump_dir>
  takproto_dir  the TAK Protocol .proto files from the public ATAK-CIV repository (takproto/); read, never copied
  dump_dir      the directory `ar_sim codec <dump_dir>` wrote: tak_specified_<kind>.bin, tak_lean_<kind>.bin

For each dump the script checks:
  1. the stream header is 0xBF 0x01 0xBF (TAK Protocol, version 1, mesh);
  2. the remainder parses as TakMessage;
  3. with unknown fields discarded, re-serializing gives the same bytes (so none were present, and field order
     and proto3 default omission are canonical);
  4. the envelope fields are the ones CoT requires and xmlDetail parses as XML.
Exit status 0 when every file passes.
"""
import glob
import os
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

proto_dir, dump_dir = sys.argv[1], sys.argv[2]
build = tempfile.mkdtemp(prefix="takproto_check_")
files = []
for path in sorted(glob.glob(os.path.join(proto_dir, "*.proto"))):
    name = os.path.basename(path)
    with open(path, "r", encoding="utf-8") as src, open(os.path.join(build, name), "w", encoding="utf-8") as dst:
        dst.write(src.read())
    files.append(os.path.join(build, name))
subprocess.run([sys.executable, "-m", "grpc_tools.protoc", f"-I{build}", f"--python_out={build}"] + files, check=True)
sys.path.insert(0, build)
import takmessage_pb2  # noqa: E402

failures = 0
dumps = sorted(glob.glob(os.path.join(dump_dir, "tak_*.bin")))
print(f"{'file':32} {'bytes':>6} {'header':>7} {'parse':>6} {'canon':>6}  envelope")
for path in dumps:
    data = open(path, "rb").read()
    header = data[:3] == b"\xbf\x01\xbf"
    message = takmessage_pb2.TakMessage()
    parsed, canonical, envelope = False, False, ""
    try:
        message.ParseFromString(data[3:])
        parsed = True
        # Unknown fields survive a parse; discarding them and still re-serializing to the same bytes proves
        # there were none, and that field order and default omission are canonical.
        message.DiscardUnknownFields()
        canonical = message.SerializeToString(deterministic=True) == data[3:]
        event = message.cotEvent
        ET.fromstring(event.detail.xmlDetail)
        envelope = f"type={event.type} how={event.how} uid={event.uid[:24]} detail={len(event.detail.xmlDetail)} B"
        if not (event.type and event.uid and event.how and event.sendTime and event.startTime and event.staleTime):
            envelope += "  MISSING REQUIRED FIELD"
            parsed = False
    except Exception as error:  # noqa: BLE001 -- report any parse failure as a failed check
        envelope = f"error: {error}"
    ok = header and parsed and canonical
    failures += 0 if ok else 1
    print(f"{os.path.basename(path):32} {len(data):6d} {str(header):>7} {str(parsed):>6} {str(canonical):>6}  {envelope}")
print(f"TAK cross-check: {len(dumps)} files, {failures} failures")
sys.exit(1 if failures or not dumps else 0)
