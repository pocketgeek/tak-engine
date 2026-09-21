"""Controlled movement-type inputs for standard/Crusades oracle comparisons.

Read source FBI/MOVEINFO assets independently of World. Select matching native
classes already loaded in the capture, checking their definitions first. This
changes type inputs, never native query outputs or World's grade results.
"""
from pathlib import Path
import re
import struct
import subprocess

from decode_save_state import decode


def properties(text):
    text = re.sub(r'/\*.*?\*/|//[^\r\n]*', '', text, flags=re.S)
    return {key.lower(): value.strip() for key,value in
            re.findall(r'([A-Za-z][A-Za-z0-9_]*)\s*=\s*([^;]*);', text)}


def unit_properties(text):
    """Read UNITINFO only; weapon sections also declare keys such as turnrate."""
    text=re.sub(r'/\*.*?\*/|//[^\r\n]*','',text,flags=re.S)
    start=re.search(r'\[\s*unitinfo\s*\]\s*\{',text,re.I)
    if not start: raise ValueError('source FBI lacks UNITINFO')
    depth=1;body=[]
    for char in text[start.end():]:
        if char=='{': depth+=1
        elif char=='}':
            depth-=1
            if depth==0: return properties(''.join(body))
        elif depth==1: body.append(char)
    raise ValueError('unterminated source UNITINFO')


