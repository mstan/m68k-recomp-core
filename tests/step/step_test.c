#include "m68k_step.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
typedef struct {uint8_t memory[0x2000];unsigned waits,ack,resets,ready;} Bus;
static M68kStepBusResult read8(void *u,uint32_t a,uint8_t *v)
{Bus*b=u;if(a>=sizeof b->memory)return M68K_BUS_FAULT;*v=b->memory[a];return M68K_BUS_OK;}
static M68kStepBusResult read16(void *u,uint32_t a,uint16_t *v)
{Bus*b=u;if(a==0x600 && !b->ready)return M68K_BUS_WAIT;if(a+1>=sizeof b->memory)return M68K_BUS_FAULT;*v=(uint16_t)(((unsigned)b->memory[a]<<8)|b->memory[a+1]);return M68K_BUS_OK;}
static M68kStepBusResult write8(void*u,uint32_t a,uint8_t v)
{Bus*b=u;if(a>=sizeof b->memory)return M68K_BUS_FAULT;b->memory[a]=v;return M68K_BUS_OK;}
static M68kStepBusResult write16(void*u,uint32_t a,uint16_t v)
{Bus*b=u;if(a+1>=sizeof b->memory)return M68K_BUS_FAULT;b->memory[a]=(uint8_t)(v>>8);b->memory[a+1]=(uint8_t)v;return M68K_BUS_OK;}
static int wait_bus(void*u,uint32_t a,int w){Bus*b=u;CHECK(a==0x600 && !w);++b->waits;b->ready=1;return 1;}
static unsigned ack(void*u,unsigned level){Bus*b=u;b->ack=level;return M68K_STEP_AUTOVECTOR;}
static void reset_devices(void*u){++((Bus*)u)->resets;}
static void set32(Bus*b,uint32_t a,uint32_t v){CHECK(write16(b,a,(uint16_t)(v>>16))==M68K_BUS_OK);CHECK(write16(b,a+2,(uint16_t)v)==M68K_BUS_OK);}
static void nop(M68kStepContext*c){(void)c;}
static void stop_op(M68kStepContext*c){m68k_step_stop(c,0x2300);}
static void rte_op(M68kStepContext*c){m68k_step_rte(c);}
static void read_wait(M68kStepContext*c){c->state.regs.D[0]=m68k_step_read16(c,0x600);}
static void bad_read(M68kStepContext*c){c->state.regs.D[0]=m68k_step_read16(c,0x601);c->state.regs.D[1]=0xBAD;}
static void reset_op(M68kStepContext*c){m68k_step_reset_devices(c);}
static void live_immediate(M68kStepContext*c)
{
    CHECK(c->state.fault_opcode==0x303C);++c->state.regs.D[1];
    uint16_t value=m68k_step_fetch16(c,c->state.instruction_pc+2);
    c->state.regs.D[0]=(c->state.regs.D[0]&0xFFFF0000u)|value;
    c->state.regs.PC=(c->state.instruction_pc+4)&0xFFFFFFu;c->charge=8;
}
static void unimplemented(M68kStepContext*c){m68k_step_unimplemented(c);c->state.regs.D[1]=0xBAD;}
static M68kStepEntry entry(uint32_t pc,uint16_t word,M68kStepOperation fn,unsigned cycles)
{M68kStepEntry e={0};e.pc=pc;e.words[0]=word;e.word_count=1;e.execute=fn;e.cycles=(uint16_t)cycles;return e;}

static void stop_at_device(M68kStepContext *context)
{
    ++context->state.regs.D[0];
    context->block_stop = 1;
}

static void check_bound_block(M68kStepContext *context)
{
    CHECK(m68k_step_reset(context) == M68K_STEP_OK);
    const M68kStepEntry first = entry(0x200, 0x4e71, nop, 4);
    const M68kStepEntry device = entry(0x202, 0x4e71, stop_at_device, 4);
    const M68kStepEntry last = entry(0x204, 0x4e71, nop, 4);
    const M68kStepEntry fault = entry(0x206, 0x4e71, bad_read, 4);
    const M68kStepEntry *index[0x104] = {0};
    index[0x100] = &first;
    index[0x101] = &device;
    index[0x102] = &last;
    context->bus.fetch16 = NULL;
    CHECK(m68k_step_execute_bound_block(context, index, 0x104, 20) == M68K_STEP_OK);
    CHECK(context->state.instructions == 2 && context->state.cycles == 8);
    CHECK(context->state.regs.PC == 0x204 && context->state.regs.D[0] == 1);

    CHECK(m68k_step_execute_bound_block(context, index, 0x104, 20) == M68K_STEP_MISS);
    CHECK(context->state.instructions == 3 && context->state.regs.PC == 0x206);
    index[0x103] = &fault;
    CHECK(m68k_step_execute_bound_block(context, index, 0x104, 20) == M68K_STEP_ADDRESS_ERROR);
    CHECK(context->state.instructions == 3 && context->state.instruction_pc == 0x206);
    CHECK(context->state.fault_address == 0x601 && !context->fault_escape);
}

