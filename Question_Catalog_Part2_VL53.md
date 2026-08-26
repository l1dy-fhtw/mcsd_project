# Presentation Question Catalog — Part 2 (VL53L1X / sensor)

**For:** the ToF driver owner  
**Also covers:** `main.c` and the whole project  
**Answers:** not in this file. Write them separately; we will check them later.

Practice out loud. Be ready for a follow-up “why?”.

---

## Project and `main.c`

1. In one minute: what does the device do after power-on, and what are the three presentation scenarios (SW1 live, live refresh, 30 s sleep + wake restore)?

2. LCD and VL53L1X share one bus. Which bus, which pins (SCL/SDA), and why is sharing a constraint?

3. Name the application states. What does one SW1 press do in Ready, Live, Hold, and Sleep?

4. What does **Hold** freeze — the last millimetre I2C sample, or the centimetre value in the application? Why does that matter?

5. After 30 s idle: what is stopped (including the ToF), what is turned off, and what is remembered so wake can restore Hold?

6. Potentiometer bands (0–33 / 34–66 / 67–100 %): what does each mode do? Does SW1 still work in all three?

7. If the ToF is unplugged at boot: what is on the LCD, how do lab keys `'1'` / `'2'` help, and when may `tof_try_attach` probe the bus again?

8. Why is USART2 RX interrupt-driven (ring buffer) instead of polling `HAL_UART_Receive` in a loop that may sleep 100 ms?

9. TIM7 replaced SysTick as the tick **source**. Why do we still call `HAL_GetTick()`? Where does the energy saving actually come from (coarse sleep + EXTI in `ST_SLEEP`)?

10. In `ST_SLEEP` we suspend the tick and `__WFI()`. Is this STM32 Stop/Standby or Sleep, and why (RAM, Hold, LCD)? Why stop ranging before that wait?

---

## VL53L1X driver (your part)

11. What does the sensor measure, and in which unit does **our driver** return it? 7-bit address `0x29` vs HAL 8-bit `0x52`?

12. Registers are 16-bit addresses. Which HAL functions (`Mem_Read` / `Mem_Write`) and which `MemAddSize`? Why no ST ULD library (config blob + register I/O)?

13. `VL53L1X_Init`: three ID registers and expected values; boot poll on `SYS_STATUS`; 91-byte write at `0x002D`. Return codes if NACK, wrong ID, or boot timeout?

14. `StartMeasurement` vs `StopMeasurement`: which registers/values (`INT_CLEAR`, `SYS_START` 0x40 / 0x00)? Continuous ranging? When does the app stop?

15. `IsDataReady`: which register and bit? Why poll over I2C instead of the Click GPIO1/INT as EXTI (PB0)?

16. `GetDistance(hi2c, pDistanceMm)`: what is written to `*pDistanceMm`, and when is the return `OK` vs `INVALID` vs `ERROR`? Which decoded statuses (0, 1, 2, 7) count as OK, and why accept warnings?

17. `ClearInterrupt`: what write, and why clear even after `INVALID`? Application path: mm → cm (`/10`), clamp `CM_MIN`…`CM_MAX` — what are those limits, and what Hold bug did `CM_MAX = 200` cause?

18. `tof_try_attach` vs `tof_service_live`: ACK + Init + Start; in which states/modes do we read a new sample? What about Hold (display frozen, sensor may still run)?

19. Four status codes (`OK`, `ERROR`, `TIMEOUT`, `INVALID`) in one sentence each. Point to the millimetre register in code (`RANGE_MM` / `I2C_ReadReg16`).

20. Trap: “Hold showed 200 cm at ~4.5 m — is the VL53L1X a 2 m sensor?” Separate sensor range, `CM_MAX` clamp, and LCD `%3d`. Demo backup if the LCD fails: what can you still show on the VCP?

---

*No answers here. Send yours when you want them checked.*
