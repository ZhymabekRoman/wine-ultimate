set pagination off
set confirm off
set debuginfod enabled off
set height 0
python
import gdb,sys,time,os
def emit(m): sys.stdout.write('t=%.3f %s\n'%(time.time(),m)); sys.stdout.flush()
class WriteBP(gdb.Breakpoint):
    def __init__(self,addr,tag):
        gdb.Breakpoint.__init__(self,'*%#x'%addr,type=gdb.BP_WATCHPOINT,wp_class=gdb.WP_WRITE,internal=True)
        self.addr=addr; self.tag=tag
    def stop(self):
        try:
            f=gdb.newest_frame(); rip=int(f.read_register('rip'))
            val=int.from_bytes(gdb.selected_inferior().read_memory(self.addr,8).tobytes(),'little')
            emit('[W %s addr=%#x] rip=%#x val=%#x'%(self.tag,self.addr,rip,val))
            gdb.execute('bt 8')
        except Exception as e: emit('[W-ERR] %s'%e)
        return False
def arm(nm,addr,tag):
    old=getattr(gdb,nm,None)
    if old is not None:
        try: old.delete()
        except Exception: pass
    setattr(gdb,nm,WriteBP(addr,tag))
class RetAlloc(gdb.FinishBreakpoint):
    def __init__(self):
        gdb.FinishBreakpoint.__init__(self, internal=True, temporary=True)
    def stop(self):
        try: base=int(gdb.parse_and_eval('$rax'))
        except Exception: return False
        if base and base!=0xffffffffffffffff:
            emit('[MMAP-RET] base=%#x'%base)
            arm('wpA',base+0xde8,'A'); arm('wpB',base+0xef98,'B')
        return False
class MmapBrk(gdb.Breakpoint):
    def __init__(self):
        gdb.Breakpoint.__init__(self,'mmap',internal=True)
    def stop(self):
        try:
            f=gdb.newest_frame(); ln=int(f.read_register('rdi'))
        except Exception: return False
        if 0x100000<=ln<=0x200000: RetAlloc()
        return False
class EntryBrk(gdb.Breakpoint):
    def __init__(self,addr):
        gdb.Breakpoint.__init__(self,'*%#x'%addr,internal=True)
        self.n=0
    def stop(self):
        self.n+=1
        try:
            f=gdb.newest_frame(); rcx=int(f.read_register('rcx')); rdx=int(f.read_register('rdx'))
        except Exception: return False
        if self.n<=200:
            v=0
            try: v=int.from_bytes(gdb.selected_inferior().read_memory(rdx,8).tobytes(),'little')
            except Exception: pass
            emit('[ENTRY n=%d] obj=%#x slot=%#x slotval=%#x'%(self.n,rcx,rdx,v))
        if self.n==13 and rdx: arm('wpC',rdx,'C')
        if rdx: arm('wpD',rdx,'D')
        return False
gdb.mmap=MmapBrk()
inf=gdb.inferiors()[0]
emit('[ATT] pid=%d'%inf.pid)
gdb.entry=None
for n in range(40000):
    if gdb.entry is None:
        try:
            for L in open('/proc/%d/maps'%inf.pid):
                if 'mrt100_app.dll' in L and 'r-xp' in L:
                    b=int(L.split('-')[0],16)
                    gdb.entry=EntryBrk(b+0x52288)
                    emit('[ENTRY-ARM] base=%#x'%b)
                    break
        except Exception: pass
    if len(inf.threads())==0: break
    try: gdb.execute('continue')
    except gdb.error as e: emit('[ERR] %s'%e); break
emit('[END] gone')
end
