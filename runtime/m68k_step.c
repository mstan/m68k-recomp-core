#include "m68k_step.h"
#include <setjmp.h>
#include <string.h>

enum { S=0x2000, T=0x8000, MASK=0xA71F };
static void data_flush(M68kStepContext *c)
{if(c->data_active){c->data_read.data=NULL;c->data_write.data=NULL;}}
static void data_end(M68kStepContext *c)
{data_flush(c);c->data_active=0;}
static unsigned data_suspend(M68kStepContext *c)
{unsigned active=c->data_active;if(active)data_end(c);return active;}
static void data_resume(M68kStepContext *c,unsigned active)
{
    if(active){c->data_read.data=NULL;c->data_write.data=NULL;c->data_active=active;}
}
static uint8_t *data_pointer(M68kStepContext *c,uint32_t a,size_t bytes,int write)
{
    if(!c->data_active)return NULL;
    M68kStepDataRegion *view=write?&c->data_write:&c->data_read;
    if(view->data&&a>=view->address&&a-view->address<view->bytes&&
       bytes<=view->bytes-(a-view->address))return view->data+(a-view->address);
    M68kStepDataRegion current={0};
    if(!c->data_region(c->user,a,bytes,write,&current)||!current.data||
       current.address>0xffffffu||!current.bytes||current.bytes>0x1000000u-current.address||
       a<current.address||a-current.address>=current.bytes||bytes>current.bytes-(a-current.address))return NULL;
    *view=current;return view->data+(a-view->address);
}

static void fault(M68kStepContext *c,M68kStepResult reason,uint32_t address)
{
    data_end(c);
    c->state.fault=(uint8_t)reason;
    c->state.fault_address=address&0xFFFFFFu;
    if(c->fault_escape)longjmp(*(jmp_buf *)c->fault_escape,1);
}
static int retry(M68kStepContext *c,M68kStepBusResult result,uint32_t a,int write)
{
    if(result==M68K_BUS_OK)return 0;
    if(result!=M68K_BUS_WAIT){fault(c,M68K_STEP_BUS_ERROR,a);return 0;}
    unsigned active=data_suspend(c);
    int resumed=c->bus.wait?c->bus.wait(c->user,a,write):0;
    data_resume(c,active);
    if(!resumed) {
        fault(c,M68K_STEP_NO_WAIT_HANDLER,a);return 0;
    }
    return 1;
}
uint8_t m68k_step_read8(M68kStepContext *c,uint32_t a)
{
    uint8_t v=0;a&=0xFFFFFFu;
    if(!c->bus.read8){fault(c,M68K_STEP_BUS_ERROR,a);return 0;}
    uint8_t *p=data_pointer(c,a,1,0);if(p)return *p;
    M68kStepBusResult result;
    do {unsigned active=data_suspend(c);result=c->bus.read8(c->user,a,&v);data_resume(c,active);}while(retry(c,result,a,0));
    return v;
}
uint16_t m68k_step_read16(M68kStepContext *c,uint32_t a)
{
    uint16_t v=0;a&=0xFFFFFFu;
    if(a&1){fault(c,M68K_STEP_ADDRESS_ERROR,a);return 0;}
    if(!c->bus.read16){fault(c,M68K_STEP_BUS_ERROR,a);return 0;}
    uint8_t *p=data_pointer(c,a,2,0);if(p)return (uint16_t)(((unsigned)p[0]<<8)|p[1]);
    M68kStepBusResult result;
    do {unsigned active=data_suspend(c);result=c->bus.read16(c->user,a,&v);data_resume(c,active);}while(retry(c,result,a,0));
    return v;
}
uint32_t m68k_step_read32(M68kStepContext *c,uint32_t a)
{
    uint32_t hi=m68k_step_read16(c,a);
    return (hi<<16)|m68k_step_read16(c,a+2);
}
uint16_t m68k_step_fetch16(M68kStepContext *c,uint32_t a)
{
    uint16_t v=0;a&=0xFFFFFFu;
    if(a&1){fault(c,M68K_STEP_ADDRESS_ERROR,a);return 0;}
    if(!c->bus.fetch16){fault(c,M68K_STEP_BUS_ERROR,a);return 0;}
    M68kStepBusResult result;
    do {unsigned active=data_suspend(c);result=c->bus.fetch16(c->user,a,&v);data_resume(c,active);}while(retry(c,result,a,0));
    return v;
}
void m68k_step_write8(M68kStepContext *c,uint32_t a,uint8_t v)
{
    a&=0xFFFFFFu;
    if(!c->bus.write8){fault(c,M68K_STEP_BUS_ERROR,a);return;}
    uint8_t *p=data_pointer(c,a,1,1);if(p){*p=v;return;}
    M68kStepBusResult result;
    do {unsigned active=data_suspend(c);result=c->bus.write8(c->user,a,v);data_resume(c,active);}while(retry(c,result,a,1));
}
void m68k_step_write16(M68kStepContext *c,uint32_t a,uint16_t v)
{
    a&=0xFFFFFFu;
    if(a&1){fault(c,M68K_STEP_ADDRESS_ERROR,a);return;}
    if(!c->bus.write16){fault(c,M68K_STEP_BUS_ERROR,a);return;}
    uint8_t *p=data_pointer(c,a,2,1);if(p){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;return;}
    M68kStepBusResult result;
    do {unsigned active=data_suspend(c);result=c->bus.write16(c->user,a,v);data_resume(c,active);}while(retry(c,result,a,1));
}
void m68k_step_write32(M68kStepContext *c,uint32_t a,uint32_t v)
{
    m68k_step_write16(c,a,(uint16_t)(v>>16));
    m68k_step_write16(c,a+2,(uint16_t)v);
}

