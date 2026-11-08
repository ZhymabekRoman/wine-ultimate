set pagination off
set confirm off
set debuginfod enabled off
set architecture i386:x86-64
set language c
set $MB=@MB@
python
import gdb,sys,time
def rd(p,n):
    return gdb.selected_inferior().read_memory(p,n).tobytes()
def qword(p):
    return int.from_bytes(rd(p,8),'little')
def cstr(p,lim=64):
    p=int(p)
    if p<0x10000 or p>0x7FFFFFFFFFFF: return ''
    try: b=rd(p,lim)
    except Exception: return ''
    i=b.find(b'\x00')
    if i<1: return ''
    s=b[:i]
    if not all(31<c<127 for c in s): return ''
    return s.decode('ascii')
def deref_name(p,depth=2):
    p=int(p)
    if depth<=0: return ''
    for off in range(0x00,0x48,8):
        try: v=qword(p+off)
        except Exception: continue
        s=cstr(v)
        if s and 2<len(s)<64: return s
    for off in range(0x00,0x28,8):
        try: v=qword(p+off)
        except Exception: continue
        if 0x10000<v<0x7FFFFFFFFFFF:
            s=deref_name(v,depth-1)
            if s: return s
    return ''
def emit(m):
    sys.stdout.write('t=%.3f %s\n'%(time.time(),m)); sys.stdout.flush()
gdb.cnt=0
gdb.card=0
def hexs(p,n):
    try: return rd(p,n).hex()
    except Exception: return '?'
end
break *(@MB@+0x52288)
commands
silent
python
import gdb
from __main__ import emit,qword,deref_name,hexs
if gdb.cnt<40:
    gdb.cnt+=1
    o=int(gdb.parse_and_eval('$rcx'))
    sl=int(gdb.parse_and_eval('$rdx'))
    v=0
    try: v=qword(sl)
    except Exception: pass
    emit('[ENTRY n=%d obj=%#x slot=%#x slotval=%#x r8=%#x m=%s]'%(gdb.cnt,o,sl,v,int(gdb.parse_and_eval('$r8')),hexs(o-16,48)))
    if gdb.cnt==1:
        wa=(sl & ~0xfffff)+0xde8
        bp=gdb.Breakpoint('*%#x'%wa,type=gdb.BP_WATCHPOINT,wp_class=gdb.WP_WRITE)
        gdb.wa=wa
        bp.commands="silent\npython import gdb,sys,time;m=__import__('__main__');rip=int(gdb.parse_and_eval('$rip'));val=int.from_bytes(gdb.selected_inferior().read_memory(m.gdb.wa,8).tobytes(),'little');sys.stdout.write('t=%.3f [W] rip=%#x val=%#x\\n'%(time.time(),rip,val));sys.stdout.flush();gdb.execute('bt 8')\ncontinue\nend"
        bp.silent=True
        gdb.wbp=bp
        emit('[ARM] watch %#x'%wa)
end
continue
end
break *(@MB@+0x522c3)
commands
silent
python
import gdb,sys,time
def emit(m):
    sys.stdout.write('t=%.3f %s\n'%(time.time(),m)); sys.stdout.flush()
if gdb.card<8:
    gdb.card+=1
    o=int(gdb.parse_and_eval('$rbp'))
    emit('[CARD rbp=%#x rsi=%#x r14=%#x rax=%#x]'%(o,int(gdb.parse_and_eval('$rsi')),int(gdb.parse_and_eval('$r14')),int(gdb.parse_and_eval('$rax'))))
end
continue
end
python
import gdb
from __main__ import emit,hexs,qword
gdb.card=0
inf=gdb.inferiors()[0]
for n in range(3000):
    if len(inf.threads())==0:
        emit('[EXIT] inferior gone at iter %d'%n); break
    try:
        gdb.execute('continue')
    except gdb.error as e:
        emit('[ERR] %s'%e); break
end
