# QEMU NE2000 Emulation Analysis and Sodex Driver Compatibility Report

## 1. How QEMU's NE2000 Actually Works

Source files analyzed:
- `/Users/sodeyama/git/qemu/hw/net/ne2000.c` (core emulation)
- `/Users/sodeyama/git/qemu/hw/net/ne2000.h` (state structure)
- `/Users/sodeyama/git/qemu/hw/net/ne2000-isa.c` (ISA bus binding)

### 1.1 Memory Layout

QEMU allocates a flat `uint8_t mem[NE2000_MEM_SIZE]` array:
- `NE2000_PMEM_START = 16384` (0x4000, i.e., page 0x40)
- `NE2000_PMEM_END = 49152` (0xC000, i.e., page 0xC0)
- `NE2000_PMEM_SIZE = 32768` (32 KiB)

The first 32 bytes (0x00-0x1F) store the PROM (MAC address duplicated). Packet memory (the ring buffer) starts at offset 0x4000 in the `mem[]` array. Page addresses are in units of 256 bytes, so page 0x40 = byte offset 0x4000.

### 1.2 I/O Port Dispatch (`ne2000_read` / `ne2000_write`)

The ISA NE2000 registers a 0x20-byte I/O region at the configured base address. The dispatch in `ne2000_read`/`ne2000_write` is:

| Address Range | Handler |
|---|---|
| `0x00-0x0F` (size=1) | `ne2000_ioport_read/write` (8390 registers) |
| `0x10` (size 1,2) | `ne2000_asic_ioport_read/write` (data port, 16-bit) |
| `0x10` (size 4) | `ne2000_asic_ioport_readl/writel` (data port, 32-bit) |
| `0x1F` (size=1) | `ne2000_reset_ioport_read/write` (reset port) |
| Everything else | Returns all-1s on read, ignored on write |

**CRITICAL: The reset port is at offset 0x1F, NOT 0x18.**

### 1.3 Register Page Addressing

In `ne2000_ioport_write` and `ne2000_ioport_read`, the page/offset calculation is:
```c
page = s->cmd >> 6;           // bits 7:6 of CR
offset = addr | (page << 4);  // combine into flat offset
```

So for physical I/O address 0x0F:
- Page 0: offset = `0x0F | 0x00 = 0x0F` (EN0_IMR for write, EN0_COUNTER2 for read)
- Page 1: offset = `0x0F | 0x10 = 0x1F` (EN1_MULT+7)
- Page 2: offset = `0x0F | 0x20 = 0x2F` (**no case in either read or write switch**)

### 1.4 ISR Behavior

**ISR is completely independent of IMR.** ISR bits are set by internal events regardless of IMR:

- **PRX (ENISR_RX = 0x01)**: Set in `ne2000_receive()` line 254: `s->isr |= ENISR_RX;`
- **PTX (ENISR_TX = 0x02)**: Set in `ne2000_ioport_write()` line 290: `s->isr |= ENISR_TX;` (during CR write with E8390_TRANS)
- **RDC (ENISR_RDC = 0x40)**: Set in `ne2000_dma_update()` line 507: `s->isr |= ENISR_RDC;` (when rcnt reaches 0)
- **RST (ENISR_RESET = 0x80)**: Set in `ne2000_reset()` line 128: `s->isr = ENISR_RESET;`

ISR bits are cleared by **writing 1s** to ISR (line 346):
```c
case EN0_ISR:
    s->isr &= ~(val & 0x7f);   // bit 7 (RST) cannot be cleared by writing
    ne2000_update_irq(s);
    break;
```

### 1.5 IMR and Interrupt Delivery

`ne2000_update_irq()` (line 140):
```c
isr = (s->isr & s->imr) & 0x7f;
qemu_set_irq(s->irq, (isr != 0));
```

- IMR only controls whether the IRQ line is asserted
- ISR bits are always set regardless of IMR
- Writing to IMR calls `ne2000_update_irq()`, so changing IMR can immediately assert/deassert IRQ

**IMR Write**: Handled only on Page 0, offset 0x0F (EN0_IMR). Stores to `s->imr`.

