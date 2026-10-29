/*
 * code_generator.h — 68K → C code emitter interface.
 */
#pragma once
#include <stdbool.h>
#include <stdio.h>
#include "m68k_decoder.h"
#include "rom_parser.h"
#include "function_finder.h"
#include "annotations.h"
#include "game_config.h"

/* Function bodies are distributed across a FIXED number of balanced-size
 * translation units so the build can compile them in parallel. CMake must
 * know all shard filenames at configure time (generation is a build step),
 * so this is a compile-time constant, not a runtime option: the emitter
 * always writes exactly this many <prefix>_partNN.c files (part00..part{N-1},
 * zero-padded to 2 digits), even if some end up empty for a small game. */
#define GENESIS_SPLIT_PART_COUNT 32

/* When reverse_debug is true, emitted C routes every m68k_write* through
 * sgrdb_write* (Tier-1 reverse debugger) and writes g_rdb_current_func =
 * 0xXXXXXXu; at the entry of every func_XXXXXX. The runner must be built
 * with -DSONIC_REVERSE_DEBUG=ON for the symbols to resolve. When false,
 * emission is byte-for-byte identical to the pre-Tier-1 baseline (modulo
 * the split-TU restructuring: function bodies are unchanged, only their
 * distribution across files and g_split_sp_popped's static->extern linkage
 * differ).
 *
 * Output, written to out_dir/:
 *   <prefix>_decls.h        — shared includes + forward decls (#pragma once)
 *   <prefix>_part00.c ..    — GENESIS_SPLIT_PART_COUNT function-body TUs
 *   <prefix>_part{N-1}.c      (part00 also defines g_split_sp_popped)
 * plus out_dispatch_path (unchanged: the dispatch table TU). */
bool codegen_emit(const GenesisRom *rom, const FunctionList *funcs,
                  const char *out_dir, const char *prefix,
                  const char *out_dispatch_path,
                  const AnnotationTable *at, const GameConfig *cfg,
                  bool reverse_debug);

/* Diagnostic: if set before codegen_emit, the final post-boundary-split
 * function-entry set is written (one hex addr per line) to `path`. Used by
 * the heuristic-coverage exercise (diff disasm-seeded vs pure-heuristic runs
 * vs the disasm label set). NULL (default) disables the dump. */
void codegen_set_dump_functions_path(const char *path);

/* Emit one straight-line instruction's native C semantics for an explicit-PC
 * step backend. The caller supplies CPU register/memory bindings and accounts
 * for cycles/PC. This shares the established
 * arithmetic/EA emitter without its function-level virtual return stack.
 * Returns false WITHOUT output for control, privileged or exception-producing
 * instructions; a step frontend must implement them or report a miss.
 * No widening/game hooks are emitted. Does not change legacy codegen defaults.
 */
bool codegen_emit_step_semantics(FILE *out, const GenesisRom *rom,
                                 const M68KInstr *instruction);

/* MC68000 operation for runtime/m68k_step. Bind g_cpu/memory to a context
 * parameter named `ctx`. The runtime supplies guarded instruction bytes,
 * the next PC and initial cycle cost. This adds real guest control transfers
 * and supervisor operations to the shared straight-line semantics. Unknown
 * operations return false, never a no-op. */
bool codegen_emit_step_operation(FILE *out, const GenesisRom *rom,
                                 const M68KInstr *instruction);
/* Closed single-memory-RMW specialization, using the SAME ordinary emitter
 * body as fallback. Only a complete dual-owned managed context view may latch
 * its EA and access bytes directly. Other bus/WAIT/fault paths stay ordinary.
 * Predecrement/postincrement updates remain in the selected original body.
 * Callers replacing archived callbacks must separately prove exact canonical
 * ordinary-body equivalence; an opcode/effect flag is not that proof. */
bool codegen_owned_rmw_instruction(const M68KInstr *);
bool codegen_emit_owned_rmw_operation(FILE *,const GenesisRom *,const M68KInstr *);
/* Initial manual-derived cost for an explicit-PC operation. Dynamic branches,
 * shifts and conditional sets override it in the emitted operation. Multiply/
 * divide still use documented nominal costs until independently qualified. */
unsigned codegen_step_nominal_cycles(const M68KInstr *instruction);

/* Closed, fixed-cycle register-only sequence ABI shared by both frontends.
 * No bus, control transfer, SR-mask change, callback or exception is admitted.
 * The caller proves the complete sequence lies before its next observable
 * boundary and guards current bytes where code is mutable. Output owns only
 * D/A/CCR locally and materializes them before returning to the caller; PC,
 * retirement diagnostics and grouped clocks remain the caller's contract.
 * Unknown instructions are ordinary-path exits. No output on rejection.
 */
#define CODEGEN_REGISTER_SPAN_MAX 8
bool codegen_register_instruction(const M68KInstr *, unsigned *flags_read,
                                  unsigned *flags_written);
bool codegen_emit_register_span(FILE *, const GenesisRom *, const M68KInstr *,
                                unsigned count);
/* Fixed-register prefix followed by a bus-free PC/variable-charge terminal.
 * Full ordinary-body/priority proof and worst-case observer admission belong
 * to the consumer. Existing fixed-span emission and ABI remain unchanged. */
bool codegen_flow_terminal(const M68KInstr *,unsigned *maximum_cycles);
bool codegen_emit_flow_span(FILE *,const GenesisRom *,const M68KInstr *,unsigned count);
