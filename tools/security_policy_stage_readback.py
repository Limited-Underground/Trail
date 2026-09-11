"""Read-only, fail-closed OT-195 projection from exactly three plaintext NVS pages.

ESP-IDF v6.0.2 nvs_constants.h/nvs_page.cpp/nvs_types.cpp define the layout.
No filesystem, device, subprocess or arbitrary key/value projection is exposed.
assert_fresh allows fully purged erased slots, but rejects recognizable diagnostic
namespace history. It does not prove the namespace never existed historically.
decode requires separately established fresh-original, one-attempt custody; it
cannot establish invocation freshness by itself. CRCs are not authentication.
"""
import binascii
import struct
import zlib

NAMESPACE=b'ot195diag'
KEY=b'stage'
STAGES={1:'admitted',2:'install_result',3:'waiting',4:'input_result',5:'evaluation_enter',6:'evaluation_return',7:'send_enter',8:'send_return'}
ERRORS={0:'none',1:'console_install',2:'input_refused',3:'session_refused',4:'entropy_start',5:'sodium_init',6:'evaluation_failed',7:'entropy_stop',8:'receipt_send'}
ALLOWED={1:{0},2:{0,1},3:{0},4:{0,2,3},5:{0},6:{0,4,5,6,7},7:{0},8:{0,8}}
class ReadbackError(ValueError):
    """Fixed refusal only; never carries raw bytes, keys, paths or identities."""
def need(ok):
    if not ok:raise ReadbackError('stage_readback_refused')
def crc(raw):return zlib.crc32(raw,0xffffffff)&0xffffffff

def _parse(raw,fresh):
    need(type(raw) is bytes and len(raw)==12288)
    entries=[];sequences=set();active=0
    for offset in range(0,12288,4096):
        page=raw[offset:offset+4096]
        state,sequence=struct.unpack_from('<II',page)
        if state==0xffffffff:
            need(page==b'\xff'*4096);continue
        need(state in (0xfffffffe,0xfffffffc))
        active+=state==0xfffffffe
        need(sequence not in sequences and sequence<0xffffff00);sequences.add(sequence)
        need(page[8]==0xfe and page[9:28]==b'\xff'*19 and crc(page[4:28])==struct.unpack_from('<I',page,28)[0])
        bitmap=int.from_bytes(page[32:64],'little');need(bitmap>>252==15)
        states=[(bitmap>>(2*i))&3 for i in range(126)]
        need(1 not in states)
        i=0
        while i<126:
            status=states[i];item=page[64+32*i:96+32*i]
            if status==3:
                need(item==b'\xff'*32);i+=1;continue
            if status==0 and item==b'\x00'*32:
                i+=1;continue
            need(crc(item[:4]+item[8:])==struct.unpack_from('<I',item,4)[0])
            ns,kind,span,chunk=item[:4]
            need(ns!=255 and 1<=span<=126-i)
            keyraw=item[8:24];need(b'\x00' in keyraw)
            key=keyraw.split(b'\x00',1)[0]
            need(1<=len(key)<=15 and all(32<=c<127 for c in key))
            need(all(s==status for s in states[i:i+span]))
            data=item[24:32]
            if kind in (1,2,4,8,0x11,0x12,0x14,0x18):
                need(span==1 and chunk==255)
            elif kind in (0x21,0x41,0x42):
                size,reserved,checksum=struct.unpack('<HHI',data)
                need(reserved==65535 and span==1+(size+31)//32 and size<=4000)
                need((kind==0x42 and chunk!=255) or (kind!=0x42 and chunk==255))
                payload=page[96+32*i:64+32*(i+span)]
                need(crc(payload[:size])==checksum and payload[size:]==b'\xff'*(len(payload)-size))
                if kind==0x21:need(size>0 and payload[size-1]==0)
            elif kind==0x48:
                size,count,start,reserved=struct.unpack('<IBBH',data)
                need(span==1 and chunk==255 and reserved==65535 and start in (0,128) and count<=127 and size<=12288)
            else:need(False)
            entries.append((status,ns,kind,key,data,chunk))
            i+=span
    need(active<=1)
    namespaces={};names=set()
    for status,ns,kind,key,data,chunk in entries:
        if ns==0:
            need(kind==1 and 1<=data[0]<255 and data[1:]==b'\xff'*7)
            if fresh:need(key!=NAMESPACE)
            # Deleted namespace identity cannot safely resolve surviving entries.
            need(status==2 and data[0] not in namespaces and key not in names)
            namespaces[data[0]]=key;names.add(key)
    live={}
    for status,ns,kind,key,data,chunk in entries:
        if ns==0:continue
        need(ns in namespaces)
        if fresh:need(namespaces[ns]!=NAMESPACE)
        if status==2:
            identity=(ns,key,chunk)
            need(identity not in live);live[identity]=(kind,data)
    # Validate live multi-page blob closure; never expose its payload.
    claimed=set()
    for (ns,key,chunk),(kind,data) in live.items():
        if kind==0x48:
            size,count,start,_=struct.unpack('<IBBH',data);total=0
            for n in range(count):
                ident=(ns,key,start+n);need(ident in live and live[ident][0]==0x42)
                need(ident not in claimed);claimed.add(ident)
                total+=struct.unpack_from('<H',live[ident][1])[0]
            need(total==size)
    need(all(kind!=0x42 or ident in claimed for ident,(kind,_) in live.items()))
    return namespaces,live

def assert_fresh(raw):
    """True only for supported storage with no diagnostic namespace history."""
    _parse(raw,True)
    return True

def decode(raw):
    """Fixed record projection only; the caller must bind fresh original custody."""
    namespaces,live=_parse(raw,False)
    indices=[i for i,name in namespaces.items() if name==NAMESPACE]
    need(len(indices)==1)
    selected=[(key,chunk,kind,data) for (ns,key,chunk),(kind,data) in live.items() if ns==indices[0]]
    need(len(selected)==1)
    key,chunk,kind,data=selected[0]
    need(key==KEY and chunk==255 and kind==8)
    need(data[:4]==bytes([0x95,0xd1,1,1]))
    stage,error=data[4:6]
    need(stage in STAGES and error in ALLOWED[stage])
    need(binascii.crc_hqx(data[:6],0xffff)==int.from_bytes(data[6:],'little'))
    return {'schema':'OT195-STAGE-READBACK-1','image':1,'stage':STAGES[stage],'error':ERRORS[error]}