**IMR Read**: There is **NO read handler for IMR in QEMU**. Page 2 offset 0x2F has no case, so it falls through to `default: ret = 0x00`. **Reading IMR always returns 0x00 in QEMU.**

### 1.6 BNRY Register

**Write** (Page 0, offset 0x03 = EN0_BOUNDARY):
```c
case EN0_BOUNDARY:
    if (val << 8 < NE2000_PMEM_END) {     // validation: val*256 < 0xC000
        s->boundary = val;
    }
    break;
```

**Read** (Page 0, offset 0x03 = EN0_BOUNDARY):
```c
case EN0_BOUNDARY:
    ret = s->boundary;
    break;
```

BNRY is used in `ne2000_buffer_full()` to determine if there's space to receive:
```c
index = s->curpag << 8;
boundary = s->boundary << 8;
if (index < boundary)
    avail = boundary - index;
else
    avail = (s->stop - s->start) - (index - boundary);
if (avail < (MAX_ETH_FRAME_SIZE + 4))
    return 1;   // buffer full
```

The driver must advance BNRY to free space. If BNRY is never advanced, eventually `ne2000_buffer_full()` returns true and QEMU drops incoming packets.

### 1.7 CR Register

Writing to CR (offset 0x00) in `ne2000_ioport_write`:
```c
s->cmd = val;
if (!(val & E8390_STOP)) {           // If STP bit is NOT set:
    s->isr &= ~ENISR_RESET;          //   Clear RST bit in ISR
    // Zero-length DMA triggers immediate RDC
    if ((val & (E8390_RREAD | E8390_RWRITE)) && s->rcnt == 0) {
        s->isr |= ENISR_RDC;
        ne2000_update_irq(s);
    }
    if (val & E8390_TRANS) {          // If TXP bit is set:
        // Execute transmit immediately (synchronous!)
        index = (s->tpsr << 8);
        qemu_send_packet(..., s->mem + index, s->tcnt);
        s->tsr = ENTSR_PTX;          //   Set PTX in TSR
        s->isr |= ENISR_TX;          //   Set PTX in ISR
        s->cmd &= ~E8390_TRANS;      //   Clear TXP in CR
        ne2000_update_irq(s);
    }
}
```

Key observations:
1. **Transmit is synchronous**: The packet is sent immediately, TSR and ISR are updated, and TXP is cleared, all within the same CR write.
2. **STP bit check**: If STP is set in the value written to CR, NO transmit or DMA processing occurs.
3. **RST clearing**: RST in ISR is cleared whenever STP is NOT set in a CR write.

### 1.8 Transmit Path

When `CR_TXP` is written:
1. QEMU reads `s->tpsr << 8` as the source address in `mem[]`
2. Sends `s->tcnt` bytes via `qemu_send_packet()`
3. Sets `s->tsr = ENTSR_PTX` (transmit success)
4. Sets `s->isr |= ENISR_TX` (transmit complete interrupt)
5. Clears `s->cmd &= ~E8390_TRANS` (TXP auto-clears)
6. Calls `ne2000_update_irq()` to potentially assert IRQ

**There is a condition**: All of this only happens if `!(val & E8390_STOP)`. If the driver writes `CR_STP | CR_TXP`, the transmit does NOT execute.

### 1.9 Receive Path

`ne2000_receive()` is called by QEMU's network subsystem when a packet arrives:
1. Checks `s->cmd & E8390_STOP` - if NIC is stopped, drops packet
2. Checks `ne2000_buffer_full()` - if ring buffer is full, drops packet
3. Validates against rxcr (promiscuous, broadcast, multicast, unicast)
4. Writes 4-byte header at `curpag << 8`: `[rsr, next_page, len_lo, len_hi]`
5. Copies packet data after header, wrapping at `s->stop` back to `s->start`
6. Updates `s->curpag = next >> 8` (next available page for writing)
7. Sets `s->isr |= ENISR_RX` and calls `ne2000_update_irq()`

The `total_len` in the header includes the 4-byte header itself: `total_len = size + 4`.
The `next` page calculation includes 4 bytes for CRC: `next = index + ((total_len + 4 + 255) & ~0xff)`.

### 1.10 Remote DMA

