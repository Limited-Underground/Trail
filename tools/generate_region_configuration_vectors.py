"""Independent binary layout cases for strict configuration 0.3 / OTRC v1."""
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
rows = []
def case(name, kind, accepted, wire, fields="-"):
    rows.append("\t".join((name, kind, str(int(accepted)), wire.hex() or "-", fields)))

def region(kind, status=0, revision=0, selection=0):
    return struct.pack("<4sBBBBQH6x", b"OTRC", 1, kind, status, 0, revision, selection)

def envelope(kind, payload):
    return struct.pack("<4sBBBBIIBBH", b"OTC0", 0, 3, kind, 0, 7, 19, 0, 1, len(payload)) + payload

info = bytes.fromhex("4f544230000301ff8000970001010000")
case("info3", "info3", True, info, "255")
for i in range(16):
    b=bytearray(info);b[i]^=1
    case(f"info_bad_{i}","info3",False,b)

values=[(1,0,0,0),(0x81,0,0,0)]
for selection in range(1,13):
    values += [(2,0,0,selection),(2,0,2**64-1,selection),(0x81,0,12,selection),(0x82,0,13,selection)]
values += [(2,0,12,65535),(0x81,0,12,65535)]
values += [(0x83,s,0,0) for s in range(1,5)] + [(0x84,5,0,0)]
for index,(kind,status,revision,selection) in enumerate(values):
    wire=region(kind,status,revision,selection)
    fields=f"{kind}|{status}|{revision}|{selection}"
    case(f"region_{index}","region",True,wire,fields)
    outer=6 if kind in (1,2) else 0x88
    case(f"frame_region_{index}","frame3",True,envelope(outer,wire),f"{outer}|7|19|{wire.hex()}")
    case(f"wrong_direction_{index}","frame3",False,envelope(0x88 if outer==6 else 6,wire))
    for reserved in [7,18,19,20,21,22,23]:
        b=bytearray(wire);b[reserved]=1
        case(f"region_reserved_{index}_{reserved}","region",False,b)
    case(f"region_short_{index}","region",False,wire[:-1])
    case(f"region_long_{index}","region",False,wire+b"\0")
for index,args in enumerate([(1,1,0,0),(1,0,1,0),(1,0,0,1),(2,1,0,1),(2,0,0,0),
    (0x81,0,0,1),(0x81,0,1,0),(0x82,0,0,1),(0x82,1,1,1),(0x83,0,0,0),
    (0x83,5,0,0),(0x83,1,1,0),(0x84,0,0,0),(0x84,5,0,1),(0,0,0,0),(3,0,0,0)]):
    case(f"region_fields_{index}","region",False,region(*args))

# Preserve existing valid base/name/time bytes, changing only the negotiated minor.
for line in (ROOT/"tests/fixtures/companion_configuration_v02.tsv").read_text().splitlines():
    if not line or line.startswith("#"):continue
    name,kind,accepted,hexwire,fields=line.split("\t")
    if kind=="frame" and accepted=="1":
        b=bytearray.fromhex(hexwire);b[5]=3
        case("carry_"+name,"frame3",True,b,fields)
        b[5]=2
        case("wrong_minor_"+name,"frame3",False,b)

target=ROOT/"tests/fixtures/companion_configuration_v03.tsv"
target.write_text("# name\ttype\taccepted\thex\tfields\n"+"\n".join(rows)+"\n",encoding="utf-8",newline="\n")
print(f"Wrote {len(rows)} independent 0.3/region cases")