int main(void)
{
    Bus b={0};M68kStepBus ops={read8,read16,write8,write16,read16,wait_bus,ack,reset_devices};M68kStepContext c;
    set32(&b,0,0x1000);set32(&b,4,0x200);set32(&b,8*4,0x400);set32(&b,9*4,0x500);set32(&b,28*4,0x700);set32(&b,31*4,0x800);
    m68k_step_init(&c,&ops,&b);CHECK(m68k_step_reset(&c)==M68K_STEP_OK);
    CHECK(c.state.regs.PC==0x200 && c.state.regs.A[7]==0x1000 && c.state.regs.SR==0x2700);
    /* User/supervisor stack banks, six-byte frame, RTE and privilege trap. */
    c.state.regs.USP=0x1800;m68k_step_set_sr(&c,1);CHECK(c.state.regs.A[7]==0x1800 && c.state.ssp==0x1000);
    c.state.instruction_pc=0x200;m68k_step_rte(&c);CHECK(c.state.regs.PC==0x400 && c.state.regs.A[7]==0xFFA && c.state.regs.USP==0x1800);
    CHECK(m68k_step_read16(&c,0xFFA)==1 && m68k_step_read32(&c,0xFFC)==0x200);
    m68k_step_rte(&c);CHECK(c.state.regs.PC==0x200 && c.state.regs.A[7]==0x1800 && c.state.regs.SR==1 && c.state.ssp==0x1000);
    m68k_step_set_sr(&c,0x2300);c.state.regs.PC=0x200;
    M68kStepEntry e=entry(0x200,0x4E71,nop,4);CHECK(write16(&b,0x200,0x4E71)==M68K_BUS_OK);
    CHECK(m68k_step_execute(&c,&e)==M68K_STEP_OK && c.state.regs.PC==0x202 && c.state.cycles==4);
    /* Byte guards reject changed code without CPU state/cycle mutation. */
    c.state.regs.PC=0x200;CHECK(write16(&b,0x200,0x7001)==M68K_BUS_OK);M68kStepState before=c.state;
    CHECK(m68k_step_execute(&c,&e)==M68K_STEP_MISS && !memcmp(&before,&c.state,sizeof before));
    CHECK(write16(&b,0x200,0x4E71)==M68K_BUS_OK);
    e.execute=stop_op;e.words[0]=0x4E72;e.words[1]=0x2300;e.word_count=2;
    CHECK(write16(&b,0x200,0x4E72)==M68K_BUS_OK && write16(&b,0x202,0x2300)==M68K_BUS_OK);
    CHECK(m68k_step_execute(&c,&e)==M68K_STEP_OK && c.state.stopped && c.state.regs.PC==0x204);
    CHECK(!m68k_step_interrupt(&c,2) && c.state.stopped);CHECK(m68k_step_interrupt(&c,4));
    CHECK(!c.state.stopped && c.state.regs.PC==0x700 && (c.state.regs.SR&0x700)==0x400 && b.ack==4);
    CHECK(m68k_step_read16(&c,c.state.regs.A[7])==0x2300 && m68k_step_read32(&c,c.state.regs.A[7]+2)==0x204);
    m68k_step_rte(&c);CHECK(c.state.regs.PC==0x204 && c.state.regs.SR==0x2300);
    /* Level seven accepts an edge even at mask seven, not a held level. */
    m68k_step_set_sr(&c,0x2700);CHECK(m68k_step_interrupt(&c,7));CHECK(!m68k_step_interrupt(&c,7));
    CHECK(!m68k_step_interrupt(&c,0));CHECK(m68k_step_interrupt(&c,7));
    e=entry(0x200,0x4E71,nop,4);CHECK(write16(&b,0x200,0x4E71)==M68K_BUS_OK);
    CHECK(m68k_step_reset(&c)==M68K_STEP_OK);m68k_step_set_sr(&c,0xA000);
    e.execute=nop;CHECK(m68k_step_execute(&c,&e)==M68K_STEP_OK);
    CHECK(c.state.regs.PC==0x500 && c.state.cycles==38 && c.state.exception_vector==9);
    CHECK(m68k_step_read16(&c,c.state.regs.A[7])==0xA000 && m68k_step_read32(&c,c.state.regs.A[7]+2)==0x202);
    /* WAIT resumes the active operation exactly once with physical bus state. */
    CHECK(m68k_step_reset(&c)==M68K_STEP_OK);e.execute=read_wait;CHECK(write16(&b,0x600,0x1234)==M68K_BUS_OK);
    CHECK(m68k_step_execute(&c,&e)==M68K_STEP_OK && b.waits==1 && c.state.regs.D[0]==0x1234);
    CHECK(m68k_step_reset(&c)==M68K_STEP_OK);b.ready=0;c.bus.wait=NULL;
    CHECK(m68k_step_execute(&c,&e)==M68K_STEP_NO_WAIT_HANDLER && c.state.regs.D[0]==0);
    c.bus.wait=wait_bus;b.ready=1;
    /* An unsupported physical fault halts loudly; no following operation runs.
     * MC68000 group-0 frame construction is deliberately not claimed here. */
    CHECK(m68k_step_reset(&c)==M68K_STEP_OK);e.execute=bad_read;
    CHECK(m68k_step_execute(&c,&e)==M68K_STEP_ADDRESS_ERROR && c.state.fault_address==0x601 && c.state.regs.D[1]==0);
    CHECK(m68k_step_reset(&c)==M68K_STEP_OK);e.execute=reset_op;CHECK(m68k_step_execute(&c,&e)==M68K_STEP_OK && b.resets==1);
    CHECK(m68k_step_reset(&c)==M68K_STEP_OK);m68k_step_set_sr(&c,0);e.execute=rte_op;
    CHECK(m68k_step_execute(&c,&e)==M68K_STEP_OK && c.state.exception_vector==8 && c.state.regs.PC==0x400 && c.state.cycles==34);
    /* A dynamic floor owns decoding, but shares the native WAIT/trace/fault
     * boundary. Changed extension bytes are fetched fresh on each operation. */
    CHECK(m68k_step_reset(&c)==M68K_STEP_OK);CHECK(write16(&b,0x200,0x303C)==M68K_BUS_OK);
    CHECK(write16(&b,0x202,0x1234)==M68K_BUS_OK);
    CHECK(m68k_step_execute_live(&c,live_immediate)==M68K_STEP_OK && c.state.regs.D[0]==0x1234 && c.state.regs.PC==0x204 && c.state.cycles==8);
    c.state.regs.PC=0x200;CHECK(write16(&b,0x202,0x5678)==M68K_BUS_OK);
    CHECK(m68k_step_execute_live(&c,live_immediate)==M68K_STEP_OK && c.state.regs.D[0]==0x5678 && c.state.regs.D[1]==2);
    CHECK(m68k_step_reset(&c)==M68K_STEP_OK);c.state.regs.PC=0x5FE;
    CHECK(write16(&b,0x5FE,0x303C)==M68K_BUS_OK);b.ready=0;unsigned waits_before=b.waits;
    CHECK(m68k_step_execute_live(&c,live_immediate)==M68K_STEP_OK && b.waits==waits_before+1 && c.state.regs.D[1]==1 && c.state.regs.PC==0x602);
    CHECK(m68k_step_reset(&c)==M68K_STEP_OK);m68k_step_set_sr(&c,0xA000);
    CHECK(m68k_step_execute_live(&c,live_immediate)==M68K_STEP_OK && c.state.cycles==42 && c.state.exception_vector==9);
    CHECK(m68k_step_read32(&c,c.state.regs.A[7]+2)==0x204);
    CHECK(m68k_step_reset(&c)==M68K_STEP_OK);
    CHECK(m68k_step_execute_live(&c,unimplemented)==M68K_STEP_UNSUPPORTED && !c.state.cycles && !c.state.instructions && !c.state.regs.D[1]);
    CHECK(c.state.fault_address==0x200 && c.state.fault_opcode==0x303C && !c.fault_escape);
    check_bound_block(&c);
    puts("Explicit-PC byte guards, CPU control/stacks, interrupts, trace, bus WAIT and bound blocks pass");
    return 0;
}
