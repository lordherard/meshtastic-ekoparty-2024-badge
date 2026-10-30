from pathlib import Path
import os, subprocess, sys, json, datetime, hashlib, re, ctypes
root=next((p for p in [Path.cwd(), *Path(__file__).resolve().parents] if (p/'work/meshtastic').is_dir()),None)
if root is None:
    raise SystemExit('Run from the workspace containing work/meshtastic and work/host-compiler.')
audit=root/'outputs/phase5/host-crash-investigation'
stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
run=audit/(stamp+'-runner'); run.mkdir(parents=True)
record={'status':'RUNNING','accepted_groups':[], 'steps':[]}
def save():
    (run/'result.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
def execute(cmd, label, env=None):
    step={'command':cmd,'label':label}; record['steps'].append(step); save()
    try:
        result=subprocess.run(cmd,capture_output=True,timeout=600,env=env)
        (run/(label+'.stdout.txt')).write_bytes(result.stdout)
        (run/(label+'.stderr.txt')).write_bytes(result.stderr)
        step.update(exit_code=result.returncode,exit_hex=f'0x{result.returncode & 0xffffffff:08X}')
        if result.returncode: raise RuntimeError(label+' failed: '+step['exit_hex'])
        save(); return result
    except Exception as exc:
        record.update(status='FAILED',error=str(exc),accepted_groups=[]); save()
        print(str(run),flush=True); raise
if sys.platform=='win32': ctypes.windll.kernel32.SetErrorMode(3)
def validate(exe):
    source=(root/'work/meshtastic/test/test_eko_badge_composer/host_main.cpp').read_text()
    expected=[''.join(re.findall(r'"([^"\n]*)"',x)) for x in re.findall(r'puts\(("PASS .*?)\);',source,re.S)]
    record['executable_sha256']=hashlib.sha256(exe.read_bytes()).hexdigest()
    result=execute([str(exe)],'tests')
    actual=[line for line in result.stdout.decode('utf-8',errors='replace').splitlines() if line.startswith('PASS ')]
    if len(expected)!=30 or actual!=expected:
        record.update(status='FAILED',error='Incomplete or unexpected group list',accepted_groups=[]); save()
        raise RuntimeError(record['error'])
    record.update(status='PASS',accepted_groups=actual); save()
    print(str(run)); print(f'PASS {len(actual)} groups; exit 0')
if len(sys.argv)>1:
    if len(sys.argv)!=3 or sys.argv[1]!='--existing-exe': raise SystemExit('Usage: --existing-exe PATH')
    validate(Path(sys.argv[2]).resolve()); raise SystemExit(0)
repo=root/'work/meshtastic'
suite=repo/'test/test_eko_badge_composer'
build=root/'work/phase5-host'
build.mkdir(exist_ok=True)
stubs=build/'include'
stubs.mkdir(exist_ok=True)
for header in ['Preferences.h','graphics/Screen.h','configuration.h','BluetoothStatus.h','MeshService.h','NodeDB.h','SinglePortModule.h','Channels.h','Router.h','RadioInterface.h','RadioLibInterface.h','concurrency/LockGuard.h','concurrency/OSThread.h','detect/LoRaRadioType.h','modules/TextMessageModule.h','target_specific.h','Adafruit_NeoPixel.h','SSD1306Wire.h','Wire.h']:
    p=stubs/header
    p.parent.mkdir(parents=True,exist_ok=True)
    p.write_text('#pragma once\n#include "Fakes.h"\n')
# Run the two original Unity composer cases unchanged through assert adapters.
(stubs/'TestUtil.h').write_text("#pragma once\n#include <cassert>\n#include <cstring>\n#define TEST_ASSERT_EQUAL_STRING(a,b) assert(strcmp(a,b)==0)\n#define TEST_ASSERT_EQUAL(a,b) assert((a)==(b))\n#define TEST_ASSERT_TRUE(a) assert(a)\n#define TEST_ASSERT_FALSE(a) assert(!(a))\n#define UNITY_BEGIN() ((void)0)\n#define RUN_TEST(f) f()\n#define UNITY_END() 0\n")
(stubs/'unity.h').write_text('#pragma once\n#include "TestUtil.h"\n')
zig=root/'work/audio-toolchain-0930/ziglang/zig.exe'
env=os.environ.copy()
env['ZIG_GLOBAL_CACHE_DIR']=str(build/'zig-cache')
env['ZIG_LOCAL_CACHE_DIR']=str(build/'zig-local')
np=repo/'.pio/libdeps/meshtastic_eko_badge_2024/Nanopb'
font=repo/'.pio/libdeps/meshtastic_eko_badge_2024/ESP8266 and ESP32 OLED driver for SSD1306 displays/src/OLEDDisplayFonts.cpp'
includes=[stubs,suite/'host',repo/'src/platform/extra_variants/eko_badge_2024',repo/'variants/esp32c6/eko_badge_2024',repo/'src',repo/'src/mesh',repo/'src/mesh/generated',np]
flags=[x for d in includes for x in ['-I',str(d)]]
utf8=build/'production_utf8.cpp'
source=(repo/'src/meshUtils.cpp').read_text()
start=source.index('bool sanitizeUtf8(')
end=source.index('\nfloat parseDecimalFloat',start)
utf8.write_text('#include <cstdint>\n#include <cstring>\n'+source[start:end])
objects=[]
for name in ['pb_common','pb_encode']:
    obj=build/(name+'.o')
    execute([str(zig),'cc','-c',str(np/(name+'.c')),'-I',str(np),'-o',str(obj)],name,env)
    objects.append(str(obj))
exe=run/'test-badge.exe'
protobufs = ['mesh','channel','config','device_ui','module_config','portnums','telemetry','xmodem']
cmd=[str(zig),'c++','-std=c++17','-DEKO_BADGE_2024','-DEKO_BADGE_HOST_TEST','-DPROGMEM=','-include','cstdint','-O0','-g',*flags,str(suite/'host_main.cpp'),str(utf8),str(repo/'src/platform/extra_variants/eko_badge_2024/BadgeMessageSender.cpp'),str(font),*[str(repo/'src/mesh/generated/meshtastic'/(p+'.pb.cpp')) for p in protobufs],*objects,'-o',str(exe)]
execute(cmd,'compile',env)
validate(exe)
