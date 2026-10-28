#include "m68k_step.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
static uint8_t code[4]={0x70,0x01,0x12,0x34};
static unsigned fetched;
static M68kStepBusResult fetch(void *u,uint32_t a,uint16_t *v)
{(void)u;++fetched;if(a>2)return M68K_BUS_FAULT;*v=(uint16_t)(((unsigned)code[a]<<8)|code[a+1]);return M68K_BUS_OK;}
static const uint8_t *span(void *u,uint32_t a,size_t n)
{(void)u;return a<=4&&n<=4-a?code+a:NULL;}
static void operation(M68kStepContext *c)
{
    c->state.regs.D[0]=(c->state.regs.D[0]*1664525u)+1013904223u;
    c->state.regs.A[3]^=c->state.regs.D[0];
    c->state.regs.SR=(uint16_t)((c->state.regs.SR&0xffe0u)|(c->state.regs.D[0]&31u));
    c->charge=4u+(c->state.regs.D[0]&7u);c->state.regs.PC=0;
}
int main(void)
{
    M68kStepBus bus={0};bus.fetch16=fetch;
    M68kStepContext a,b; m68k_step_init(&a,&bus,NULL);m68k_step_init(&b,&bus,NULL);
    CHECK(m68k_step_code_span(&a,span)&&m68k_step_code_span(&b,span));
    M68kStepEntry entry={0,{0x7001,0x1234},2,4,operation};
    a.state.regs.SR=b.state.regs.SR=0x2700;
    for(unsigned i=0;i<20000;++i) {
        CHECK(m68k_step_execute(&a,&entry)==M68K_STEP_OK);
        CHECK(m68k_step_execute_register(&b,&entry)==M68K_STEP_OK);
        CHECK(!memcmp(&a.state,&b.state,sizeof a.state)&&a.charge==b.charge&&!b.fault_escape);
    }
    M68kStepState before=b.state;unsigned old=fetched;
    code[3]^=1;CHECK(m68k_step_execute_register(&b,&entry)==M68K_STEP_MISS);
    CHECK(!memcmp(&before,&b.state,sizeof before)&&fetched==old);code[3]^=1;
    b.code_span=NULL;CHECK(m68k_step_execute_register(&b,&entry)==M68K_STEP_MISS);
    CHECK(!memcmp(&before,&b.state,sizeof before)&&fetched==old);b.code_span=span;
    b.state.regs.SR|=0x8000u;before=b.state;
    CHECK(m68k_step_execute_register(&b,&entry)==M68K_STEP_MISS&&!memcmp(&before,&b.state,sizeof before));
    b.state.regs.SR&=0x7fffu;b.fault_escape=&b;
    CHECK(m68k_step_execute_register(&b,&entry)==M68K_STEP_REENTERED);b.fault_escape=NULL;
    b.state.stopped=1;CHECK(m68k_step_execute_register(&b,&entry)==M68K_STEP_STOPPED);
    puts("register-region exact retirement, live extension guards and observable exits pass");return 0;
}