def class_record(fields):
    number = lambda key, default: int(fields.get(key, default))
    dry, wet = number('maxslope',255)&255, number('maxwaterslope',255)&255
    soft_dry, soft_wet = number('badslope',dry//2)&255, number('badwaterslope',wet//2)&255
    dry = min(dry,wet)
    maximum, minimum = number('maxwaterdepth',10000), number('minwaterdepth',-10000)
    return struct.pack('<6h4B',number('footprintx',0),number('footprintz',0),
        maximum,minimum,number('badmaxwaterdepth',maximum),number('badminwaterdepth',minimum),
        dry,min(soft_dry,dry),wet,min(soft_wet,wet))


def set_balance_inputs(process, capture, save_path, retail_root, crusades,
                       hpitool=Path('build-dbg/hpitool'), corpses=False, surface=False, motion=False):
    saved=decode(save_path.read_bytes())
    if saved['source_sha256'] != capture['source_sha256']:
        raise ValueError('balance input save does not belong to capture')
    cache={}
    def asset(path, optional=False):
        if path not in cache:
            found=subprocess.run([str(hpitool),'where',str(retail_root),path],
                                 text=True,capture_output=True)
            if found.returncode:
                if optional: return None
                raise ValueError(f'asset unavailable: {path}: {found.stderr}')
            location=found.stdout.strip().split(' -> ',1)[-1]
            if '!' not in location: raise ValueError(f'expected archive asset: {location}')
            archive,internal=location.split('!',1)
            cache[path]=subprocess.run([str(hpitool),'cat',str(retail_root/archive),internal],
                                      text=True,capture_output=True,check=True).stdout
        return cache[path]
    definitions={}
    for block in re.findall(r'\[[^]]+\]\s*\{([^{}]*)\}',asset('gamedata/moveinfo.tdf')):
        fields=properties(block)
        if 'name' in fields: definitions[fields['name'].lower()]=class_record(fields)
    classes={}
    for address in range(0x62dbf0,0x634670,0x354):
        pointer=process.u32(address)
        if not pointer: continue
        name=bytes(process.uc.mem_read(pointer,64)).split(b'\0')[0].decode('ascii').lower()
        record=bytes(process.uc.mem_read(address+4,16))
        if name not in definitions or record != definitions[name]:
            raise ValueError(f'captured MOVEINFO class differs from source assets: {name}')
        classes[name]=address
    saved_types={u['id']:u['type'].lower() for u in saved['units']}
    written=set()
    corpse_written=set()
    feature_names={}
    if corpses:
        table=process.u32(process.game+0x19edc)
        for index in range(process.u32(process.game+0x19ec0)):
            name=bytes(process.uc.mem_read(table+index*320,32)).split(b'\0')[0].decode('ascii').lower()
            feature_names[name]=index
    selected={}
    for unit in process.frame['units']:
        name=saved_types[unit['id']]
        source=asset(f'unitscb/{name}.fbi',True) if crusades else None
        if source is None: source=asset(f'units/{name}.fbi')
        fields=unit_properties(source)
        movement=fields.get('movementclass','').lower()
        if corpses and unit['type_address'] not in corpse_written:
            footprint=tuple(int(fields.get(key,1)) for key in ('footprintx','footprintz'))
            if movement in definitions:
                inherited=struct.unpack_from('<2h',definitions[movement])
                footprint=tuple(parent if parent>0 else own for parent,own in zip(inherited,footprint))
            if footprint != tuple(unit['footprint']):
                raise ValueError(f'balance switch changes captured corpse anchor footprint: {name}')
            for key,offset in (('corpse',0x24e),('stone',0x250),('frozen',0x252)):
                feature=fields.get(key,'').lower()
                if feature and feature not in feature_names:
                    raise ValueError(f'controlled corpse feature is not loaded in capture: {feature}')
                process.uc.mem_write(unit['type_address']+offset,
                    struct.pack('<H',feature_names[feature] if feature else 0xffff))
            adjustment=tuple(int(fields.get(key,0)) for key in ('corpseadjustx','corpseadjustz'))
            process.uc.mem_write(unit['type_address']+0x25c,struct.pack('<2h',*adjustment))
            corpse_written.add(unit['type_address'])
        if movement not in classes:
            if unit.get('mover_address'): raise ValueError(f'unhandled movement class for {name}: {movement}')
            continue
        grid=classes[movement]
        record=definitions[movement]
        if tuple(unit['footprint']) != struct.unpack_from('<hh',record):
            raise ValueError(f'balance switch changes captured footprint: {name}')
        kind=unit['type_address']
        if kind not in written:
            if motion:
                process.uc.mem_write(kind+0x226,struct.pack('<h',int(fields.get('sightdistance',0))))
                for key,offset,default in (('maxvelocity',0x162,0),('brakerate',0x166,0.5),
                                           ('acceleration',0x16a,0.5)):
                    process.uc.mem_write(kind+offset,struct.pack('<i',int(float(fields.get(key,default))*65536)))
                for key,offset,default in (('turnrate',0x18e,500),('turninplacerate',0x190,0)):
                    process.uc.mem_write(kind+offset,struct.pack('<H',int(float(fields.get(key,default)))&65535))
            if surface:
                flags=process.u32(kind+0x260)&~(0x100000|0x80000|0x1000)
                for key,bit in (('upright',0x100000),('floater',0x80000),('canhover',0x1000)):
                    if int(fields.get(key,0)): flags|=bit
                process.uc.mem_write(kind+0x260,struct.pack('<I',flags))
                process.uc.mem_write(kind+0x248,bytes([int(fields.get('waterline',0))&255]))
                for key,offset in (('bankscale',0x176),('pitchscale',0x17a)):
                    default=0 if key=='pitchscale' and int(fields.get('canfly',0)) else 0.5
                    process.uc.mem_write(kind+offset,struct.pack('<i',int(float(fields.get(key,default))*65536)))
            process.uc.mem_write(kind+0x18a,struct.pack('<I',grid))
            process.uc.mem_write(kind+0x192,record[4:12])
            process.uc.mem_write(kind+0x23c,bytes((record[12],record[14])))
            road=int(float(fields.get('roadmultiplier',1.2))*65536)
            water=int(float(fields.get('watermultiplier',fields.get('watermultipliser',1)))*65536)
            process.uc.mem_write(kind+0x172,struct.pack('<i',road))
            process.uc.mem_write(kind+0x16e,struct.pack('<i',water))
            if motion:
                maximum=int(float(fields.get('maxvelocity',0))*65536)
                best=max(road,water,65536)*maximum>>16
                scale=255 if best<=0 else max(1,min(255,(8*65536)//best))
                process.uc.mem_write(kind+0x249,bytes([scale]))
            written.add(kind)
        mover=unit.get('mover_address')
        if mover:
            process.uc.mem_write(mover+4,struct.pack('<I',grid))
            selected[unit['id']]=grid
    process.uc.mem_write(0x641144,bytes([int(crusades)]))
    return selected
