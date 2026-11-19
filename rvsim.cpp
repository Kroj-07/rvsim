// rvsim.cpp - Simulador de RISC-V RV32I (CS3051 Arquitectura de Computadores, HW#5)
//
// Compilar:  g++ -std=c++17 -O2 -Wall -Wextra -o rvsim rvsim.cpp
// Usar:      ./rvsim programa.bin      (binario crudo o ELF32)
//
// Estado arquitectural: PC + 32 registros + memoria plana byte-addressable (little-endian).
// Ciclo de ejecución:   fetch -> decode -> execute -> actualizar estado.

#include <array>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <unistd.h>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using i32 = int32_t;
using u64 = uint64_t;

static const char* ABI[32] = {"zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
                              "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
                              "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};

// Devuelve el índice 0..31 o -1 si el nombre no es un registro válido (x5, t0, a0, fp...).
static int parse_reg(const std::string& s) {
    if (s.size() >= 2 && s[0] == 'x') {
        char* end;
        long n = strtol(s.c_str() + 1, &end, 10);
        if (*end == 0 && n >= 0 && n < 32) return (int)n;
    }
    for (int i = 0; i < 32; i++)
        if (s == ABI[i]) return i;
    if (s == "fp") return 8;
    return -1;
}

// ---------------------------------------------------------------------------
// Memoria: espacio de 32 bits, byte-addressable, little-endian, dispersa por páginas de 4 KiB.
// Lo no escrito se lee como 0.

class Memory {
    std::unordered_map<u32, std::array<u8, 4096>> pages;

public:
    u8 read8(u32 a) const {
        auto it = pages.find(a >> 12);
        return it == pages.end() ? 0 : it->second[a & 0xFFF];
    }
    void write8(u32 a, u8 v) { pages[a >> 12][a & 0xFFF] = v; }  // la página nueva nace en ceros
    u16 read16(u32 a) const { return (u16)(read8(a) | (read8(a + 1) << 8)); }
    u32 read32(u32 a) const {
        return (u32)read8(a) | ((u32)read8(a + 1) << 8) | ((u32)read8(a + 2) << 16) |
               ((u32)read8(a + 3) << 24);
    }
    void write16(u32 a, u16 v) {
        write8(a, v & 0xFF);
        write8(a + 1, v >> 8);
    }
    void write32(u32 a, u32 v) {
        for (int i = 0; i < 4; i++) write8(a + i, (v >> (8 * i)) & 0xFF);
    }
    void clear() { pages.clear(); }
};

// ---------------------------------------------------------------------------
// Extracción de campos e inmediatos
static inline u32 opcode(u32 i) { return i & 0x7F; }
static inline u32 rd_of(u32 i) { return (i >> 7) & 0x1F; }
static inline u32 f3_of(u32 i) { return (i >> 12) & 0x7; }
static inline u32 rs1_of(u32 i) { return (i >> 15) & 0x1F; }
static inline u32 rs2_of(u32 i) { return (i >> 20) & 0x1F; }
static inline u32 f7_of(u32 i) { return i >> 25; }
static inline i32 imm_i(u32 i) { return (i32)i >> 20; }
static inline i32 imm_s(u32 i) { return ((i32)(i & 0xFE000000) >> 20) | (i32)((i >> 7) & 0x1F); }
static inline i32 imm_b(u32 i) {
    return ((i32)(i & 0x80000000) >> 19) | (i32)((i & 0x80) << 4) | (i32)((i >> 20) & 0x7E0) |
           (i32)((i >> 7) & 0x1E);
}
static inline i32 imm_u(u32 i) { return (i32)(i & 0xFFFFF000); }
static inline i32 imm_j(u32 i) {
    return ((i32)(i & 0x80000000) >> 11) | (i32)(i & 0xFF000) | (i32)((i >> 9) & 0x800) |
           (i32)((i >> 20) & 0x7FE);
}

static std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
static std::string fmt(const char* f, ...) {
    char buf[128];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

// ---------------------------------------------------------------------------
// Desensamblador
static std::string disasm(u32 i, u32 pc) {
    const char *rd = ABI[rd_of(i)], *r1 = ABI[rs1_of(i)], *r2 = ABI[rs2_of(i)];
    u32 f3 = f3_of(i), f7 = f7_of(i);
    switch (opcode(i)) {
        case 0x37: return fmt("lui %s, 0x%x", rd, (u32)imm_u(i) >> 12);
        case 0x17: return fmt("auipc %s, 0x%x", rd, (u32)imm_u(i) >> 12);
        case 0x6F:
            if (rd_of(i) == 0) return fmt("j 0x%x", pc + imm_j(i));
            return fmt("jal %s, 0x%x", rd, pc + imm_j(i));
        case 0x67:
            if (rd_of(i) == 0 && rs1_of(i) == 1 && imm_i(i) == 0) return "ret";
            return fmt("jalr %s, %d(%s)", rd, imm_i(i), r1);
        case 0x63: {
            static const char* n[8] = {"beq", "bne", "?", "?", "blt", "bge", "bltu", "bgeu"};
            if (!strcmp(n[f3], "?")) break;
            return fmt("%s %s, %s, 0x%x", n[f3], r1, r2, pc + imm_b(i));
        }
        case 0x03: {
            static const char* n[8] = {"lb", "lh", "lw", "?", "lbu", "lhu", "?", "?"};
            if (!strcmp(n[f3], "?")) break;
            return fmt("%s %s, %d(%s)", n[f3], rd, imm_i(i), r1);
        }
        case 0x23: {
            static const char* n[8] = {"sb", "sh", "sw", "?", "?", "?", "?", "?"};
            if (!strcmp(n[f3], "?")) break;
            return fmt("%s %s, %d(%s)", n[f3], r2, imm_s(i), r1);
        }
        case 0x13: {
            switch (f3) {
                case 0:
                    if (i == 0x00000013) return "nop";
                    return fmt("addi %s, %s, %d", rd, r1, imm_i(i));
                case 1: if (f7 == 0) return fmt("slli %s, %s, %u", rd, r1, rs2_of(i)); break;
                case 2: return fmt("slti %s, %s, %d", rd, r1, imm_i(i));
                case 3: return fmt("sltiu %s, %s, %d", rd, r1, imm_i(i));
                case 4: return fmt("xori %s, %s, %d", rd, r1, imm_i(i));
                case 5:
                    if (f7 == 0) return fmt("srli %s, %s, %u", rd, r1, rs2_of(i));
                    if (f7 == 0x20) return fmt("srai %s, %s, %u", rd, r1, rs2_of(i));
                    break;
                case 6: return fmt("ori %s, %s, %d", rd, r1, imm_i(i));
                case 7: return fmt("andi %s, %s, %d", rd, r1, imm_i(i));
            }
            break;
        }
        case 0x33: {
            const char* n = nullptr;
            if (f7 == 0) {
                static const char* t[8] = {"add", "sll", "slt", "sltu", "xor", "srl", "or", "and"};
                n = t[f3];
            } else if (f7 == 0x20) {
                if (f3 == 0) n = "sub";
                if (f3 == 5) n = "sra";
            }
            if (n) return fmt("%s %s, %s, %s", n, rd, r1, r2);
            break;
        }
        case 0x0F: return "fence";
        case 0x73:
            if (i == 0x00000073) return "ecall";
            if (i == 0x00100073) return "ebreak";
            break;
    }
    return fmt(".word 0x%08x  (inválida)", i);
}

// ---------------------------------------------------------------------------
// CPU
enum class Status { Running, Exited, Breakpoint /* ebreak */ };

struct Cpu {
    u32 pc = 0;
    u32 x[32] = {0};
    Memory mem;
    u64 steps = 0;
    Status status = Status::Running;
    int exit_code = 0;

    void reset() {
        pc = 0;
        std::memset(x, 0, sizeof x);
        steps = 0;
        status = Status::Running;
        exit_code = 0;
    }

    // Ejecuta una instrucción. Lanza std::runtime_error si es inválida (PC no cambia).
    void step() {
        if (status == Status::Exited) throw std::runtime_error("el programa ya terminó (usa 'reset')");
        status = Status::Running;
        u32 inst = mem.read32(pc);
        u32 next = pc + 4;
        u32 op = opcode(inst), f3 = f3_of(inst), f7 = f7_of(inst);
        u32 rd = rd_of(inst);
        u32 a = x[rs1_of(inst)], b = x[rs2_of(inst)];
        auto invalid = [&]() {
            throw std::runtime_error(fmt("instrucción inválida 0x%08x en pc=0x%08x", inst, pc));
        };

        switch (op) {
            case 0x37: set(rd, (u32)imm_u(inst)); break;                       // lui
            case 0x17: set(rd, pc + (u32)imm_u(inst)); break;                  // auipc
            case 0x6F: set(rd, pc + 4); next = pc + (u32)imm_j(inst); break;   // jal
            case 0x67: {                                                       // jalr
                if (f3 != 0) invalid();
                u32 t = (a + (u32)imm_i(inst)) & ~1u;
                set(rd, pc + 4);
                next = t;
                break;
            }
            case 0x63: {  // branches
                bool take;
                switch (f3) {
                    case 0: take = a == b; break;
                    case 1: take = a != b; break;
                    case 4: take = (i32)a < (i32)b; break;
                    case 5: take = (i32)a >= (i32)b; break;
                    case 6: take = a < b; break;
                    case 7: take = a >= b; break;
                    default: invalid(); return;
                }
                if (take) next = pc + (u32)imm_b(inst);
                break;
            }
            case 0x03: {  // loads
                u32 addr = a + (u32)imm_i(inst);
                switch (f3) {
                    case 0: set(rd, (u32)(i32)(int8_t)mem.read8(addr)); break;    // lb
                    case 1: set(rd, (u32)(i32)(int16_t)mem.read16(addr)); break;  // lh
                    case 2: set(rd, mem.read32(addr)); break;                     // lw
                    case 4: set(rd, mem.read8(addr)); break;                      // lbu
                    case 5: set(rd, mem.read16(addr)); break;                     // lhu
                    default: invalid();
                }
                break;
            }
            case 0x23: {  // stores
                u32 addr = a + (u32)imm_s(inst);
                switch (f3) {
                    case 0: mem.write8(addr, b & 0xFF); break;    // sb
                    case 1: mem.write16(addr, b & 0xFFFF); break; // sh
                    case 2: mem.write32(addr, b); break;          // sw
                    default: invalid();
                }
                break;
            }
            case 0x13: {  // OP-IMM
                u32 imm = (u32)imm_i(inst);
                u32 sh = rs2_of(inst);
                switch (f3) {
                    case 0: set(rd, a + imm); break;
                    case 1: if (f7 != 0) invalid(); set(rd, a << sh); break;
                    case 2: set(rd, (i32)a < (i32)imm); break;
                    case 3: set(rd, a < imm); break;
                    case 4: set(rd, a ^ imm); break;
                    case 5:
                        if (f7 == 0) set(rd, a >> sh);
                        else if (f7 == 0x20) set(rd, (u32)((i32)a >> sh));
                        else invalid();
                        break;
                    case 6: set(rd, a | imm); break;
                    case 7: set(rd, a & imm); break;
                }
                break;
            }
            case 0x33: {  // OP
                u32 sh = b & 0x1F;
                if (f7 == 0) {
                    switch (f3) {
                        case 0: set(rd, a + b); break;
                        case 1: set(rd, a << sh); break;
                        case 2: set(rd, (i32)a < (i32)b); break;
                        case 3: set(rd, a < b); break;
                        case 4: set(rd, a ^ b); break;
                        case 5: set(rd, a >> sh); break;
                        case 6: set(rd, a | b); break;
                        case 7: set(rd, a & b); break;
                    }
                } else if (f7 == 0x20 && f3 == 0) set(rd, a - b);
                else if (f7 == 0x20 && f3 == 5) set(rd, (u32)((i32)a >> sh));
                else invalid();
                break;
            }
            case 0x0F: break;  // fence: no-op
            case 0x73:
                if (inst == 0x00000073) ecall();
                else if (inst == 0x00100073) status = Status::Breakpoint;
                else invalid();
                break;
            default: invalid();
        }
        x[0] = 0;
        pc = next;
        steps++;
    }

private:
    void set(u32 r, u32 v) { if (r != 0) x[r] = v; }

    // Environment calls estilo SPIM (número en a7, argumento en a0).
    void ecall() {
        switch (x[17]) {
            case 1: std::cout << (i32)x[10]; break;                       // print int
            case 4: {                                                     // print string
                for (u32 p = x[10]; mem.read8(p); p++) std::cout << (char)mem.read8(p);
                break;
            }
            case 5: {                                                     // read int
                long long v = 0;
                std::cin >> v;
                x[10] = (u32)v;
                std::cin.ignore(1 << 20, '\n');
                break;
            }
            case 8: {                                                     // read string
                std::string s;
                std::getline(std::cin, s);
                u32 maxlen = x[11];
                u32 n = 0;
                while (maxlen > 1 && n < s.size() && n < maxlen - 1) { mem.write8(x[10] + n, s[n]); n++; }
                if (maxlen > 0) mem.write8(x[10] + n, 0);
                break;
            }
            case 10: status = Status::Exited; exit_code = 0; break;       // exit
            case 11: std::cout << (char)(x[10] & 0xFF); break;            // print char
            case 12: {                                                    // read char
                char c = 0;
                std::cin.get(c);
                x[10] = (u8)c;
                break;
            }
            case 17: status = Status::Exited; exit_code = (i32)x[10]; break;  // exit2
            case 34: std::cout << fmt("0x%08x", x[10]); break;            // print hex
            case 35: {                                                    // print binary
                for (int i = 31; i >= 0; i--) std::cout << ((x[10] >> i) & 1);
                break;
            }
            case 36: std::cout << x[10]; break;                           // print unsigned
            default:
                throw std::runtime_error(fmt("ecall no soportada: a7=%u en pc=0x%08x", x[17], pc));
        }
        std::cout.flush();
    }
};

// ---------------------------------------------------------------------------
// Carga de programas (binario crudo en 0x0 o ELF32 little-endian)
// ---------------------------------------------------------------------------
static void load_program(Cpu& cpu, const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("no se pudo abrir '" + path + "'");
    std::vector<u8> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    cpu.reset();
    cpu.mem.clear();

    if (data.size() >= 52 && data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F') {
        if (data[4] != 1 || data[5] != 1) throw std::runtime_error("ELF: se espera 32 bits little-endian");
        auto rd32 = [&](size_t o) { u32 v; std::memcpy(&v, &data[o], 4); return v; };
        auto rd16 = [&](size_t o) { u16 v; std::memcpy(&v, &data[o], 2); return v; };
        if (rd16(18) != 0xF3) throw std::runtime_error("ELF: la arquitectura no es RISC-V");
        u32 entry = rd32(24), phoff = rd32(28);
        u32 phentsize = rd16(42), phnum = rd16(44);
        for (u32 k = 0; k < phnum; k++) {
            size_t ph = phoff + (size_t)k * phentsize;
            if (ph + 32 > data.size()) throw std::runtime_error("ELF: cabecera de programa fuera de rango");
            if (rd32(ph) != 1) continue;  // solo PT_LOAD
            u32 off = rd32(ph + 4), vaddr = rd32(ph + 8), filesz = rd32(ph + 16);
            if ((size_t)off + filesz > data.size()) throw std::runtime_error("ELF: segmento fuera de rango");
            for (u32 j = 0; j < filesz; j++) cpu.mem.write8(vaddr + j, data[off + j]);
        }
        cpu.pc = entry;
        std::cout << "\"" << path << "\" (ELF) cargado a memoria. Entry = " << fmt("0x%08x", entry) << ".\n";
    } else {
        for (size_t j = 0; j < data.size(); j++) cpu.mem.write8((u32)j, data[j]);
        std::cout << "\"" << path << "\" cargado a memoria (" << data.size() << " bytes en 0x00000000).\n";
    }
}

// ---------------------------------------------------------------------------
// Interfaz de línea de comandos

static const char* HELP =
    "Comandos:\n"
    "  load <archivo>        cargar .bin (crudo, en 0x0) o .elf; reinicia la CPU\n"
    "  reset                 reiniciar PC y registros (la memoria se conserva)\n"
    "  step [n]  (s)         ejecutar n instrucciones (por defecto 1)\n"
    "  run [max] (r)         ejecutar hasta ebreak/exit/breakpoint/bucle infinito (max pasos, def. 10M)\n"
    "  pc                    mostrar el PC\n"
    "  regs [r ...]          mostrar registros (x5, t0, a0, pc...); sin argumentos, todos\n"
    "  mem <ini> [fin]       volcado de bytes [ini, fin] (inclusive)\n"
    "  memw <dir> [n]        mostrar n palabras de 32 bits desde dir\n"
    "  dis [dir] [n]         desensamblar n instrucciones (def. desde el PC, n=5)\n"
    "  break <dir> | unbreak <dir> | breaks   gestionar breakpoints\n"
    "  set <reg> <valor>     escribir un registro (o pc)\n"
    "  setb <dir> <byte> | setw <dir> <palabra>   escribir en memoria\n"
    "  trace on|off          imprimir cada instrucción ejecutada en 'run'\n"
    "  help | exit (quit, q)\n";

struct Shell {
    Cpu cpu;
    std::set<u32> breaks;
    bool trace = false;
    bool tty = isatty(STDIN_FILENO);

    static bool num(const std::string& s, u32& out) {
        char* end;
        errno = 0;
        long long v = strtoll(s.c_str(), &end, 0);
        if (s.empty() || *end || errno) return false;
        out = (u32)v;
        return true;
    }

    void show_exec(u32 pc_before, u32 inst) {
        std::cout << fmt("0x%08x:  %08x  ", pc_before, inst) << disasm(inst, pc_before) << "\n";
    }

    // Ejecuta una instrucción mostrando lo ejecutado. Devuelve false si debe parar.
    bool do_step(bool show) {
        u32 pc0 = cpu.pc, inst = cpu.mem.read32(pc0);
        cpu.step();
        if (show) show_exec(pc0, inst);
        return cpu.status == Status::Running;
    }

    void report_stop() {
        if (cpu.status == Status::Exited)
            std::cout << "\nPrograma terminó con código " << cpu.exit_code << " (ecall exit).\n";
        else if (cpu.status == Status::Breakpoint)
            std::cout << "ebreak alcanzado en pc=" << fmt("0x%08x", cpu.pc - 4) << ".\n";
    }

    void cmd_regs(const std::vector<std::string>& t) {
        auto one = [&](int i) { std::cout << fmt("x%-2d (%-4s) = 0x%08x  (%d)\n", i, ABI[i], cpu.x[i], (i32)cpu.x[i]); };
        if (t.size() == 1) {
            std::cout << fmt("pc = 0x%08x\n", cpu.pc);
            for (int i = 0; i < 32; i++) one(i);
            return;
        }
        for (size_t k = 1; k < t.size(); k++) {
            if (t[k] == "pc") { std::cout << fmt("pc = 0x%08x\n", cpu.pc); continue; }
            int r = parse_reg(t[k]);
            if (r < 0) std::cout << "registro desconocido: " << t[k] << "\n";
            else one(r);
        }
    }

    // Devuelve false si el usuario quiere salir.
    bool exec(const std::string& line) {
        std::istringstream is(line);
        std::vector<std::string> t;
        for (std::string w; is >> w;) t.push_back(w);
        if (t.empty()) return true;
        const std::string& c = t[0];
        try {
            if (c == "exit" || c == "quit" || c == "q") return false;
            else if (c == "help" || c == "h") std::cout << HELP;
            else if (c == "load" && t.size() >= 2) { load_program(cpu, t[1]); }
            else if (c == "reset") { cpu.reset(); std::cout << "CPU reiniciada.\n"; }
            else if (c == "pc") std::cout << fmt("pc = 0x%08x\n", cpu.pc);
            else if (c == "regs") cmd_regs(t);
            else if (c == "step" || c == "s") {
                u32 n = 1;
                if (t.size() > 1 && !num(t[1], n)) { std::cout << "n inválido\n"; return true; }
                for (u32 k = 0; k < n; k++)
                    if (!do_step(true)) { report_stop(); break; }
            } else if (c == "run" || c == "r") {
                u32 max = 10000000;
                if (t.size() > 1 && !num(t[1], max)) { std::cout << "max inválido\n"; return true; }
                u64 done = 0;
                bool first = true;
                while (done < max) {
                    if (!first && breaks.count(cpu.pc)) { std::cout << fmt("Breakpoint en 0x%08x.\n", cpu.pc); break; }
                    first = false;
                    u32 pc0 = cpu.pc;
                    bool go = do_step(trace);
                    done++;
                    if (!go) { report_stop(); break; }
                    if (cpu.pc == pc0) { std::cout << fmt("Bucle infinito en 0x%08x: el programa terminó.\n", pc0); break; }
                }
                if (done >= max) std::cout << "Límite de " << max << " pasos alcanzado.\n";
                std::cout << "(" << done << " instrucciones ejecutadas; total " << cpu.steps << ")\n";
            } else if (c == "mem" && t.size() >= 2) {
                u32 a, b;
                if (!num(t[1], a) || (t.size() > 2 ? !num(t[2], b) : (b = a, false))) { std::cout << "dirección inválida\n"; return true; }
                if (b < a) { std::cout << "rango inválido\n"; return true; }
                std::cout << fmt("Memoria (0x%x-0x%x):", a, b);
                for (u64 p = a; p <= b; p++) std::cout << fmt(" %02X", cpu.mem.read8((u32)p));
                std::cout << "\n";
            } else if (c == "memw" && t.size() >= 2) {
                u32 a, n = 1;
                if (!num(t[1], a) || (t.size() > 2 && !num(t[2], n))) { std::cout << "argumento inválido\n"; return true; }
                for (u32 k = 0; k < n; k++)
                    std::cout << fmt("0x%08x: 0x%08x  (%d)\n", a + 4 * k, cpu.mem.read32(a + 4 * k), (i32)cpu.mem.read32(a + 4 * k));
            } else if (c == "dis") {
                u32 a = cpu.pc, n = 5;
                if (t.size() > 1 && !num(t[1], a)) { std::cout << "dirección inválida\n"; return true; }
                if (t.size() > 2 && !num(t[2], n)) { std::cout << "n inválido\n"; return true; }
                for (u32 k = 0; k < n; k++) {
                    u32 p = a + 4 * k, i = cpu.mem.read32(p);
                    std::cout << (p == cpu.pc ? "=> " : "   ") << fmt("0x%08x:  %08x  ", p, i) << disasm(i, p) << "\n";
                }
            } else if (c == "break" && t.size() >= 2) {
                u32 a;
                if (!num(t[1], a)) { std::cout << "dirección inválida\n"; return true; }
                breaks.insert(a); std::cout << fmt("Breakpoint en 0x%08x.\n", a);
            } else if (c == "unbreak" && t.size() >= 2) {
                u32 a;
                if (!num(t[1], a)) { std::cout << "dirección inválida\n"; return true; }
                breaks.erase(a);
            } else if (c == "breaks") {
                for (u32 a : breaks) std::cout << fmt("0x%08x\n", a);
            } else if (c == "set" && t.size() >= 3) {
                u32 v;
                if (!num(t[2], v)) { std::cout << "valor inválido\n"; return true; }
                if (t[1] == "pc") cpu.pc = v;
                else {
                    int r = parse_reg(t[1]);
                    if (r < 0) { std::cout << "registro desconocido\n"; return true; }
                    if (r != 0) cpu.x[r] = v;
                }
            } else if ((c == "setb" || c == "setw") && t.size() >= 3) {
                u32 a, v;
                if (!num(t[1], a) || !num(t[2], v)) { std::cout << "argumento inválido\n"; return true; }
                if (c == "setb") cpu.mem.write8(a, v & 0xFF); else cpu.mem.write32(a, v);
            } else if (c == "trace" && t.size() >= 2) {
                trace = (t[1] == "on");
                std::cout << "trace " << (trace ? "activado" : "desactivado") << ".\n";
            } else std::cout << "comando desconocido o incompleto. Escribe 'help'.\n";
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n";
        }
        return true;
    }

    void repl() {
        std::string line;
        while (true) {
            if (tty) { std::cout << "> " << std::flush; }
            if (!std::getline(std::cin, line)) break;
            if (!tty) std::cout << "> " << line << "\n";  // eco para que las capturas de scripts sean legibles
            if (!exec(line)) break;
        }
        std::cout << "See you next time...\n";
        std::cout << "Program exited with code " << (cpu.status == Status::Exited ? cpu.exit_code : 0) << ".\n";
    }
};

int main(int argc, char** argv) {
    Shell sh;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") { std::cout << "uso: rvsim [programa.bin|programa.elf]\n" << HELP; return 0; }
        try { load_program(sh.cpu, a); } catch (const std::exception& e) { std::cerr << "Error: " << e.what() << "\n"; return 1; }
    }
    sh.repl();
    return sh.cpu.status == Status::Exited ? sh.cpu.exit_code : 0;
}
