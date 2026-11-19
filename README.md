# Simulador RISC-V RV32I — Tarea 5 (CS3051 Arquitectura de Computadores)

Simulador en **C++17** de un solo archivo (`rvsim.cpp`, ~570 líneas) que ejecuta código de máquina RV32I paso a paso, con inspección de PC, registros y memoria.

## 1. Compilar y usar

```bash
make                      # o: g++ -std=c++17 -O2 -Wall -Wextra -o rvsim rvsim.cpp
./rvsim tests/quicksort.bin
```

```
> run
> memw 0x1000 7        # los 7 enteros del arreglo, ya ordenados
> exit
```

Comandos (`help` los lista): `load`, `reset`, `step [n]`, `run [max]`, `pc`, `regs [r…]`, `mem ini [fin]`, `memw dir [n]`, `dis [dir] [n]`, `break/unbreak/breaks`, `set reg val`, `setb/setw`, `trace on|off`, `exit`.

## 2. Cómo funciona

**Estado arquitectural** = PC + 32 registros + memoria.

| Pieza | Implementación |
|---|---|
| Registros | `u32 x[32]`; `x0` se fuerza a 0 tras cada instrucción (`set()` además ignora escrituras a x0) |
| PC | `u32 pc`, empieza en `0x00000000` |
| Memoria | clase `Memory`: espacio de 32 bits, byte-addressable, **little-endian**. Se guarda en páginas de 4 KiB en un `unordered_map` (solo se reservan las páginas tocadas; lo no escrito vale 0) |

**Ciclo de `Cpu::step()`:**
1. **Fetch**: `inst = mem.read32(pc)`; `next = pc + 4`.
2. **Decode**: se extraen `opcode`, `rd`, `funct3`, `rs1`, `rs2`, `funct7` con desplazamientos y máscaras; los inmediatos (I, S, B, U, J) se reconstruyen según el formato, con **extensión de signo** (`(i32)x >> n`).
3. **Execute**: `switch` por opcode (`0x37` lui, `0x17` auipc, `0x6F` jal, `0x67` jalr, `0x63` branches, `0x03` loads, `0x23` stores, `0x13` op-imm, `0x33` op, `0x73` ecall/ebreak) y luego por `funct3`/`funct7`.
4. **Write-back**: se escribe `rd`, `pc = next` (o el destino del salto), `x[0] = 0`.

**Detalles que suelen fallar:**
- `lb/lh` extienden signo (`(int8_t)`), `lbu/lhu` no.
- `slt/slti` comparan con signo, `sltu/sltiu` sin signo (en `sltiu` el inmediato se extiende con signo y luego se compara como sin signo).
- `srai`/`sra` desplazan con signo (`(i32)a >> sh`); `srl/srli` sin signo. Se distinguen por `funct7 = 0x20`.
- Shifts por registro usan solo los 5 bits bajos de `rs2`.
- `jalr`: `target = (rs1 + imm) & ~1`, y `rd = pc+4` se escribe *después* de leer `rs1` (por si `rd == rs1`).
- Branches: offset relativo al PC **de la instrucción**, no a `pc+4`.
- Instrucción inválida → `std::runtime_error` con mensaje y PC; el estado no se modifica.

**Condiciones de parada de `run`:** `ebreak`, `ecall` de exit (10 / 17), breakpoint, **salto a sí mismo** (`beq x2,x2,done` / `j done`, el típico "fin" de los tests) o límite de pasos (10M por defecto).

**ecall estilo SPIM** (número en `a7`, argumento en `a0`): 1 print int, 4 print string, 5 read int, 8 read string, 10 exit, 11 print char, 12 read char, 17 exit2 (código en a0), 34 print hex, 35 print binary, 36 print unsigned.

**Extras implementados (ideas adicionales del enunciado):** ecall SPIM, carga de **ELF32** (`.elf`; el PC arranca en el entry point), **desensamblador** (`dis`, y cada `step` imprime la instrucción ejecutada), breakpoints y trace.