**Read DMA** (`ne2000_asic_ioport_read`):
- Reads from `s->mem[s->rsar]` (16-bit if `dcfg & 0x01`)
- Calls `ne2000_dma_update()` which advances `rsar` and decrements `rcnt`
- When `rcnt` reaches 0, sets `ENISR_RDC` in ISR
- `rsar` wraps from `s->stop` to `s->start`

**Write DMA** (`ne2000_asic_ioport_write`):
- If `rcnt == 0`, write is **silently ignored**
- Writes to `s->mem[s->rsar]`
- Same DMA update logic

**Memory access validation**: Writes to `mem[]` are only allowed if `addr < 32` (PROM area) or `addr >= NE2000_PMEM_START && addr < NE2000_MEM_SIZE`. So writing to page addresses below 0x40 (other than PROM) is silently dropped.

### 1.11 Reset Sequence

**Reading from reset port (offset 0x1F)**:
```c
ne2000_reset(s);
return 0;
```
This calls `ne2000_reset()` which:
1. Sets `s->isr = ENISR_RESET` (only RST bit, all others cleared)
2. Reinitializes the PROM area of `mem[]`
3. Does NOT change: `cmd`, `start`, `stop`, `boundary`, `curpag`, `imr`, `tpsr`, etc.

**Writing to reset port (offset 0x1F)**: Does nothing (empty function).

So the correct reset sequence is: READ from reset port (triggers reset), then WRITE to reset port (ends reset pulse, does nothing in QEMU). After reset, only ISR is initialized (to 0x80). All other registers are undefined/zero.

---

## 2. Discrepancies Between Sodex Driver and QEMU

### CRITICAL BUG #1: Reset Port Offset is Wrong

**Sodex**: `RESET_PORT_OFFSET = 0x18`
**QEMU**: Reset port is at offset `0x1F`

In `init_ne2000()`:
```c
out8(io_base+RESET_PORT_OFFSET, in8(io_base+RESET_PORT_OFFSET));
```

The `in8(io_base+0x18)` reads from an unhandled address (returns 0xFF). The `out8(io_base+0x18, 0xFF)` writes to an unhandled address (silently ignored). **The NIC is never actually reset.**

Since `ne2000_reset()` is never called, the ISR never gets the ENISR_RESET bit set. The init code then waits for `ISR_RST` in a busy loop which either times out at `WAIT_MAX_COUNT` or might find leftover state.

**Fix**: Change `RESET_PORT_OFFSET` from `0x18` to `0x1F`.

Note: On real NE2000 hardware, 0x18 is sometimes documented as the reset port for some NE2000 clones. However, QEMU specifically implements it at 0x1F (the RTL8029/DP8390 standard location). Since 0x18 does nothing in QEMU, the init sequence is fundamentally broken.

### CRITICAL BUG #2: CURR_ADDR Should Equal PSTART_ADDR, Not PSTART_ADDR+1

**Sodex**: `CURR_ADDR = PSTART_ADDR + 1 = 0x47`
**Convention**: CURR should be initialized to PSTART_ADDR (0x46)

The empty-buffer detection logic in the Sodex driver is:
```c
bnry = in8(io_base + I_BNRY);   // read BNRY
packet_page = bnry + 1;          // next page after BNRY
if (packet_page == curr)          // if matches CURR, buffer is empty
    return 0;
```

With `BNRY_ADDR = 0x46` and `CURR_ADDR = 0x47`:
- On init: `bnry=0x46`, `packet_page=0x47`, `curr=0x47` => empty (correct)
- After first packet received, QEMU sets `curpag` to the next free page

The issue is that QEMU's `ne2000_buffer_full()` uses `boundary` and `curpag` directly:
```c
index = s->curpag << 8;
boundary = s->boundary << 8;
```
If `boundary == curpag`, QEMU considers the buffer FULL (avail=0), not empty. The Sodex convention of BNRY = CURR-1 for "empty" is the correct DP8390 convention.

However, initializing CURR to PSTART+1 wastes one page (256 bytes) of the ring buffer. More importantly, if the driver later sets BNRY = next_page - 1, and next_page could be PSTART_ADDR, then BNRY = PSTART-1 which is outside the ring. The Sodex code handles this wrap:
```c
new_bnry = next_page - 1;
if (new_bnry < PSTART_ADDR)
    new_bnry = PSTOP_ADDR - 1;
```

