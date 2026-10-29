#include "m68k_step.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
typedef struct {
    uint8_t memory[512],banked[2][32];
    unsigned bank,reads,writes,fetches,resolves,waits,wait_once,fault_second,malformed;
    unsigned nested;
    uint32_t observed;
    M68kStepContext *cpu;
} Fixture;
static void flip(Fixture *f)
{
    if(!f->nested){f->bank^=1;return;}
    uint32_t first=m68k_step_read16(f->cpu,0x100);
    f->bank^=1;
    f->observed=(first<<16)|m68k_step_read16(f->cpu,0x100);
}
static uint8_t *byte(Fixture *f,uint32_t a)
{return a>=0x100&&a<0x120?&f->banked[f->bank][a-0x100]:&f->memory[a&511u];}
static M68kStepBusResult read8(void *u,uint32_t a,uint8_t *v)
{Fixture*f=u;++f->reads;*v=*byte(f,a);return M68K_BUS_OK;}
static M68kStepBusResult read16(void *u,uint32_t a,uint16_t *v)
{
    Fixture*f=u;++f->reads;
    if(a==0x110&&f->fault_second)return M68K_BUS_FAULT;
    if(a==0x110&&f->wait_once){f->wait_once=0;return M68K_BUS_WAIT;}
    if(a==0x200){flip(f);*v=0;return M68K_BUS_OK;}
    *v=(uint16_t)(((unsigned)*byte(f,a)<<8)|*byte(f,a+1));return M68K_BUS_OK;
}
static M68kStepBusResult write8(void *u,uint32_t a,uint8_t v)
{Fixture*f=u;++f->writes;*byte(f,a)=v;return M68K_BUS_OK;}
static M68kStepBusResult write16(void *u,uint32_t a,uint16_t v)
{
    Fixture*f=u;++f->writes;
    if(a==0x110&&f->fault_second)return M68K_BUS_FAULT;
    if(a==0x110&&f->wait_once){f->wait_once=0;return M68K_BUS_WAIT;}
    if(a==0x200){flip(f);return M68K_BUS_OK;}
    *byte(f,a)=(uint8_t)(v>>8);*byte(f,a+1)=(uint8_t)v;return M68K_BUS_OK;
}
static M68kStepBusResult fetch16(void *u,uint32_t a,uint16_t *v)
{Fixture*f=u;++f->fetches;if(a==0x200)flip(f);*v=a==0?0x4e71:0;return M68K_BUS_OK;}
static int wait_bus(void *u,uint32_t a,int write)
{Fixture*f=u;(void)a;(void)write;++f->waits;flip(f);return 1;}
static void reset_bus(void *u){flip(u);}
static int resolve(void *u,uint32_t a,size_t bytes,int write,M68kStepDataRegion *out)
{
    Fixture*f=u;(void)write;++f->resolves;
    if(a<0x100||a>=0x110||bytes>0x110-a)return 0;
    out->address=f->malformed?0xffffffu:0x100;
    out->bytes=f->malformed?0x1000000u:16;
    out->data=f->banked[f->bank];return 1;
}
static unsigned mode;
static void operation(M68kStepContext *c)
{
    if(mode==0) {
        c->state.regs.D[0]=m68k_step_read32(c,0x100)^m68k_step_read32(c,0x104);
        m68k_step_write32(c,0x108,c->state.regs.D[0]);
    } else if(mode==1) {
        c->state.regs.D[0]=m68k_step_read16(c,0x100);
        (void)m68k_step_read16(c,0x200);
        c->state.regs.D[1]=m68k_step_read16(c,0x100);
    } else if(mode==2)c->state.regs.D[0]=m68k_step_read32(c,0x10e);
    else if(mode==3)m68k_step_write32(c,0x10e,0x12345678);
    else if(mode==4) {
        c->state.regs.D[0]=m68k_step_read16(c,0x100);
        m68k_step_reset_devices(c);
        c->state.regs.D[1]=m68k_step_read16(c,0x100);
    } else if(mode==5) {
        c->state.regs.D[0]=m68k_step_read16(c,0x100);
        CHECK(!m68k_step_data_regions(c,NULL));
        m68k_step_write16(c,0x200,0);
        c->state.regs.D[1]=m68k_step_read16(c,0x100);
    } else if(mode==6) {
        c->state.regs.D[0]=m68k_step_read8(c,0x100);
        m68k_step_write8(c,0x102,0x55);
        c->state.regs.D[1]=m68k_step_read8(c,0x103);
    } else if(mode==7)c->state.regs.D[0]=m68k_step_read32(c,0xfffffe);
    else if(mode==8)c->state.regs.D[0]=m68k_step_read16(c,0x101);
    else {
        c->state.regs.D[0]=m68k_step_read16(c,0x100);
        (void)m68k_step_fetch16(c,0x200);
        c->state.regs.D[1]=m68k_step_read16(c,0x100);
    }
}
static const M68kStepEntry entry={0,{0x4e71},1,20,operation};
static void setup(Fixture*f,M68kStepContext*c)
{
    memset(f,0,sizeof *f);
    for(unsigned b=0;b<2;++b)for(unsigned n=0;n<32;++n)f->banked[b][n]=(uint8_t)(b*113u+n*7u);
    M68kStepBus bus={read8,read16,write8,write16,fetch16,wait_bus,NULL,reset_bus};
    m68k_step_init(c,&bus,f);f->cpu=c;c->state.regs.SR=0x2700;c->state.regs.A[7]=0x180;
}
int main(void)
{
    unsigned cases=0;
    for(mode=0;mode<10;++mode)for(unsigned wait=0;wait<2;++wait)
    for(unsigned fault=0;fault<2;++fault)for(unsigned trace=0;trace<2;++trace)
    for(unsigned malformed=0;malformed<2;++malformed)for(unsigned nested=0;nested<2;++nested) {
        Fixture a,b;M68kStepContext x,y;setup(&a,&x);setup(&b,&y);
        a.wait_once=b.wait_once=wait;a.fault_second=b.fault_second=fault;b.malformed=malformed;
        a.nested=b.nested=nested;
        if(trace)x.state.regs.SR=y.state.regs.SR=0xa700;
        CHECK(m68k_step_data_regions(&y,resolve));
        M68kStepResult expected=m68k_step_execute(&x,&entry),actual=m68k_step_execute_tracked(&y,&entry);
        CHECK(expected==actual&&!memcmp(&x.state,&y.state,sizeof x.state));
        CHECK(!memcmp(a.memory,b.memory,sizeof a.memory)&&!memcmp(a.banked,b.banked,sizeof a.banked));
        CHECK(a.bank==b.bank&&a.waits==b.waits&&a.wait_once==b.wait_once);
        CHECK(a.observed==b.observed);
        CHECK(!y.data_active&&!y.data_read.data&&!y.data_write.data&&!y.fault_escape);
        if(mode==0&&!trace&&!malformed)CHECK(b.resolves==2&&b.reads==0&&b.writes==0);
        if(trace)CHECK(!b.resolves);
        ++cases;
    }
    /* Ordinary HLE/live execution never inherits or creates a data lease. */
    Fixture f;M68kStepContext c;setup(&f,&c);mode=0;
    CHECK(m68k_step_data_regions(&c,resolve));
    CHECK(m68k_step_execute(&c,&entry)==M68K_STEP_OK&&!f.resolves&&f.reads==4&&f.writes==2);
    CHECK(m68k_step_execute_live(&c,operation)==M68K_STEP_OK&&!f.resolves);
    printf("Owned data regions: %u matched CPU/memory/fault/WAIT/MMIO/reset/trace cases pass\n",cases);
    return 0;
}
