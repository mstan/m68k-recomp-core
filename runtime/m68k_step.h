/* Explicit-PC MC68000 execution ABI for generated native instruction steps.
 * No decoder, interpreter, game, OS, host assembly or media is required here.
 * Semantics: Motorola MC68000UM section 6 and M68000PRM.
 */
#ifndef M68K_STEP_H
#define M68K_STEP_H
#include <stddef.h>
#include <stdint.h>

typedef enum { M68K_BUS_OK=1, M68K_BUS_WAIT=2, M68K_BUS_FAULT=3 } M68kStepBusResult;
typedef enum {
    M68K_STEP_OK=0, M68K_STEP_MISS, M68K_STEP_STOPPED,
    M68K_STEP_BUS_ERROR, M68K_STEP_ADDRESS_ERROR, M68K_STEP_NO_WAIT_HANDLER,
    M68K_STEP_REENTERED, M68K_STEP_UNSUPPORTED
} M68kStepResult;
enum { M68K_STEP_AUTOVECTOR=256 };

typedef struct {
    uint32_t D[8], A[8];
    uint16_t SR;
    uint32_t PC, USP;
} M68kStepRegisters;

/* Pure CPU state. No raw-struct portable serialization is promised. */
typedef struct {
    M68kStepRegisters regs;
    /* A7 is the active stack. SSP is its shadow while in user mode; USP is
     * the user-stack shadow while in supervisor mode. */
    uint32_t ssp;
    uint64_t cycles, instructions;
    uint32_t instruction_pc, fault_address;
    uint16_t fault_opcode;
    uint8_t stopped, irq_pins, fault, exception_vector;
} M68kStepState;

typedef struct {
    M68kStepBusResult (*read8)(void *,uint32_t,uint8_t *);
    M68kStepBusResult (*read16)(void *,uint32_t,uint16_t *);
    M68kStepBusResult (*write8)(void *,uint32_t,uint8_t);
    M68kStepBusResult (*write16)(void *,uint32_t,uint16_t);
    /* Side-effect-free instruction bytes from the CURRENT mapped program.
     * WAIT still means physical ownership must be resolved by the scheduler. */
    M68kStepBusResult (*fetch16)(void *,uint32_t,uint16_t *);
    /* Resolve one WAIT by advancing other CPUs/devices; zero means deadlock.
     * The active instruction remains on its own C stack, preserving partial
     * bus effects. Never restart a partially executed instruction on WAIT. */
    int (*wait)(void *,uint32_t,int);
    unsigned (*ack_interrupt)(void *,unsigned); /* vector or AUTOVECTOR */
    void (*reset_devices)(void *);
} M68kStepBus;

struct M68kStepEntry;
typedef struct M68kStepContext {
    M68kStepState state;
    M68kStepBus bus;
    void *user;
    /* Transient execution state. Snapshot only between completed instructions
     * (fault_escape == NULL); these fields are not serialized. */
    void *fault_escape;
    unsigned charge;
    /* Optional host-owned current program view. The callback returns a stable
     * byte range only when every ordinary fetch would succeed without WAIT or
     * side effects; NULL retains fetch16, including its faults/wait handling.
     * The view is consumed immediately, never cached across instructions.
     * Appended so existing generated operation prefix offsets stay unchanged. */
    const uint8_t *(*code_span)(void *,uint32_t,size_t);
    /* Optional host-owned code identity. 0 requests the live guard, +1 means
     * all current bytes match, -1 means mismatch. The host must cover every
     * writer/remap and never reuse identity across an unowned observer. */
    int (*code_guard)(void *,const struct M68kStepEntry *);
    /* Bracket unqualified callbacks, including faults and live execution.
     * The host suspends identity reuse during the callback and invalidates
     * direct/opaque writes on return. Not architectural/serialized state. */
    void (*code_observer)(void *,int entering);
} M68kStepContext;
#define M68K_STEP_HAS_CODE_SPANS 1
typedef const uint8_t *(*M68kStepCodeSpan)(void *,uint32_t,size_t);

typedef void (*M68kStepOperation)(M68kStepContext *);
typedef struct M68kStepEntry {
    uint32_t pc;
    uint16_t words[11];
    uint8_t word_count;
    uint16_t cycles;
    M68kStepOperation execute;
} M68kStepEntry;

