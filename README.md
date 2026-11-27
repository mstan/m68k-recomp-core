# m68k-recomp-core

Current licensing and the grant for existing owner contributions are documented in [LICENSE-SCOPE.md](LICENSE-SCOPE.md).

Shared clean-room Motorola 68000-family static-recompiler frontend used by
[segagenesisrecomp](https://github.com/mstan/segagenesisrecomp) and
[cdirecomp](https://github.com/mstan/cdirecomp).

The context-bound runtime also serves Saturn and 32X. Its bound-block API
shares instruction retirement, faults and trace handling with individual native
operations; callers retain code invalidation and device-observation boundaries.

The initial extraction deliberately preserves the two validated policy
profiles while centralizing the genuinely common instruction layer:

- `common/` contains the decoder, validator, annotation reader and shared
  original-68000 nominal timing table.
- `profiles/genesis/` preserves the mature plain-68000 Genesis discovery and
  code-emission policy.
- `profiles/scc68070/` preserves CD-i's SCC68070 timing, exception, OS-9 trap,
  async-resume, and guest-stack behavior.

This profile split is a reconciliation boundary, not a claim that duplicated
emitter/finder code is permanently desirable. Changes should first be made in
shared code when semantics are CPU-family-wide. Profile code is reserved for a
demonstrated CPU, generated-runtime-ABI, or platform-policy difference.

Consumers include `cmake/M68kRecompCore.cmake`, select a profile with
`m68k_recomp_core_sources()`, and provide their own `rom_parser.h`,
`game_config.h`, timing/oracle dependencies, CLI, and generated runtime.

The repository contains no Clown68000, ClownMDEmu, CeDImu, ROM, BIOS, disc, or
generated game code. Optional third-party instruction oracles remain in the
consumer development repositories and are not dependencies of this source
package.

The Genesis profile also exposes `codegen_emit_step_semantics()` for a consumer
with explicit PCs and independently scheduled CPU contexts. It emits the same
native arithmetic/effective-address C as the established function emitter, with
consumer-owned register/memory bindings, and omits virtual return-stack tracking,
widening and game hooks. Control, privilege and exception instructions return
false without output; the consumer must implement their CPU behavior or report
a miss. The caller owns PC/cycle accounting, CPU-model checks and writable-code
identity guards. `codegen_emit_step_operation()` adds explicit guest control
transfers, supervisor/stack operations and normal exceptions through
`runtime/m68k_step`; it requires a context parameter named `ctx`. It also emits
DIV and CHK exceptions. Unknown operations return false. Nonzero divide costs
currently use the manual's nominal maximum; operand-dependent timing still
needs qualification.

The shared DIVU emitter now rejects quotient 65536 as overflow rather than
truncating it to zero. Division by zero in the legacy function ABI reports its
unimplemented vector-five exception loudly instead of silently continuing.
The shared EA emitter also keeps byte accesses through A7 aligned by stepping
two bytes, and CMPM increments its source before reading its destination when
the two address registers alias. Both apply to established and step emission.
Memory CLR now performs the original 68000's discarded read before its zero
write, keeping each EA update single across the read/modify/write operation.
Register-count ROXL/ROXR use the current count register rather than an immediate
field. The consumer's native/floor/Unicorn matrix covers 4,992 register-shift
seeds; nominal charges are checked separately from the oracle, which supplies
architectural state rather than hardware cycle observations.

`runtime/m68k_step` provides a portable MC68000 execution context for those
generated operations: independent registers/stacks, physical memory callbacks,
resumable bus waits, instruction-byte guards, normal exception frames,
supervisor transitions, STOP/RTE/RESET, interrupt masks and level-seven edges,
trace and emulated cycle counters. It contains no instruction decoder or
interpreter. `m68k_recomp_step_sources()` and
`m68k_recomp_step_include_dirs()` expose it to source-package consumers.
The root CMake project also builds `m68k_step_runtime` and its synthetic tests.
`m68k_step_execute_live()` exposes the same WAIT/exception/trace boundary to a
consumer's current-byte correctness floor. Its callback owns decoding and
PC/charge changes; `m68k_step_fetch16()` reads the current mapped instruction
stream and `m68k_step_unimplemented()` reports unsupported semantics loudly.
No decoder or process-global CPU binding is added to the context runtime.
`m68k_recomp_decode_sources()` separately exposes decode/legality/nominal timing
services, including `common/m68k_timing.h`, to such consumers.

`M68kStepModule` registers sorted instruction entries with load-base/size/input
CRC metadata. Descriptor validation is explicit; execution binary-searches the
PC and checks all currently mapped instruction bytes. Competing modules at the
same PC can be tried after a miss. `m68k_step_bind.h` supplies generated-operation
bindings to the explicit `ctx` parameter, without a global or thread-local CPU.
The original-model `m68k_decode_68000()` preserves `$6xFF` as a one-word -1
branch and treats MOVEC as one-word illegal, while retaining the existing
family decoder for established consumers. Invalid JMP/JSR/LEA/PEA EAs now fail
the common validator. `codegen_step_nominal_cycles()` exposes manual-derived
initial costs; generated branches, register shifts and register Scc charge
their actual dynamic path/count.

This runtime currently stops and reports bus/address faults; construction of
MC68000 group-zero frames remains unimplemented. Snapshots must occur between
completed instructions. A complete step frontend, code registry/correctness
floor and system scheduler remain consumer work; the context tests do not
establish BIOS/disc boot or a completed Sega CD backend. Specification references:
[MC68000 User's Manual](https://www.nxp.com/docs/en/reference-manual/MC68000UM.pdf),
sections 6 and 8, and
[M68000 Programmer's Reference Manual](https://www.nxp.com/docs/en/reference-manual/M68000PRM.pdf).

## Reconciliation direction

1. Keep common decoder/validator semantics covered by synthetic tests from
   both consumers.
2. Replace `GenesisRom` with a callback-backed neutral image interface.
3. Move timing and exception behavior behind explicit CPU profiles.
4. Reconcile function discovery, retaining Genesis evidence tracking and
   CD-i's full 24-bit address-space/OS-9 requirements.
5. Reconcile the emitter around a neutral generated-runtime ABI and extension
   hooks, then remove the duplicated profile implementations.
6. Extract the clean-room fallback interpreter once its runtime callbacks are
   similarly explicit.

## License

For this project's code covered by the [PolyForm Noncommercial License](LICENSE),
[PERMISSIONS.md](PERMISSIONS.md) grants standing permission for voluntary
donations, sponsorships, and advertising revenue, including support for broader
development initiatives. Payment must not buy access to the software, games
built with it, or their updates, features, or game content, including early access.

Matthew Stanley has authority from the relevant copyright holders to grant
these permissions for the project's PolyForm-covered code. Third-party
components and code explicitly licensed under other terms retain those terms.
