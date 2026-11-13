/* Exceed MSVCRT's per-process TMP_MAX while alternating emission lengths.
 * Function-body emission may call the operation scratch; RMW and register
 * spans themselves are nonnested. Rejected operations must not consume the
 * following valid emission or leave a longer previous suffix in it. */
#include "code_generator.h"
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %u iteration %u: %s\n",(unsigned)__LINE__,iteration,#x);exit(1); } } while (0)
static unsigned iteration;
static const unsigned char image[]={0x52,0x58,0x42,0x39,0,0,0,0x54,
    0x70,0xff,0x72,0,0x74,1,0x30,1,0x12,0,0x24,1,0x52,0x40,0x53,0x81};
int main(void)
{
    GenesisRom rom={0};rom.rom_data=(uint8_t *)image;rom.rom_size=sizeof image;
    M68KInstr rmw[2],span[8];
    CHECK(m68k_decode_68000(&rom,0,&rmw[0]));CHECK(m68k_decode_68000(&rom,2,&rmw[1]));
    unsigned pc=8;
    for(unsigned n=0;n<8;++n) { CHECK(m68k_decode_68000(&rom,pc,&span[n]));pc+=span[n].byte_length; }
    FILE *out=tmpfile();CHECK(out);
    char expected[4][8192];long lengths[4]={0};
    for(iteration=0;iteration<70004;++iteration) {
        unsigned kind=iteration%4;CHECK(!fseek(out,0,SEEK_SET));
        // Unsupported operations are rejected before borrowing scratch.
        CHECK(!codegen_emit_owned_rmw_operation(out,&rom,&span[0]));
        CHECK(!codegen_emit_register_span(out,&rom,span,1));CHECK(ftell(out)==0);
        if(kind<2)CHECK(codegen_emit_owned_rmw_operation(out,&rom,&rmw[kind]));
        else CHECK(codegen_emit_register_span(out,&rom,span,kind==2?8:2));
        CHECK(!fflush(out));long length=ftell(out);CHECK(length>0&&length<8192);
        char body[8192];CHECK(!fseek(out,0,SEEK_SET));CHECK(fread(body,1,(size_t)length,out)==(size_t)length);
        if(iteration<4) { lengths[kind]=length;memcpy(expected[kind],body,(size_t)length); }
        else { CHECK(lengths[kind]==length);CHECK(!memcmp(expected[kind],body,(size_t)length)); }
    }
    CHECK(lengths[2]>lengths[3]); // long span immediately followed by short
    CHECK(!fclose(out));
    puts("70004 varying RMW/span emissions exact beyond TMP_MAX; long-to-short and rejection controls pass");return 0;
}