/* Entries are strictly increasing by guest PC. Image identity is diagnostic;
 * execution validates current opcode AND extension identity (live bytes by
 * default; optional host versions must cover all writes/remaps/observers). */
typedef struct {
    const char *name;
    uint32_t image_base, image_size, image_crc32;
    const M68kStepEntry *entries;
    size_t count;
} M68kStepModule;

/* Optional immutable parallel effect flags; REGISTER or TRACKED_BUS below.
 * Old boolean arrays remain valid. Zero/absent is an arbitrary callback.
 * No change to old catalog or operation-object ABI. */
typedef struct {
    const M68kStepModule *module;
    const uint8_t *register_only;
} M68kStepEffectModule;

void m68k_step_init(M68kStepContext *,const M68kStepBus *,void *user);
/* Configure between completed operations. The ordinary fetch16 is still
 * required; unsupported ranges always retain the original portable path. */
int m68k_step_code_span(M68kStepContext *,M68kStepCodeSpan);
M68kStepResult m68k_step_reset(M68kStepContext *);
M68kStepResult m68k_step_execute(M68kStepContext *,const M68kStepEntry *);
/* Independently qualified operations may bypass opaque-callback fencing only
 * when all external accesses use this context's tracked bus/state helpers.
 * This does NOT bypass live guards unless the host supplies sound identity. */
enum { M68K_STEP_EFFECT_REGISTER=1, M68K_STEP_EFFECT_TRACKED_BUS=2 };
M68kStepResult m68k_step_execute_tracked(M68kStepContext *,const M68kStepEntry *);
#define M68K_STEP_HAS_CODE_IDENTITY 1
/* A region may use this only for independently qualified operations that
 * access registers/charge exclusively: no bus, callbacks, exceptions, stopped
 * state or external storage. Unknown operations retain m68k_step_execute.
 * A current, side-effect-free code_span is mandatory; MISS makes no change.
 * Live words, retirement diagnostics and variable charge remain exact. Trace
 * and reentry always retain the ordinary observable instruction boundary. */
M68kStepResult m68k_step_execute_register(M68kStepContext *,const M68kStepEntry *);
#define M68K_STEP_HAS_REGISTER_REGIONS 1
/* Runtime correctness floors use the same exception/WAIT/trace boundary.
 * The callback decodes current bytes and sets PC/charge; no stale entry or
 * process-global CPU binding is used. Unsupported semantics must halt. */
M68kStepResult m68k_step_execute_live(M68kStepContext *,M68kStepOperation);
void m68k_step_unimplemented(M68kStepContext *);
int m68k_step_module_valid(const M68kStepModule *);
M68kStepResult m68k_step_module_execute(M68kStepContext *,const M68kStepModule *);
/* Architectural illegal/A-line/F-line operation for generated illegal entries. */
void m68k_step_illegal(M68kStepContext *);
/* Update physical IRQ pins and accept an eligible autovectored/vectored IRQ.
 * Call only between instructions. Level 7 is edge triggered. */
int m68k_step_interrupt(M68kStepContext *,unsigned level);

uint8_t m68k_step_read8(M68kStepContext *,uint32_t);
uint16_t m68k_step_read16(M68kStepContext *,uint32_t);
uint16_t m68k_step_fetch16(M68kStepContext *,uint32_t);
uint32_t m68k_step_read32(M68kStepContext *,uint32_t);
void m68k_step_write8(M68kStepContext *,uint32_t,uint8_t);
void m68k_step_write16(M68kStepContext *,uint32_t,uint16_t);
void m68k_step_write32(M68kStepContext *,uint32_t,uint32_t);
void m68k_step_set_sr(M68kStepContext *,uint16_t);
int m68k_step_supervisor(M68kStepContext *);
void m68k_step_exception(M68kStepContext *,unsigned vector,uint32_t return_pc,unsigned total_cycles);
void m68k_step_rte(M68kStepContext *);
void m68k_step_stop(M68kStepContext *,uint16_t sr);
void m68k_step_reset_devices(M68kStepContext *);

static inline uint32_t m68k_step_brief_index_value(uint16_t extension,
                                                  uint32_t dreg,uint32_t areg)
{
    uint32_t value=(extension&0x8000u)?areg:dreg;
    return (extension&0x0800u)?value:(uint32_t)(int32_t)(int16_t)value;
}

#endif