void m68k_step_init(M68kStepContext *c,const M68kStepBus *bus,void *user)
{
    memset(c,0,sizeof *c);
    if(bus)c->bus=*bus;
    c->user=user;
}
int m68k_step_code_span(M68kStepContext *c,M68kStepCodeSpan span)
{
    if(!c||c->fault_escape)return 0;
    c->code_span=span;return 1;
}
int m68k_step_data_regions(M68kStepContext *c,M68kStepDataResolver resolver)
{
    if(!c||c->fault_escape||c->data_active)return 0;
    data_end(c);c->data_region=resolver;return 1;
}
void m68k_step_set_sr(M68kStepContext *c,uint16_t value)
{
    uint16_t old=c->state.regs.SR;
    value&=MASK;
    if((old&S) && !(value&S)) {
        c->state.ssp=c->state.regs.A[7];
        c->state.regs.A[7]=c->state.regs.USP;
    } else if(!(old&S) && (value&S)) {
        c->state.regs.USP=c->state.regs.A[7];
        c->state.regs.A[7]=c->state.ssp;
    }
    c->state.regs.SR=value;
}
M68kStepResult m68k_step_reset(M68kStepContext *c)
{
    if(c->fault_escape)return M68K_STEP_REENTERED;
    data_end(c);
    memset(&c->state,0,sizeof c->state);
    c->state.regs.SR=0x2700;
    c->state.regs.A[7]=m68k_step_read32(c,0);
    if(c->state.fault)return (M68kStepResult)c->state.fault;
    c->state.ssp=c->state.regs.A[7];
    c->state.regs.PC=m68k_step_read32(c,4)&0xFFFFFFu;
    if(c->state.regs.PC&1)fault(c,M68K_STEP_ADDRESS_ERROR,c->state.regs.PC);
    return (M68kStepResult)c->state.fault;
}

static void enter_exception(M68kStepContext *c,unsigned vector,uint32_t pc)
{
    uint16_t old=c->state.regs.SR;
    m68k_step_set_sr(c,(uint16_t)((old|S)&~T));
    c->state.regs.A[7]-=4;m68k_step_write32(c,c->state.regs.A[7],pc);
    if(c->state.fault)return;
    c->state.regs.A[7]-=2;m68k_step_write16(c,c->state.regs.A[7],old);
    if(c->state.fault)return;
    c->state.regs.PC=m68k_step_read32(c,(vector&255)*4)&0xFFFFFFu;
    c->state.stopped=0;c->state.exception_vector=(uint8_t)vector;
}
void m68k_step_exception(M68kStepContext *c,unsigned vector,uint32_t pc,unsigned cycles)
{
    c->charge=cycles;
    enter_exception(c,vector,pc);
}
int m68k_step_supervisor(M68kStepContext *c)
{
    if(c->state.regs.SR&S)return 1;
    m68k_step_exception(c,8,c->state.instruction_pc,34);
    return 0;
}
void m68k_step_rte(M68kStepContext *c)
{
    if(!m68k_step_supervisor(c))return;
    uint32_t sp=c->state.regs.A[7];
    uint16_t sr=m68k_step_read16(c,sp);
    uint32_t pc=m68k_step_read32(c,sp+2);
    if(c->state.fault)return;
    c->state.regs.A[7]=sp+6;
    m68k_step_set_sr(c,sr);
    c->state.regs.PC=pc&0xFFFFFFu;
}
void m68k_step_stop(M68kStepContext *c,uint16_t sr)
{
    if(!m68k_step_supervisor(c))return;
    m68k_step_set_sr(c,sr);
    c->state.stopped=1;
}
void m68k_step_reset_devices(M68kStepContext *c)
{
    if(!m68k_step_supervisor(c))return;
    unsigned active=data_suspend(c);
    if(c->bus.reset_devices)c->bus.reset_devices(c->user);
    data_resume(c,active);
}
int m68k_step_interrupt(M68kStepContext *c,unsigned level)
{
    if(c->fault_escape || c->state.fault || level>7)return 0;
    unsigned previous=c->state.irq_pins;c->state.irq_pins=(uint8_t)level;
    if(!level || (level==7 ? previous==7 : level<=(((unsigned)c->state.regs.SR>>8)&7u)))return 0;
    unsigned active=data_suspend(c);
    unsigned vector=c->bus.ack_interrupt?c->bus.ack_interrupt(c->user,level):M68K_STEP_AUTOVECTOR;
    data_resume(c,active);
    if(vector==M68K_STEP_AUTOVECTOR)vector=24+level;
    else if(!vector)vector=15; /* uninitialized interrupt vector */
    enter_exception(c,vector,c->state.regs.PC);
    if(c->state.fault)return 0;
    c->state.regs.SR=(uint16_t)((c->state.regs.SR&~0x700)|(level<<8));
    c->state.cycles+=44;
    return 1;
}