**Recommendation**: Change `CURR_ADDR` to `PSTART_ADDR` (0x46) and change `BNRY_ADDR` to `PSTOP_ADDR - 1` (0x7F). This is the standard initialization: BNRY=PSTOP-1, CURR=PSTART. The empty check becomes: `(bnry+1 wrapped) == curr` which evaluates to `(0x7F+1 wrapped to 0x46) == 0x46` => empty.

Actually, the current Sodex convention (BNRY=PSTART, CURR=PSTART+1) also works, though it's non-standard. The real bugs are elsewhere.

### CRITICAL BUG #3: IMR Read on Page 2 Always Returns 0 in QEMU

**Sodex init debug code**:
```c
out8(io_base+O_CR, CR_PAGE2|CR_STA|CR_RD_STOP);
ne_serial_hex8(in8(io_base+0x0F));   // reads "IMR" from Page 2
```

**QEMU**: Page 2, register 0x0F => offset `0x0F | (2 << 4) = 0x2F`. There is NO case for 0x2F in `ne2000_ioport_read`, so it hits `default: ret = 0x00`.

**This is not a Sodex bug - it is a QEMU limitation.** QEMU does not implement Page 2 read of IMR. The real DP8390 chip allows reading IMR via Page 2, register 0x0F. QEMU simply does not emulate this.

**The IMR IS being set correctly** (writing to Page 0 register 0x0F works fine - offset `0x0F | 0x00 = 0x0F = EN0_IMR`). The value just cannot be read back via Page 2 in QEMU.

**Impact**: The debug message `IMR(p2)=00` is misleading but does not indicate a real problem. IMR is correctly stored as 0x7F internally in QEMU.

### BUG #4: Transmit Never Works if STP is Set in CR

Looking at the transmit code:
```c
out8(io_base+O_CR, CR_STA|CR_TXP|CR_RD_STOP);
```

This sets bits: STA (0x02), TXP (0x04), RD_STOP (0x20) = value 0x26. Since `E8390_STOP = 0x01` is NOT set, this should work correctly. **No bug here.**

However, there's a subtle issue: QEMU checks `!(val & E8390_STOP)` not `val & E8390_START`. So as long as STP (bit 0) is clear, transmit proceeds. The Sodex driver correctly sets STA without STP.

### BUG #5: Write DMA Address Calculation

In `ne2000_send()`:
```c
write_remote_dma(SEND_ADDR << 8, buf, len);
```

`SEND_ADDR = 0x40`, so the address is `0x40 << 8 = 0x4000`. In QEMU's memory model, this corresponds to `NE2000_PMEM_START = 0x4000`. This is correct - the write goes to the beginning of packet memory.

However, `TPSR` is set to `SEND_ADDR = 0x40`. In the transmit handler:
```c
index = (s->tpsr << 8);   // = 0x4000
```
This matches. **No bug here.**

### BUG #6: Receive Ring Buffer Range

Sodex defines:
- `PSTART_ADDR = 0x46` => `s->start = 0x4600`
- `PSTOP_ADDR = 0x80` => `s->stop = 0x8000`

QEMU validates:
- `PSTART`: `val << 8 <= NE2000_PMEM_END` => `0x4600 <= 0xC000` (OK)
- `PSTOP`: `val << 8 <= NE2000_PMEM_END` => `0x8000 <= 0xC000` (OK)

The transmit buffer occupies pages 0x40-0x45 (6 pages = 1536 bytes, enough for one max-size Ethernet frame). The receive ring is pages 0x46-0x7F (58 pages = 14848 bytes). **This is fine.**

### BUG #7: BNRY Write Validation

In QEMU:
```c
case EN0_BOUNDARY:
    if (val << 8 < NE2000_PMEM_END) {   // Note: strict less-than
        s->boundary = val;
    }
    break;
```

Sodex writes `BNRY_ADDR = 0x46` and later `new_bnry` values. `0x46 << 8 = 0x4600 < 0xC000` => passes. `0x7F << 8 = 0x7F00 < 0xC000` => passes. **No bug here.**

