#!/bin/sh
# Ensambla tests/*.s -> .bin (crudo) y .elf. Requiere clang + ld.lld + llvm-objcopy
# (o la toolchain riscv64-unknown-elf-*; ajusta los comandos).
set -e
cd "$(dirname "$0")/tests"
for f in *.s; do
  n="${f%.s}"
  clang --target=riscv32 -march=rv32i -mabi=ilp32 -c "$f" -o "$n.o"
  ld.lld -m elf32lriscv -Ttext=0 -o "$n.elf" "$n.o"
  llvm-objcopy -O binary "$n.elf" "$n.bin"
done