M68kStepResult m68k_step_execute_register(M68kStepContext *c,const M68kStepEntry *entry)
{
    if(c->fault_escape)return M68K_STEP_REENTERED;
    if(c->state.fault)return (M68kStepResult)c->state.fault;
    if(c->state.stopped)return M68K_STEP_STOPPED;
    if(!entry||!entry->execute||!entry->cycles||!entry->word_count||entry->word_count>11||
       entry->pc!=c->state.regs.PC||!c->bus.fetch16||!c->code_span||
       (c->state.regs.SR&T))return M68K_STEP_MISS;
    uint32_t pc=entry->pc;size_t bytes=(size_t)entry->word_count*2u;
    if((pc&1u)||pc>0xffffffu||bytes>0x1000000u-pc)return M68K_STEP_MISS;
    const uint8_t *span=c->code_span(c->user,pc,bytes);
    if(!span)return M68K_STEP_MISS;
    for(unsigned n=0;n<entry->word_count;++n)
        if((((uint32_t)span[n*2u]<<8)|span[n*2u+1u])!=entry->words[n])return M68K_STEP_MISS;
    c->state.instruction_pc=pc;c->state.fault_opcode=entry->words[0];
    c->state.exception_vector=0;c->charge=entry->cycles;
    c->state.regs.PC=(pc+(uint32_t)bytes)&0xffffffu;
    entry->execute(c);
    c->state.cycles+=c->charge;++c->state.instructions;
    return M68K_STEP_OK;
}
M68kStepResult m68k_step_execute_span(M68kStepContext *c,const M68kStepRegisterSpan *span,uint64_t budget)
{
    if(c->fault_escape)return M68K_STEP_REENTERED;
    if(c->state.fault)return (M68kStepResult)c->state.fault;
    if(c->state.stopped)return M68K_STEP_STOPPED;
    if(!span || !span->entries || !span->execute || span->count<2 ||
       span->count>M68K_STEP_REGISTER_SPAN_MAX || !span->cycles || span->cycles>budget ||
       !c->code_span || !c->bus.fetch16 || (c->state.regs.SR&T))return M68K_STEP_MISS;
    uint32_t pc=c->state.regs.PC,next=pc;unsigned total=0;
    if((pc&1u) || pc>0xffffffu)return M68K_STEP_MISS;
    for(unsigned n=0;n<span->count;++n) {
        const M68kStepEntry *e=span->entries[n];
        if(!e || !e->execute || !e->cycles || !e->word_count || e->word_count>11 || e->pc!=next ||
           (unsigned)e->word_count*2u>0x1000000u-next)return M68K_STEP_MISS;
        total+=e->cycles;next+=(unsigned)e->word_count*2u;
    }
    if(total!=span->cycles)return M68K_STEP_MISS;
    const uint8_t *bytes=c->code_span(c->user,pc,next-pc);if(!bytes)return M68K_STEP_MISS;
    for(unsigned n=0;n<span->count;++n) {
        const M68kStepEntry *e=span->entries[n];unsigned offset=e->pc-pc;
        for(unsigned w=0;w<e->word_count;++w,offset+=2u)
            if((((unsigned)bytes[offset]<<8)|bytes[offset+1u])!=e->words[w])return M68K_STEP_MISS;
    }
    span->execute(c);
    const M68kStepEntry *last=span->entries[span->count-1u];
    c->state.regs.PC=next&0xffffffu;c->state.instruction_pc=last->pc;
    c->state.fault_opcode=last->words[0];c->state.exception_vector=0;c->charge=last->cycles;
    c->state.cycles+=span->cycles;c->state.instructions+=span->count;
    return M68K_STEP_OK;
}
static M68kStepResult execute_operation(M68kStepContext *c,const M68kStepEntry *entry,M68kStepOperation operation,int tracked)
{
    if(c->fault_escape)return M68K_STEP_REENTERED;
    if(c->state.fault)return (M68kStepResult)c->state.fault;
    if(c->state.stopped)return M68K_STEP_STOPPED;
    if(!operation || !c->bus.fetch16)return M68K_STEP_MISS;
    if(entry && (!entry->cycles || !entry->word_count || entry->word_count>11 ||
       entry->pc!=c->state.regs.PC))return M68K_STEP_MISS;
    data_end(c);
    jmp_buf escape;c->fault_escape=&escape;
    if(setjmp(escape)) {
        data_end(c);
        c->fault_escape=NULL;
        return (M68kStepResult)c->state.fault;
    }
    uint32_t pc=c->state.regs.PC;
    if(entry) {
        size_t bytes=(size_t)entry->word_count*2u;
        const uint8_t *span=c->code_span && !(pc&1u) && pc<=0xffffffu && bytes<=0x1000000u-pc?
            c->code_span(c->user,pc,bytes):NULL;
        for(unsigned n=0;n<entry->word_count;++n) {
            uint16_t word=span?(uint16_t)(((uint32_t)span[n*2u]<<8)|span[n*2u+1u]):
                               m68k_step_fetch16(c,pc+n*2u);
            if(word!=entry->words[n]){c->fault_escape=NULL;return M68K_STEP_MISS;}
        }
    }
    int trace=(c->state.regs.SR&T)!=0;
    c->state.instruction_pc=pc;
    c->state.fault_opcode=entry?entry->words[0]:m68k_step_fetch16(c,pc);
    c->state.exception_vector=0;c->charge=entry?entry->cycles:4;
    c->state.regs.PC=(pc+(entry?entry->word_count*2u:2u))&0xFFFFFFu;
    c->data_active=tracked&&c->data_region&&!trace;
    operation(c);
    data_end(c);
    if(trace && !c->state.exception_vector) {
        enter_exception(c,9,c->state.regs.PC);c->charge+=34;
    }
    c->state.cycles+=c->charge;++c->state.instructions;
    c->fault_escape=NULL;
    return M68K_STEP_OK;
}
M68kStepResult m68k_step_execute(M68kStepContext *c,const M68kStepEntry *entry)
{return execute_operation(c,entry,entry?entry->execute:NULL,0);}
M68kStepResult m68k_step_execute_tracked(M68kStepContext *c,const M68kStepEntry *entry)
{return execute_operation(c,entry,entry?entry->execute:NULL,1);}
M68kStepResult m68k_step_execute_live(M68kStepContext *c,M68kStepOperation operation)
{return execute_operation(c,NULL,operation,0);}
void m68k_step_unimplemented(M68kStepContext *c)
{fault(c,M68K_STEP_UNSUPPORTED,c->state.instruction_pc);}