### ISSUE #8: Data Path Configuration (DCR)

Sodex writes:
```c
out8(io_base+O_DCR, DCR_FT_8B|DCR_LS|DCR_WTS);
```

`DCR_WTS = (1<<0) = 0x01` sets word-transfer mode (16-bit). In QEMU:
```c
if (s->dcfg & 0x01) {
    /* 16 bit access */
    ne2000_mem_writew(s, s->rsar, val);
    ne2000_dma_update(s, 2);
}
```

The driver uses `in16`/`out16` for DMA transfers, which is consistent with DCR_WTS being set. **This is correct.**

### ISSUE #9: DMA Wrap-Around in QEMU vs. Sodex

QEMU's `ne2000_dma_update()` wraps `rsar` when it hits `s->stop`:
```c
if (s->rsar == s->stop)
    s->rsar = s->start;
```

This means read/write DMA automatically wraps at the ring buffer boundary. The Sodex driver manually splits DMA reads for wrap-around in `ne2000_receive()`:
```c
if (data_addr + data_len > ring_end) {
    read_remote_dma(data_addr, uip_buf, first_len);
    read_remote_dma(second_addr, ...);
}
```

Since QEMU wraps `rsar` automatically during DMA, the manual wrap-around in Sodex should also work (both approaches produce the same result). **No bug, but the manual split is unnecessary since QEMU handles wrapping.**

---

## 3. Specific Fixes Needed

### Fix 1: Reset Port Offset (CRITICAL)

In `/Users/sodeyama/git/sodex/src/include/ne2000.h`, change:
```c
#define RESET_PORT_OFFSET   0x18
```
to:
```c
#define RESET_PORT_OFFSET   0x1F
```

This is the single most critical fix. Without it, `init_ne2000()` never actually resets the NIC, and the ISR_RST wait loop runs to timeout.

### Fix 2: Remove Misleading IMR Page2 Read (INFORMATIONAL)

The debug code reading IMR via Page 2 will always return 0 in QEMU. This is a QEMU limitation, not a driver bug. Either remove the debug read or add a comment explaining it doesn't work in QEMU.

### Fix 3: Consider Standard BNRY/CURR Initialization (RECOMMENDED)

Change initialization to the more standard pattern:
```c
#define BNRY_ADDR   (PSTOP_ADDR - 1)   // 0x7F
#define CURR_ADDR   PSTART_ADDR          // 0x46
```

This follows the conventional DP8390 initialization where BNRY points to the last page (marking the entire ring as empty) and CURR starts at PSTART.

### Fix 4: Interrupt Handler Should Check All Bits (RECOMMENDED)

The ISR handler only checks PRX and OVW but ignores PTX and TXE. While this works if the driver doesn't need TX completion notification, it means PTX interrupts accumulate. Since the handler does clear all bits with `out8(io_base + O_ISR, status)`, this is not technically broken, but it would be cleaner to log or handle TX completion.

---

## 4. Explanation of Each Known Issue

### Issue: "BNRY write doesn't seem to take effect (bnry stays at 0x46 after writing 0x46)"

**Root Cause**: This is actually working correctly. The debug log shows:
```
BNRY-W: 46 read-back=46
```

Writing 0x46 and reading back 0x46 means the write succeeded. If the "known issue" is that BNRY stays at 0x46 even after attempting to write a different value, that would indicate the write value itself is being computed as 0x46. This happens when:
- `next_page` from the packet header is 0x47
- `new_bnry = next_page - 1 = 0x46`
- BNRY gets 0x46, which is the same as the initial value

This means only one packet was received and its `next_page` was 0x47 (one page after PSTART). BNRY IS updating correctly; it just happens to update to the same value in this case.

If BNRY genuinely fails to update to a different value, verify that the CR is set to Page 0 before writing BNRY. The Sodex code does this:
```c
out8(io_base + O_CR, CR_PAGE0 | CR_STA | CR_RD_STOP);
out8(io_base + O_BNRY, new_bnry);
```
In QEMU, the page is determined by `s->cmd >> 6`. Since CR_PAGE0 = 0, and the CR write stores `val` directly into `s->cmd`, this should correctly select Page 0. **BNRY writes should work.**

