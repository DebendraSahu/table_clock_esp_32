# Troubleshooting

[← back to README](../README.md)

Every entry here is a failure this project actually hit. Most of them
look like bad wiring and aren't.

---

## Quick table

| Symptom | Cause |
| --- | --- |
| White screen | `DC` or `CS` miswired, or `RESET` left floating |
| Garbled pixels | Lower `SPI_FREQUENCY` — try 20 MHz |
| Graphics fine, **no text** | Font flags wrong — see below |
| Touch never responds | `TFT_MISO` undefined, or `TOUCH_CS` missing |
| Touch works intermittently | Bus noise — see below |
| Touch offsets wrong | Hold the screen while powering on to recalibrate |
| Reboots when WiFi connects | Backlight loading the 3V3 rail |
| Time stuck at `--:--` | NTP unreachable; retries every 60 s |
| Weather shows `--` | Check serial for `[WX]` lines |

---

## Graphics render but no text appears

The most confusing failure mode, because **it compiles and links
cleanly**. TFT_eSPI guards every font behind `#ifdef`, so an unloaded
font makes `drawString()` a silent no-op rather than a compile error.

Check that `platformio.ini` has both:

```ini
-DLOAD_GFXFF=1
-DSMOOTH_FONT=1
```

`LOAD_GFXFF` supplies the proportional UI text; `SMOOTH_FONT` supplies
the anti-aliased clock. Without them the background, icons and cards all
draw normally and every character silently vanishes.

The same applies in reverse: if you switch to `setTextFont(2)` or `(4)`,
you must add `-DLOAD_FONT2` / `-DLOAD_FONT4`, which this project does not
compile in.

---

## Touch never responds

Almost always one of two build flags.

**`TOUCH_CS` missing.** The entire touch extension lives behind
`#ifdef TOUCH_CS`. Without it, `tft.getTouch()` does not exist as a
symbol and the build fails with *"class TFT_eSPI has no member named
getTouch"*. The library warns during compilation:

```
warning: >>>>------>> TOUCH_CS pin not defined, TFT_eSPI touch functions will not be available!
```

**`TFT_MISO` undefined.** This one is silent and much nastier. TFT_eSPI
falls back to `#define TFT_MISO -1` and passes that to the SPI peripheral
as `.miso_io_num = -1`. The bus is configured read-disabled, so the ESP32
is not listening on GPIO 19 at all. Touch is a read operation, so it
returns nothing however perfectly the panel is wired.

---

## Touch works, but only sometimes

This is bus noise, not a loose wire, and the mechanism is worth knowing.

`validTouch()` in TFT_eSPI takes two raw samples and rejects the touch
unless they agree within 20 ADC counts out of 4096 — about 0.5%:

```c
#define _RAWERR 20
if (abs(x_tmp - x_tmp2) > _RAWERR) return false;
if (abs(y_tmp - y_tmp2) > _RAWERR) return false;
```

`getTouch()` retries five times and needs one to pass. So when the bus is
noisy most sample pairs disagree, a few slip through, and touch feels
random.

Fixes in order of effect:

1. **Lower `SPI_FREQUENCY` to 27 MHz.** `SCK` and `MOSI` are shared with
   the touch controller, so display-speed ringing lands directly on the
   lines the ADC is sampling.
2. **Lower `SPI_TOUCH_FREQUENCY` to 1 MHz.** The library default of
   2.5 MHz sits at the edge of the XPT2046's sample-and-hold settling
   time. Touch has no speed requirement.
3. **Check the display's `SDO` is unconnected.** If it is wired, it
   contends with `T_DO` on GPIO 19.
4. **Shorten the jumpers**, especially `SCK` and `MOSI`.
5. **Clean up the 3.3 V rail.** The XPT2046 is *ratiometric* — its ADC
   reference is its supply pin, so rail sag translates directly into
   wandering coordinates. Add a 100 µF cap across the display's VCC/GND,
   or power the panel from a separate supply with grounds tied.

---

## Reading the diagnostic

```bash
pio run -e touch-test -t upload -t monitor
```

Four stages, so a failure tells you *which* part is broken:

| Stage | You should see | It proves |
| --- | --- | --- |
| 1 | Four colour corner blocks, centre cross | Display, rotation, extents |
| 2 | `Idle Z = <n>` on serial | Controller answers over SPI |
| 3 | Raw X/Y sweeping ~200–3900 as you drag | The resistive panel works |
| 4 | Cyan dot under your finger | Calibration and mapping |

Stage 2 is decisive and self-diagnoses:

- **`Z = 0`** — MISO or `T_CS` not connected, or `TFT_MISO` still `-1`.
- **`Z > 3000`** — MISO railed. The display's `SDO` is probably wired
  and must be left unconnected.
- **Low hundreds** — healthy.

At stage 3, hold a finger still and watch the raw values. Wandering by
more than ~20 counts between lines is the deadband rejection described
above.

The diagnostic draws only blocks, lines and circles, so it works even
when the font configuration is broken.

---

## Reboots or flicker when WiFi connects

The backlight draws roughly 100 mA on top of the ESP32's WiFi bursts, and
the DevKitM-1's regulator is small. Add a 100 µF capacitor across the
display's VCC/GND, or power the panel separately with grounds tied
together.

---

## Config changes have no effect

> [!IMPORTANT]
> Do not edit `.pio/libdeps/esp32dev/TFT_eSPI/User_Setup.h`.
> `USER_SETUP_LOADED=1` in `platformio.ini` makes TFT_eSPI skip that file
> entirely, so edits there do nothing. `.pio/` is also gitignored, so
> anything you put there is lost on a clean checkout.

All hardware configuration belongs in `platformio.ini` `build_flags`.