void m68k_step_illegal(M68kStepContext *c)
{
    unsigned top=c->state.fault_opcode>>12;
    m68k_step_exception(c,top==10?10u:top==15?11u:4u,c->state.instruction_pc,34);
}

int m68k_step_module_valid(const M68kStepModule *m)
{
    if(!m || !m->name || !m->image_size || m->image_base>0xFFFFFFu ||
       m->image_size>0x1000000u-m->image_base || (m->count && !m->entries))return 0;
    for(size_t n=0;n<m->count;++n) {
        const M68kStepEntry *e=&m->entries[n];
        if(!e->execute || !e->word_count || e->word_count>11 || !e->cycles ||
           (e->pc&1u) || e->pc<m->image_base ||
           e->pc-m->image_base>=m->image_size ||
           e->word_count*2u>m->image_size-(e->pc-m->image_base) ||
           (n && m->entries[n-1].pc>=e->pc))return 0;
    }
    return 1;
}

M68kStepResult m68k_step_module_execute(M68kStepContext *c,const M68kStepModule *m)
{
    if(!m || !m->entries)return M68K_STEP_MISS;
    size_t lo=0,hi=m->count;
    while(lo<hi) {
        size_t mid=lo+(hi-lo)/2;
        uint32_t pc=m->entries[mid].pc;
        if(pc<c->state.regs.PC)lo=mid+1;
        else if(pc>c->state.regs.PC)hi=mid;
        else return m68k_step_execute(c,&m->entries[mid]);
    }
    return M68K_STEP_MISS;
}
