# Port RIOT OS na procesor Gaisler NOEL-V (rv64) na platformie ZedBoard FPGA

**Autor:** Matvii Ivashchenko  
**Data:** 2026  

---

## Spis tresci

1. [Przeglad architektury sprzetowej](#1-przeglad-architektury-sprzetowej)
2. [Struktura portu w drzewie RIOT](#2-struktura-portu-w-drzewie-riot)
3. [Sekwencja startu systemu](#3-sekwencja-startu-systemu)
4. [Zarzadzanie kontekstem watkow](#4-zarzadzanie-kontekstem-watkow)
5. [Obsluga przerwan i pulapek](#5-obsluga-przerwan-i-pulapek)
6. [Sterownik timera - ACLINT/CLINT](#6-sterownik-timera---aclintclint)
7. [Sterownik UART - APBUART](#7-sterownik-uart---apbuart)
8. [Sterownik GPIO - GRGPIO](#8-sterownik-gpio---grgpio)
9. [Konfiguracja pamieci i skrypt linkera](#9-konfiguracja-pamieci-i-skrypt-linkera)
10. [System przerwan - PLIC](#10-system-przerwan---plic)
11. [Konfiguracja lancucha narzedzi i flag kompilacji](#11-konfiguracja-lancucha-narzedzi-i-flag-kompilacji)
12. [Rozmiary stosow i rv64](#12-rozmiary-stosow-i-rv64)
13. [Napotkane problemy i ich rozwiazania](#13-napotkane-problemy-i-ich-rozwiazania)
14. [Przykladowa aplikacja testowa](#14-przykladowa-aplikacja-testowa)
15. [Procedura wgrywania i debugowania przez GRMON](#15-procedura-wgrywania-i-debugowania-przez-grmon)

---

## 1. Przeglad architektury sprzetowej

### 1.1 Procesor NOEL-V

NOEL-V to procesor RISC-V opracowany przez firme Frontgrade Gaisler (dawniej Cobham Gaisler), implementowany jako logika konfiguracyjna na FPGA. 

W konfiguracji uzytej w tym porcie procesor uruchamia sie w trybie **rv64imac**.

Pelny zestaw rozszerzen wylistowany przez modul debug (dm0 w grmon):

```
rv64imafdcb, Modes M S U, SV39
i, m, a, f, d, c, smdbltrp, smepmp, smrnmi, smstateen,
sscofpmf, ssdbltrp, svadu, svinval, zaamo, zalrsc,
zba, zbb, zbs, zca, zcd, zicbom, zicfilp, zicfiss,
zicntr, zicond, zifencei, zihpm, zimop
```

Kompilacja jest jednak ograniczona do rv64imac ze wzgledu na konfiguracje lancucha narzedzi.

### 1.2 Platforma ZedBoard i interfejs GRLIB

ZedBoard (Digilent) to plyta rozwojowa oparta na ukladzie Xilinx Zynq-7000 SoC (XC7Z020). Na platfomie tej biegnie referencyjna konfiguracja NoeLV jako bitstream FPGA, ladowany przez Vivado. Kontroler DDR3 (MIG) zapewnia 256 MB pamieci RAM dostepnej od adresu `0x00000000`.

Infrastruktura GRLIB dostarcza standardowych IP-corów:

| Nazwa | Typ | Adres bazowy | Opis |
|-------|-----|-------------|------|
| `mig0` | AHB | `0x00000000` | Kontroler Xilinx MIG (DDR3, 256 MB) |
| `clint0` | AHB | `0xe0000000` | RISC-V ACLINT (timer + IPI) |
| `plic0` | AHB | `0xf8000000` | RISC-V PLIC (31 zrodel, 4 konteksty, 7 priorytetow) |
| `uart0` | APB | `0xff900000` | Gaisler APBUART (Generic UART) |
| `gptimer0` | APB | `0xff908000` | Modular Timer Unit (timer pomocniczy) |
| `gpio0` | APB | `0xff983000` | Gaisler GRGPIO (GPIO controller) |
| `ahbstat0` | APB | `0xff982000` | AHB Status Register |
| `ahbuart0` | APB | `0xff986000` | AHB Debug UART (uzywany przez GRMON) |
| `dm0` | AHB | `0xfe000000` | RISC-V Debug Module |

### 1.3 Czestotliwosc taktowania

AHB clock wynosi **91 MHz**. Jest to czestotliwosc z ktorej wyprowadzane sa wszystkie zegarowanie peryferiow:

- CPU core: 91 MHz
- APBUART scaler: `SYSCLK / (BAUD * 8) - 1`
- ACLINT MTIME: 1 MHz (podzielnik = 91, taktuje timerek systemowy RIOT)

Wartosc `CLOCK_CORECLOCK = 91000000UL` zdefiniowana jest w `boards/zedboard-noelv/include/board.h`.

### 1.4 Interfejs debugowania i UART

GRMON4 komunikuje sie z procesorem przez interfejs **JTAG** (ahbjtag0) lub **AHB Debug UART** (ahbuart0 na `0xff986000`, 115200 baud). GRMON korzysta z protokolu AHB Trace i ma bezposredni dostep do rejestrow APBUART, wiec dane z `puts()` i `printf()` sa widoczne w konsoli GRMON nawet bez fizycznego polaczenia z gniazdem UART na plycie.

Funkcja `uart_write()` w sterowniku zapisuje bajty bezposrednio do rejestru DATA rejestru APBUART0 na `0xff900000`. GRMON przechwyca te zapisy i wyswietla je w swojej konsoli.

---

## 2. Struktura portu w drzewie RIOT

```
RIOT/
- cpu/noelv/                        # Definicja CPU
-   Makefile                        # Buduje modul "cpu", wywoluje periph/
-   Makefile.features               # Lista dostarczanych featureów
-   Makefile.include                # Flagi kompilatora, lancuch narzedzi
-   Makefile.dep                    # Zaleznosci modulow
-   Kconfig                         # Konfigjracja Kconfig
-   cpu.c                           # cpu_init(): riscv_init + periph_init
-   riscv_init.c                    # riscv_init(): FPU, IRQ, PMP
-   start.S                         # Punkt wejscia _start, inicjalizacja BSS/data
-   irq_arch.c                      # trap_entry (ASM), handle_trap (C)
-   thread_arch.c                   # thread_stack_init, cpu_switch_context_exit
-   context_frame.c                 # Weryfikacja offsetow ramki kontekstu
-   panic.c                         # core_panic: petla busy
-   ldscripts/                      # Skrypty linkera (wspolne RISC-V)
-   include/
-       cpu_conf.h                  # CLINT/PLIC adresy, RTC_FREQ
-       cpu_conf_common.h           # Rozmiary stosow, IRQ_API_INLINED
-       context_frame.h             # struct context_switch_frame, offsety
-       vendor/
-           riscv_csr.h             # CSR makra, MCAUSE_INT (rv64)
-           apbuart.h               # APBUART rejestry i bity
-           grgpio.h                # GRGPIO struct i makra
-           clint.h                 # CLINT offsety MTIME/MTIMECMP
-           plic.h                  # PLIC rejestry
-   periph/
-       Makefile
-       uart.c                      # Sterownik APBUART
-       gpio.c                      # Sterownik GRGPIO
-       coretimer.c                 # Sterownik timera (ACLINT MTIME)
-       plic.c                      # Kontroler przerwan PLIC
-       clic.c                      # (nieuzywane w tej konfiguracji)
-       pmp.c                       # Physical Memory Protection
-
- boards/zedboard-noelv/            # Definicja plyty
-   Makefile                        # Buduje modul "board"
-   Makefile.features               # CPU=noelv, CPU_CORE=rv64imac
-   Makefile.include                # Mapa pamieci, PROGRAMMER=grmon
-   Kconfig
-   board.c                         # board_init(): inicjalizacja GPIO
-   include/
-       board.h                     # CLOCK_CORECLOCK, LED_PIN, BTN_PIN
-       periph_conf.h               # GPIO0_BASE_ADDR, uart_config[]
-
- examples/noelv_test/              # Przykladowa aplikacja testowa
-   Makefile
-   main.c
```

### 2.1 Hierarchia dziedziczenia

Port `noelv` korzysta z wspolnego kodu RISC-V w `cpu/riscv_common/`. Pliki z `riscv_common` sa dolaczane przez `Makefile.include` przez:

```makefile
INCLUDES += -I$(RIOTCPU)/riscv_common/include
```

Dzieki temu mozna reuzywac implementacji PLIC, CLIC, PMP, atomics i innych komponentow wspolnych dla wszystkich portow RISC-V w RIOT.

---

## 3. Sekwencja startu systemu

### 3.1 Punkt wejscia - `start.S`

Po zaladowaniu ELF przez GRMON procesor zaczyna wykonywac kod od adresu `_start` w sekcji `.init`. Kolejnosc operacji:

```
_start:
    csrc mstatus, MSTATUS_MIE    ; wylacz przerwania globalne
    lui/jalr -> _start_real      ; dlugi skok (pozycja niezalezna)

_start_real:
    la gp, __global_pointer$     ; inicjalizacja rejestru globalnego
    la sp, _sp                   ; ustawienie poczatkowego wskaznika stosu

    ; Kopiowanie sekcji .data z ROM do RAM
    la a0, _data_lma             ; zrodlo (LMA - Load Memory Address)
    la a1, _data                 ; cel (VMA - Virtual Memory Address)
    la a2, _edata
    ld/sd loop                   ; 
    ; Zerowanie sekcji .bss
    la a0, __bss_start
    la a1, __bss_end
    sd zero loop                 

    call __libc_init_array       ; konstruktory C++/static

    call cpu_init                ; inicjalizacja CPU (UART, PLIC, timery)
    puts "[BOOT] calling board_init"
    call board_init              ; inicjalizacja GPIO (LEDy, przyciski)
    puts "[BOOT] calling kernel_init"
    call kernel_init             ; inicjalizacja jadra RIOT (tworzy wątki)

```

**Kluczowa roznica rv64 vs rv32:** instrukcje kopiowania uzywaja `ld`/`sd` (8-bajtowe) zamiast `lw`/`sw`. Jesli uzylibysmy rv32-owego kodu z krokiem 4 bajtow na maszynie rv64, BSS byloby zerowane dwukrotnie wolniej, a dane kopiowane niepoprawnie przy niesprzyjajacych wyrownaniach.

### 3.2 `cpu_init()` - `cpu/noelv/cpu.c`

```c
void cpu_init(void)
{
    riscv_init();    // FPU enable, IRQ init (mtvec, PLIC), PMP
    early_init();    // inicjalizacja UART (przez periph_cpu_common)
    periph_init();   // inicjalizacja peryferiow uzytkownika
}
```

`early_init()` pochodzi z `cpu/riscv_common` i wywoluje `uart_init()` dla `UART_DEV(0)` w trybie TX-only, co pozwala uzywac `printf()` juz na tym etapie.

### 3.3 `kernel_init()` - jadro RIOT

`kernel_init()` z `core/kernel_init.c` wykonuje:
1. Tworzy watek idle (stos: `THREAD_STACKSIZE_IDLE`)
2. Tworzy watek main (stos: `THREAD_STACKSIZE_MAIN`)
3. Wywoluje `cpu_switch_context_exit()` - inicjuje przelaczanie kontekstu

Jadro **nigdy nie wraca** z `kernel_init()`. Po tym wywolaniu kontrole przejmu scheduler.

---

## 4. Zarzadzanie kontekstem watkow

### 4.1 Ramka kontekstu - `context_frame.h`

RIOT przechowuje rejestry watkow na stosie (nie w bloku kontrolnym watkow TCB). Przy kazdym przelaczeniu kontekstu caly stan procesora jest zapisywany na stosie aktywnego watku.

Struktura `context_switch_frame` zawiera wszystkie rejestry robocze rv64:

```c
struct context_switch_frame {
    // Rejestry callee-saved (s0-s11): 12 x 8 = 96 bajtow
    uint64_t s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
    // Rejestry caller-saved (ra, t0-t6, a0-a7): 16 x 8 = 128 bajtow
    uint64_t ra, t0, t1, t2, t3, t4, t5, t6;
    uint64_t a0, a1, a2, a3, a4, a5, a6, a7;
    // Licznik programu (mepc)
    uint64_t pc;
    // Padding do wyrownania 16-bajtowego
    uint64_t pad[3];
};
```

**Rozmiar ramki:**
- Pola: 29 x 8 = 232 bajty
- Padding: 3 x 8 = 24 bajty  
- Lacznie: `CONTEXT_FRAME_SIZE = 256 bajtow`

Na rv32 kazdy rejestr ma 4 bajty, więc ramka zajmuje 128 bajtow.

### 4.2 Offsety rejestr i ich weryfikacja

Offsety sa zdefiniowane jako makra i uzywane w inline assembly:

```
s0_OFFSET  =   0    s8_OFFSET  =  64    ra_OFFSET  =  96
s1_OFFSET  =   8    s9_OFFSET  =  72    t0_OFFSET  = 104
s2_OFFSET  =  16    s10_OFFSET =  80    ...
...                  s11_OFFSET =  88    a7_OFFSET  = 216
                                         pc_OFFSET  = 224
```

Plik `context_frame.c` zawiera asercje kompilacji (`static_assert`), ktore weryfikuja ze offsety makr odpowiadaja rzeczywistemu rozmieszczeniu pol w strukturze. Jesli ktos zmodyfikuje strukture bez aktualizacji offsetow, blad kompilacji zostanie zgloszone.

### 4.3 Inicjalizacja stosu nowego watku - `thread_stack_init()`

Kiedy RIOT tworzy nowy wątek, `thread_stack_init()` przygotowuje stos tak, jakby watek juz raz byl wywlaszczony przez ISR:

```c
char *thread_stack_init(thread_task_func_t task_func, void *arg,
                        void *stack_start, int stack_size)
{
    // 1. Oblicz gore stosu
    stk_top = stack_start + stack_size;

    // 2. Marker do wykrywania przepelnienia (0x77777777)
    *--stk_top = STACK_MARKER;

    // 3. Wyrownaj do 16 bajtow 
    stk_top = (stk_top) & ~0xf;

    // 4. Zarezerwuj miejsce na ramke kontekstu
    stk_top -= sizeof(struct context_switch_frame);

    // 5. Wypelnij ramke zerowymi wartosciami
    memset(sf, 0, sizeof(*sf));

    // 6. Ustaw poczatkowe wartosci
    sf->pc = (uword_t)task_func;   // punkt startu
    sf->a0 = (uword_t)arg;         // argument (wg ABI rv64)
    sf->ra = (uword_t)sched_task_exit; // powrot po zakonczeniu

    return (char *)stk_top;
}
```

Watek startuje jakby zostal wznowiony przez ISR - `trap_entry` bedzie restorowac rejestry z przygotowanej ramki, ustawi `mepc = sf->pc` i wykona `mret`, co skoczy do funkcji watkowej z argumentem w `a0`.

### 4.4 Przelaczanie kontekstu

Przelaczanie kontekstu odbywa sie w funkcji `trap_entry` (ISR), ktora jest wywoływana przy kazdym przerwaniu:

1. Zapisz rejestry caller saved na stosie aktywnego watku
2. Przenies SP na stos ISR (`_sp`)
3. Wywolaj `handle_trap(mcause)`
4. Jesli `sched_context_switch_request == 1`: wywolaj `sched_run()`
5. Zapisz rejestry callee-saved starego watku
6. Zaladuj SP nowego watku (z `thread->sp`)
7. Odtworz rejestry nowego watku
8. `mret` - powrot do nowego watku

---

## 5. Obsluga przerwan i pulapek

### 5.1 Wektor przerwan - `mtvec`

Na poczatku `riscv_irq_init()` ustawia CSR `mtvec` na adres funkcji `trap_entry`:

```c
write_csr(mtvec, (uintptr_t)&trap_entry);  // tryb direct (bit0=0)
```

W trybie "direct" WSZYSTKIE przerwania i pulapki trafiaja do jednego handlera. Tryb "vectored" (bit0=1) wymaga tablicy wektorow; w tej implementacji nie jest uzywany (z wyjatkiem CLIC ktory tu nie wystepuje).

Funkcja `trap_entry` jest wyrownana do granicy 64 bajtow (`__attribute__((aligned(64)))`).

### 5.2 Wejscie do ISR - `trap_entry` (assembly)

```asm
trap_entry:
    addi sp, sp, -256          ; zarezerwuj CONTEXT_FRAME_SIZE
    sd ra, 96(sp)              ; zapisz rejestry caller-saved
    sd t0, 104(sp)
    ... (t1-t6, a0-a7, s0, s1)
    mv s0, sp                  ; s0 = stos uzytkownika
    la sp, _sp                 ; sp = stos ISR 
    csrr a0, mcause            
    call handle_trap           ; wywolaj handler w C
    ld a0, sched_context_switch_request
    beqz a0, no_sched          ; brak przelaczenia -> skip
    ld s1, sched_active_thread
    call sched_run             ; wybierz nowy watek
no_sched:
    mv sp, s0                  ; przywroc stos uzytkownika
    beqz a0, no_switch
    ; zapisz callee-saved starego watku 
    sd sp, 0(s1)               ; zapisz sp watku
null_thread:
    ld s1, sched_active_thread ; zaladuj nowy watek
    ld sp, 0(s1)               ; zaladuj sp nowego watku
    ld a1, 224(sp)             ; zaladuj pc nowego watku
    csrw mepc, a1              ; ustaw mepc
    ; przywroc callee-saved nowego watku 
no_switch:
    ; przywroc caller-saved 
    addi sp, sp, 256
```

**Uwaga:** Uzywany jest oddzielny stos ISR (`_sp` zdefiniowany w skrypcie linkera), aby ISR mial miejsce nawet gdy stos aktywnego wątku jest prawie pelny.

### 5.3 Dispatch przerwan - `handle_trap()`

```c
static void handle_trap(uword_t mcause)
{
    riscv_in_isr = 1;
    bool is_interrupt = (mcause & MCAUSE_INT) == MCAUSE_INT;
    uword_t trap = mcause & MCAUSE_CAUSE;

    if (is_interrupt) {
        switch (trap) {
        case IRQ_M_TIMER:  timer_isr(); break;   // bit 7 mcause
        case IRQ_M_EXT:    plic_isr_handler(); break; // bit 11 mcause
        default:           core_panic(...);
        }
    } else {
        switch (trap) {
        case CAUSE_MACHINE_ECALL:
            sched_context_switch_request = 1;
            write_csr(mepc, read_csr(mepc) + 4); // przeskocz instrukcje ecall
            break;
        default:
            core_panic(PANIC_GENERAL_ERROR, "Unhandled trap");
        }
    }
    riscv_in_isr = 0;
}
```

### 5.4 Kluczowa poprawka: MCAUSE_INT dla rv64

**Problem:** W oryginalnym `riscv_csr.h` (wspolny kod RISC-V):
```c
#define MCAUSE_INT   0x80000000   // bit 31 - poprawny dla rv32
```

Na rv64 bit wskaznika przerwania to **bit 63** rejestru `mcause` (64-bitowego):
```
mcause dla timer interrupt: 0x8000000000000007
                              ^-- bit 63 = przerwanie
                                             ^-- cause = 7 (M-timer)
```

Sprawdzenie `mcause & 0x80000000` na wartosci `0x8000000000000007` daje `0` - procesor wchodzi w blad "unhandled trap" zamiast obsluzyc timer.

**Poprawka w `vendor/riscv_csr.h`:**
```c
#define MCAUSE_INT   0x8000000000000000UL  // bit 63
#define MCAUSE_CAUSE 0x7FFFFFFFFFFFFFFFUL  // bity 62:0
```

---

## 6. Sterownik timera - ACLINT/CLINT

### 6.1 Architektura ACLINT

RISC-V ACLINT (Advanced Core Local Interruptor) zastepuje starszy CLINT. Implementacja Gaisler (`clint0`) udostepnia:
- `MTIME` - 64-bitowy licznik czasu, taktowany 1 MHz (podzielnik 91)
- `MTIMECMP` - rejestr porownania; gdy `MTIME >= MTIMECMP` generuje przerwanie M-timer

Rejestry (offsety od `CLINT_BASE_ADDR = 0xe0000000`):
- `0x0000` - `msip[0]` (software interrupt)
- `0x4000` - `mtimecmp[0]` (compare register, 64-bit)
- `0xbff8` - `mtime` (current time, 64-bit)

### 6.2 Czestotliwosc timera

```c
#define RTC_FREQ  (1000000UL)  // 1 MHz - czestotliwosc MTIME
```

RIOT wymaga od kalienta wywolania `timer_init(dev, RTC_FREQ, cb, arg)` - jesli poda inna czestotliwosc, zwracany jest blad. Moduly wyzszego poziomu (ztimer, xtimer) uzywaja tej czestotliwosci do przeliczania tykniec.

### 6.3 Implementacja `timer_set()`

```c
int timer_set(tim_t dev, int channel, unsigned int timeout)
{
    uint64_t now = *mtime;
    uint64_t then = now + (uint64_t)timeout;
    clear_csr(mie, MIP_MTIP);   // wylacz przerwanie timera
    *mtimecmp = then;            // ustaw nowy czas alarmu
    set_csr(mie, MIP_MTIP);     // wlacz przerwanie timera
    return 0;
}
```

Wylaczenie `MIP_MTIP` przed zapisem do `mtimecmp` jest konieczne aby uniknac spurious interrupt gdy wartosci sa zapisywane czescioowo (na 32-bitowych szynach danych mogloby to wystapic nawet przy 64-bitowym rejestrze).

### 6.4 Konflikt `timer_set` i flaga `PERIPH_TIMER_PROVIDES_SET`

RIOT zawiera w `sys/periph_common/timer.c` generyczna implementacje `timer_set()`:

```c
// periph_common/timer.c
int timer_set(tim_t dev, int channel, unsigned int timeout) {
    return timer_set_absolute(dev, channel, timer_read(dev) + timeout);
}
```

Poniewaz `coretimer.c` dostarcza wlasna implementacje `timer_set()`, pojawia sie blad linkera `multiple definition of 'timer_set'`. Rozwiazaniem jest zdefiniowanie flagi preprocesora w `Makefile.include`:

```makefile
CFLAGS += -DPERIPH_TIMER_PROVIDES_SET
```

Ta flaga powoduje ze `periph_common/timer.c` pomija swoja implementacje `timer_set()`.

---

## 7. Sterownik UART - APBUART

### 7.1 Rejestry APBUART

Gaisler APBUART (Generic UART) ma nastepujace rejestry (offsety od adresu bazowego):

| Offset | Nazwa | Opis |
|--------|-------|------|
| `0x00` | DATA | Rejestr danych RX/TX |
| `0x04` | STATUS | Rejestr stanu |
| `0x08` | CTRL | Rejestr sterujacy |
| `0x0C` | SCALER | Rejestr skalera baudrate |
| `0x10` | FIFO | Rejestr debug FIFO |

Kluczowe bity STATUS:
- `bit 0` - DR (Data Ready): bajt dostepny w RX FIFO
- `bit 9` - TF (TX Full): TX FIFO pelne, czekaj przed zapisem

Kluczowe bity CTRL:
- `bit 0` - RE (Receiver Enable)
- `bit 1` - TE (Transmitter Enable)
- `bit 2` - RI (Receiver Interrupt Enable)


### 7.2 Inicjalizacja UART z przerwaniami RX

```c
int uart_init(uart_t dev, uint32_t baudrate, uart_rx_cb_t rx_cb, void *arg)
{
    // Ustaw baudrate
    APBUART_REG(uart_config[dev].addr, APBUART_SCALER) =
        APBUART_SCALER_VAL(CLOCK_CORECLOCK, baudrate);

    if (rx_cb) {
        // Zarejestruj callback w PLIC
        plic_set_isr_cb(uart_config[dev].irq, uart_isr);
        plic_enable_interrupt(uart_config[dev].irq);
        plic_set_priority(uart_config[dev].irq, UART_ISR_PRIO);
        // Aktywuj TX + RX + interrupt RX
        APBUART_REG(..., APBUART_CTRL) = APBUART_CTRL_TE | APBUART_CTRL_RE | APBUART_CTRL_RI;
    } else {
        // Tylko TX (bez przerwan)
        APBUART_REG(..., APBUART_CTRL) = APBUART_CTRL_TE | APBUART_CTRL_RE;
    }
    return UART_OK;
}
```

### 7.3 Zapis blokujacy

`uart_write()` stosuje aktywne czekanie az TX FIFO nie bedzie pelne:

```c
void uart_write(uart_t dev, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        while (APBUART_REG(..., APBUART_STATUS) & APBUART_STATUS_TF) {}
        APBUART_REG(..., APBUART_DATA) = data[i];
    }
}
```

Podejscie blokujace jest proste i niezawodne; dla systemow wymagajacych DMA lub asynchronicznego TX nalezy zaimplementowac TX FIFO z przerwaniami.

---

## 8. Sterownik GPIO - GRGPIO

### 8.1 Struktura rejestrów GRGPIO

Gaisler GRGPIO jest peryferia APB z nastepujacym layoutem rejestrow:

```c
typedef struct {
    volatile uint32_t data;    // 0x00 - wartosc pinow (read=wejscie, write=wyjscie)
    volatile uint32_t output;  // 0x04 - rejestr wyjsciowy
    volatile uint32_t dir;     // 0x08 - kierunek (1=wyjscie, 0=wejscie)
    volatile uint32_t imask;   // 0x0C - maska przerwan
    volatile uint32_t ipol;    // 0x10 - polarnosc przerwan
    volatile uint32_t iedge;   // 0x14 - wyzwalanie krawedzia/poziomem
} grgpio_t;
```

**Kluczowe rozroznienie:** Rejestr `data` (0x00) przy odczycie zwraca aktualny stan pinow fizycznych , przy zapisie zachowuje sie jak rejestr wyjsciowy. Rejestr `output` (0x04) to latch wyjsciowy - jego zapis nie jest natychmiastowo widpczny na `data` dla pinow wejsciowych.

### 8.2 Mapowanie pinow dla ZedBoard NoeLV

Mapowanie GPIO jest wyniklem analizy VHDL projektu NoeLV:

**LEDy (LD0..LD7) - wyjscia:**
```
gpio_o[16] -> LED0 (LD0)    gpio_o[20] -> LED4 (LD4)
gpio_o[17] -> LED1 (LD1)    gpio_o[21] -> LED5 (LD5)
gpio_o[18] -> LED2 (LD2)    gpio_o[22] -> LED6 (LD6)
gpio_o[19] -> LED3 (LD3)    gpio_o[23] -> LED7 (LD7)
```
Active HIGH - `gpio_set()` zapala LED.

**Przyciski (BTN) - wejscia:**
```
gpio_i[5] -> BTN1 (BTND)    gpio_i[0] -> BTN4/SW0
gpio_i[6] -> BTN2 (BTNL)    gpio_i[1] -> BTN5/SW1
gpio_i[7] -> BTN3 (BTNR)    gpio_i[2] -> BTN6/SW2
                              gpio_i[3] -> BTN7/SW3
```

BTN0 (BTNC - srodkowy) NIE jest podlaczony do GPIO w tym projekcie VHDL.

**DIP Switches (SW0..SW3):**
```
SW0 -> gpio_i[0]   SW2 -> gpio_i[2]
SW1 -> gpio_i[1]   SW3 -> gpio_i[3] (rowniez DSU/UART mux: 1=debug)
```

### 8.3 Definicje pinow w board.h

```c
// Makro GPIO_PIN(port, pin) = (port << 5) | pin
#define LED0_PIN   GPIO_PIN(0, 16)  // gpio_o[16]
#define LED7_PIN   GPIO_PIN(0, 23)  // gpio_o[23]

#define BTN1_PIN   GPIO_PIN(0, 5)   // BTND
#define BTN4_PIN   GPIO_PIN(0, 0)   // SW0
```

### 8.4 Implementacja operacji GPIO

```c
// Ekstrakcja numeru pinu (bity 4:0 wartosci gpio_t)
static inline int _pin(gpio_t g) { return (int)(g & 0x1f); }

// Zwroc wskaznik do kontrolera (tylko jeden w naszym przypadku)
static inline grgpio_t *_dev(gpio_t g) {
    (void)g;
    return GRGPIO_DEV(GPIO0_BASE_ADDR);  // 0xff983000
}

int gpio_init(gpio_t pin, gpio_mode_t mode) {
    switch (mode) {
    case GPIO_OUT:
        dev->dir |= (1u << p);   // ustaw bit kierunku
        break;
    case GPIO_IN:
    case GPIO_IN_PD:  // GRGPIO nie ma rezystorow pull - traktowane jak GPIO_IN
    case GPIO_IN_PU:
        dev->dir &= ~(1u << p);
        break;
    default: return -1;
    }
    return 0;
}

bool gpio_read(gpio_t pin)   { return (_dev(pin)->data >> _pin(pin)) & 1u; }
void gpio_set(gpio_t pin)    { _dev(pin)->output |=  (1u << _pin(pin)); }
void gpio_clear(gpio_t pin)  { _dev(pin)->output &= ~(1u << _pin(pin)); }
void gpio_toggle(gpio_t pin) { _dev(pin)->output ^=  (1u << _pin(pin)); }
```

**Uwaga o pull-up/pull-down:** GRGPIO nie posiada wbudowanych rezystorow pull-up/pull-down. Tryby `GPIO_IN_PD` i `GPIO_IN_PU` sa akceptowane przez API ale dzialaja identycznie jak `GPIO_IN`. 

### 8.5 Inicjalizacja GPIO w board_init()

`board_init()` w `boards/zedboard-noelv/board.c` nadpisuje slabą implementacje `board_common_init` (z modulu `board_common`):

```c
void board_init(void)
{
    // Wszystkie 8 LEDow jako wyjscia
    gpio_init(LED0_PIN, GPIO_OUT);
    gpio_init(LED1_PIN, GPIO_OUT);
    // ... LED2-LED7

    // Wszystkie przyciski i DIP switches jako wejscia
    gpio_init(BTN1_PIN, GPIO_IN);
    gpio_init(BTN2_PIN, GPIO_IN);
    // ... BTN3-BTN7
}
```

Bez tej funkcji `board_init()` byloby pustym stubem z `board_common`, a rejestry DIR zostayby przy wartosci domyslnej (zazwyczaj 0 - wszystko wejscia), co uniemozliwia sterowanie LEDami.

---

## 9. Konfiguracja pamieci i skrypt linkera

### 9.1 Mapa pamieci

Projekt NoeLV na ZedBoard uzywa zewnetrznej pamieci DDR3 (kontroler MIG) dostepnej od adresu `0x00000000` z 256 MB przestrzeni:

```makefile
# boards/zedboard-noelv/Makefile.include
ROM_START_ADDR = 0x00000000   # poczatek kodu (ladowany przez GRMON)
ROM_LEN        = 0x00800000   # 8 MB dla kodu i danych read-only
RAM_START_ADDR = 0x00800000   # poczatek RAM
RAM_LEN        = 0x07800000   # 120 MB dla danych, stosu i heap
```

Nazwy "ROM" i "RAM" sa terminologia RIOT - w rzeczywistosci obie regiony sa w DDR3 RAM (brak fizycznej flash/ROM w tej konfiguracji). GRMON laduje caly ELF (kod + dane) do DDR3 przez JTAG.

### 9.2 Skrypt linkera

Uzyty jest standardowy skrypt RIOT dla RISC-V: `cpu/riscv_common/ldscripts/riscv.ld`. Otrzymuje on wartosci przez symbole linkera:

```makefile
LINKFLAGS += --defsym=_rom_start_addr=0x00000000
LINKFLAGS += --defsym=_ram_start_addr=0x00800000
LINKFLAGS += --defsym=_rom_length=0x00800000
LINKFLAGS += --defsym=_ram_length=0x07800000
```

Skrypt definiuje sekcje:
- `.text` + `.rodata` - ladowane do ROM region (kod, stale)
- `.data` - ladowane do ROM, kopiowane do RAM przy starcie
- `.bss` - w RAM, zerowane przy starcie przez `start.S`
- `_sp` (stos glowny ISR) - na gorze RAM
- `_sheap` / `_eheap` - przestrzen dla `malloc()`

### 9.3 Adres startowy i reset

Po zaladowaniu przez GRMON (`load plik.elf`), wykonanie startuje od adresu z naglowka ELF (entry point = `_start` = `0x00000000`). Nie ma osobnej pamieci flash ani bootloadera.

---

## 10. System przerwan - PLIC

### 10.1 Konfiguracja PLIC

RISC-V PLIC (Platform Level Interrupt Controller) zarzadza przerwaniami zewnetrznymi z peryferiow. W NoelV ZedBoard:

```c
#define PLIC_CTRL_ADDR       (0xf8000000UL)  // adres bazowy
#define PLIC_NUM_INTERRUPTS  (31U)           // 31 zrodel przerwan
#define PLIC_NUM_PRIORITIES  (7U)            // 7 poziomow priorytetu
```

### 10.2 Inicjalizacja PLIC

```c
void plic_init(void) {
    // Ustaw progi priorytetow na 0 (przepusc wszystkie)
    plic->threshold[0] = 0;
    // Wylacz wszystkie przerwania zewnetrzne
    plic->enable[0] = 0;
}
```

Kazdy sterownik peryferium rejestruje swoj handler przez:
```c
plic_set_isr_cb(irq_num, callback_fn);   // zarejestruj callback
plic_enable_interrupt(irq_num);           // wlacz w PLIC
plic_set_priority(irq_num, priority);    // ustaw priorytet
```

UART0 uzywa IRQ=1, priorytet=1.

### 10.3 Obsluga przerwania zewnetrznego

Kiedy PLIC zglosci przerwanie (IRQ_M_EXT, cause=11 w mcause):

```c
case IRQ_M_EXT:
    plic_isr_handler();  // odczytaj claim, wywolaj callback, zapisz complete
    break;
```

`plic_isr_handler()` odczytuje rejestr CLAIM (ktory rownoczasnie blokuje kolejne przerwanie o tym samym priorytecie), wywoluje zarejestrowany callback, i zapisuje numer do rejestru COMPLETE zwalniajac PLIC.

---

## 11. Konfiguracja lancucha narzedzi i flag kompilacji

### 11.1 ISA i ABI

```makefile
# cpu/noelv/Makefile.include
ifeq (rv64,$(CPU_ARCH))
    CFLAGS_CPU  := -march=rv64imac -mabi=lp64
    ASFLAGS     := $(CFLAGS_CPU)
    TARGET_ARCH_LLVM := riscv64-none-elf
    RUST_TARGET  = riscv64imac-unknown-none-elf
endif
```

- `-march=rv64imac` - docelowa architektura: 64-bit, Integer, Multiply, Atomic, Compressed
- `-mabi=lp64` - ABI: long i pointery sa 64-bitowe, brak rejestrowych argumentow float

Roznicy od rv32:
- rv32: `-march=rv32imac -mabi=ilp32` (int, long i pointery 32-bit)
- rv64: `-march=rv64imac -mabi=lp64` (int 32-bit, long i pointery 64-bit)

### 11.2 Dlaczego nadpisanie po riscv.inc.mk

`riscv.inc.mk` domyslnie ustawia `CFLAGS_CPU` dla rv32. Port nadpisuje te wartosc po wlaczeniu tego pliku, poniewaz `CFLAGS_CPU` jest zmienna leniwa , więc pozniejsze przypisanie obowiazuje w czasie ekspansji:

```makefile
include $(RIOTMAKE)/arch/riscv.inc.mk

# Po riscv.inc.mk nadpisz dla rv64
ifeq (rv64,$(CPU_ARCH))
    CFLAGS_CPU := -march=rv64imac -mabi=lp64
    ASFLAGS := $(CFLAGS_CPU)  # ASFLAGS to := wiec musi byc jawnie ustawiony
endif
```

`ASFLAGS` wymaga jawnego przepisania, bo jest przypisywane przez `:=` w `riscv.inc.mk` - nie moze byc lazily expanded.

### 11.3 Flagi specjalne

```makefile
CFLAGS += -Wno-pedantic          # wylacz pedantyczne ostrzezenia
CFLAGS += -DPERIPH_TIMER_PROVIDES_SET  # nie kompiluj timer_set z periph_common
```

---

## 12. Napotkane problemy i ich rozwiazania

### 12.1 Jadro nie startuje po `kernel_init`

**Objaw:** Po wypisaniu `[BOOT] calling kernel_init` system zawiesza sie bez komunikatu.

**Diagnostyka:** Dodano wydruki w `thread_stack_init()` - okazalo sie ze funkcja jest wywolywana poprawnie. Problem lezy wiec nie w tworzeniu watkow, ale w przelaczaniu kontekstu lub stanie pamieci.

**Analiza:** `THREAD_STACKSIZE_IDLE = 256` < `CONTEXT_FRAME_SIZE = 256` + overhead -> przepelnienie stosu -> korupcja BSS.

**Rozwiazanie:** Zwiekszono rozmiary stosow o 2 razy.

### 12.2 Brak obslugi przerwan timera - panic

**Objaw:** Po uruchomieniu blinky:
```
Trap: mcause=0x8000000000000007
Machine Cause Error 0x7: Store/AMO access fault
*** RIOT kernel panic: Unhandled trap
```

**Analiza:** `mcause = 0x8000000000000007`:
- bit 63 = 1 - to przerwanie
- bity 62:0 = 7 - M-timer interrupt

Ale stare `MCAUSE_INT = 0x80000000` - sprawdzenie `mcause & 0x80000000 = 0` -> `is_interrupt = false` -> wpadniec w obsluge pulapki -> panic.

**Rozwiazanie:** `MCAUSE_INT = 0x8000000000000000UL`.

### 12.3 Multiple definition of timer_set

**Objaw:** Blad linkera `multiple definition of 'timer_set'`.

**Przyczyna:** `coretimer.c` i `sys/periph_common/timer.c` oba definiuja `timer_set()`.

**Rozwiazanie:** `CFLAGS += -DPERIPH_TIMER_PROVIDES_SET`.

### 12.4 Konflikt typow gpio_read

**Objaw:** `conflicting types for 'gpio_read'; have 'int(gpio_t)' ... previous declaration 'bool(gpio_t)'`

**Przyczyna:** `periph/gpio.h` deklaruje `bool gpio_read(gpio_t)`, implementacja miala `int gpio_read(gpio_t)`.

**Rozwiazanie:** Zmiana typu zwracanego na `bool`.

### 12.5 Niepoprawna mapa rejestrów GRGPIO

**Objaw:** LEDy nie swieca sie pomimo wywolania `gpio_set()`.

**Przyczyna:** Pierwsza wersja sterownika miala zly layout struktury - `dir` byl na offsecie 0x04 zamiast 0x08 (pominiety rejestr `output`). Zapis do `dir` trafisal pod adres rejestru `output` i odwrotnie.

**Rozwiazanie:** Dodanie brakujacego pola `output` w strukturze `grgpio_t`:
```c
volatile uint32_t data;    // 0x00
volatile uint32_t output;  // 0x04  
volatile uint32_t dir;     // 0x08
```

---

## 13. Przykladowa aplikacja testowa

### 13.1 `examples/noelv_test/`

Aplikacja testowa weryfikuje trzy obszary funkcjonalnosci:

**Test 1 - UART:**
```c
puts("\r\n=== NoelV RIOT port test ===\r\n");
printf("  coreclk = %lu Hz\r\n", (unsigned long)coreclk());
```
Jezeli ten komunikat jest widoczny, UART i inicjalizacja systemu dzialaja poprawnie.

**Test 2 - GPIO output (Knight Rider):**
```c
static void knight_rider(int n_rounds) {
    for (int r = 0; r < n_rounds; r++) {
        for (int i = 0; i < LED_NUMOF; i++) {
            led_only(i);    // zapal tylko i-ty LED
            delay_ms(80);
        }
        for (int i = LED_NUMOF - 2; i > 0; i--) {
            led_only(i);
            delay_ms(80);
        }
    }
}
```
Swieci kolejno LEDy od LD0 do LD7 i z powrotem - efekt "Knight Rider". 10 rund.

**Test 3 - GPIO input:**
```c
for (int i = 0; i < 2000; i++) {
    bool b1 = gpio_read(BTN1_PIN);  // BTND
    // ...
    if (b1) gpio_set(LED0_PIN); else gpio_clear(LED0_PIN);
    // ...
    delay_ms(50);
}
```
Przez 100 sekund (2000 x 50ms) przyciski steruja odpowiadajacymi LEDami.

### 13.2 Funkcja delay_ms

Delay jest realizowany przez petle busy-wait skalowana przez `coreclk()`:

```c
static void delay_ms(uint32_t ms)
{
    uint32_t loops = (coreclk() / 20) / 1000 * ms;
    for (volatile uint32_t i = 0; i < loops; i++) {}
}
```

Dzielnik `20` to przyblizona liczba cykli na iteracje petli (kompilator moze to zoptymalizowac). Dla dokladnych opoznien nalezy uzywac `ztimer` lub `xtimer` z RIOT.

### 13.3 Makefile aplikacji

```makefile
APPLICATION = noelv_test
BOARD ?= zedboard-noelv
RIOTBASE ?= $(CURDIR)/../..
DEVELHELP ?= 1
QUIET ?= 1
FEATURES_OPTIONAL += periph_timer
include $(RIOTBASE)/Makefile.include
```

`FEATURES_OPTIONAL += periph_timer` powoduje ze jesli `periph_timer` jest dostepne (a jest - dostarczane przez `cpu/noelv/Makefile.features`), zostanie wlaczone. W przeciwnym razie kompilacja nie zawiedzie.

---

## 14. Procedura wgrywania i debugowania przez GRMON

### 14.1 Kompilacja

```bash
cd examples/noelv_test
make BOARD=zedboard-noelv
# lub z jawna sciezka
make BOARD=zedboard-noelv RIOTBASE=/sciezka/do/RIOT
```

Rezultatem jest plik ELF: `bin/zedboard-noelv/noelv_test.elf`

### 14.2 Wgrywanie przez GRMON4

```
./grmon -u -digilent -jtagdevice 0 -v
```

Po polaczeniu:
```
grmon4> load bin/zedboard-noelv/noelv_test.elf
grmon4> run
```

`run` uruchamia procesor i wyswietla wydruki z `printf()` bezposrednio w konsoli GRMON (przez AHB debug trace APBUART).



To pozwala testowac GPIO niezaleznie od kodu RIOT, co jest przydatne przy diagnostyce sterownika.

---

## Podsumowanie

Port RIOT OS na Gaisler NOEL-V rv64 na ZedBoard FPGA obejmuje:

1. **CPU port** (`cpu/noelv`) - sekwencja startu, przelaczanie kontekstu, obsluga przerwan i pulapek, wsparcie dla PLIC, ACLINT, APBUART, GRGPIO
2. **Board port** (`boards/zedboard-noelv`) - konfiguracja pamieci DDR3, mapa peryferiow, inicjalizacja GPIO
3. **Sterowniki peryferyjne** - UART z przerwaniami RX, GPIO z pelnym API RIOT, timer oparty na ACLINT MTIME

Kluczowe adaptacje rv64 vs rv32:
- `CONTEXT_FRAME_SIZE = 256` (vs 128 dla rv32) - wszystkie rejestry 8-bajtowe
- `THREAD_STACKSIZE_IDLE = 512` (minimum dla rv64, wiekszy niz rv32)
- `MCAUSE_INT = 0x8000000000000000UL` (bit 63, nie bit 31)
- `start.S` uzywa `ld`/`sd` (8B) zamiast `lw`/`sw` do inicjalizacji BSS i .data
- Flagi kompilatora `-march=rv64imac -mabi=lp64`