### Issue: "ISR reads as 0x00 via polling even though IRQ fires with ISR=0x03"

**Root Cause**: The IRQ handler at line 150 clears ALL ISR bits:
```c
out8(io_base + O_ISR, status);
```

The sequence is:
1. Packet received: QEMU sets `isr |= ENISR_RX | ENISR_TX` (if TX also completed) = 0x03
2. IRQ fires, handler reads ISR = 0x03, saves to `ne2k_last_isr`
3. Handler writes 0x03 to ISR, which clears those bits: `isr &= ~0x03` => `isr = 0x00`
4. Polling code reads ISR = 0x00

**This is the expected behavior.** ISR was already cleared by the interrupt handler. If you need to poll ISR, you should NOT clear it in the interrupt handler, OR you should use the saved `ne2k_last_isr` value instead of re-reading ISR.

If the intent is to poll without interrupts, then disable interrupts (IMR=0) and don't install an IRQ handler, OR check `ne2k_last_isr` / `ne2000_rx_pending` flag that the handler sets.

### Issue: "IMR reads as 0x00 from Page2 even after writing 0x7F to Page0"

**Root Cause**: **QEMU does not implement Page 2 IMR read.** This is confirmed by the source code.

In `ne2000_ioport_read()`, the read dispatch uses:
```c
offset = addr | (page << 4);
```

For Page 2, register 0x0F: `offset = 0x0F | 0x20 = 0x2F`. There is no `case 0x2F:` in the read switch statement. The `default:` case returns `0x00`.

**The IMR IS correctly set to 0x7F internally.** The write to Page 0 register 0x0F (`offset = 0x0F | 0x00 = 0x0F = EN0_IMR`) succeeds and stores `0x7F` in `s->imr`. Interrupts ARE being delivered. The readback simply doesn't work in QEMU.

This is a known limitation of QEMU's NE2000 emulation. The real DP8390 chip supports reading IMR (and other config registers) via Page 2, but QEMU omits most Page 2 read-back registers.

### Issue: "TSR shows PTX=1 (transmit success) but ISR never shows PTX"

**Root Cause**: This can happen for two reasons:

**Reason A: ISR is cleared before reading.** If the interrupt handler fires and clears ISR (including the PTX bit) before the polling code reads ISR, the polling code sees ISR=0x00. As explained above, the handler does `out8(io_base + O_ISR, status)` which clears all set bits.

**Reason B: STP was set during CR write.** In QEMU:
```c
if (!(val & E8390_STOP)) {
    // ... transmit logic that sets s->tsr and s->isr
}
```

If the CR write has STP set (even accidentally), the entire transmit path is skipped. However, the `s->cmd = val` still stores the value, and a subsequent read of TSR might show leftover state. But looking at the Sodex transmit code:
```c
out8(io_base+O_CR, CR_STA|CR_TXP|CR_RD_STOP);  // 0x02|0x04|0x20 = 0x26
```
STP (0x01) is NOT set, so this should work.

**Most likely explanation**: Reason A. The ISR handler fires (because IMR has PTXE enabled), clears the PTX bit in ISR, and by the time polling code checks, ISR is already 0. TSR retains its value because nothing clears it.

**Fix**: Either:
1. Don't enable PTX interrupts in IMR if you plan to poll ISR for TX completion
2. Check `ne2k_last_isr` instead of reading ISR directly
3. Add PTX handling in the interrupt handler (set a flag like `ne2000_tx_complete`)

---

## 5. Summary of Priority Fixes

| Priority | Issue | Fix |
|---|---|---|
| **P0** | Reset port at wrong offset (0x18 vs 0x1F) | Change `RESET_PORT_OFFSET` to `0x1F` |
| **P1** | ISR appears empty when polled after IRQ | Use `ne2k_last_isr` or don't clear ISR in handler if polling |
| **P2** | IMR Page2 read returns 0 | QEMU limitation; remove debug read or add comment |
| **P3** | TSR has PTX but ISR doesn't | Same root cause as ISR-appears-empty; IRQ handler clears it |
| **P3** | CURR_ADDR non-standard init | Consider BNRY=PSTOP-1, CURR=PSTART |
