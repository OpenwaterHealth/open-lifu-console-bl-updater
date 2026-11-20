# open-lifu-console-bl-updater

One-shot firmware for the Open-LIFU **console** (STM32F072RB). It replaces the secure
bootloader from inside the application slot, then resets into the new bootloader's USB
DFU, where the host installs the application.

It exists for one migration: research units on bootloader **1.0.x** (shipped with
console app 1.2.6, bootloader `1.0.3-rc.1`) to bootloader **1.1.x**. Bootloader 1.1.0
moves the end of the slot and adds the preset page, so those units need it before
console app 1.3.x. Both bootloaders trust the same research key, and this image is
signed with it. Clinical units are built with the new bootloader and never run this.

Host side: [openlifu-sdk](https://github.com/OpenwaterHealth/openlifu-sdk)
`LIFUFirmwareUpdate.update()` installs it automatically when it finds a bootloader
older than 1.1.0, waits for the new bootloader's DFU and installs the application.
See `docs/api/LIFUFirmwareUpdate.md`, "Replacing a 1.0.x bootloader".

## What it does

It runs as an ordinary application launched by bootloader 1.0.x at `0x08010400`:

1. It masks interrupts, turns LED_ON on and makes sure the HSI oscillator runs (the F0
   flash needs it to erase and program).
2. It refuses, without writing anything, unless the option bytes are open: RDP level 0
   and no write protection. It then requests DFU and resets, so the old bootloader
   comes back in DFU.
3. It checks the embedded bootloader:
   - its size matches the build;
   - its initial stack pointer is in SRAM;
   - its reset vector is in the bootloader region;
   - it carries the `OWBI` info block at `0x080000E0`.

   The image is in `.rodata`, so the old bootloader already verified it as part of this
   firmware's signature.
4. If pages 0-31 already hold exactly this bootloader, it goes straight to step 6.
5. It rewrites the bootloader from SRAM-resident code (`src/flash_ram.c`):
   - erases pages 0-31;
   - programs the image half-word by half-word from `.rodata`;
   - verifies by read-back, including that the rest of the region reads erased;
   - feeds the watchdog throughout and makes up to three attempts.

   LED_ON and HB_LED are both on during this step: **do not power off**.
6. It writes the bootloader's DFU request (`RTC->BKP0R = 'OWBL'`, `BKP1R = 'DFU!'`) and
   clears the boot-failure counter. From SRAM, it then erases:
   - pages 32-61: the old slot, itself included, and the new preset page;
   - page 63: the anti-rollback floor.

   Then it resets. Page 62 (runtime config: serial number, calibration) is kept. The
   new bootloader starts in DFU with an empty slot, a blank preset page and no floor.
   The first application installed sets the new floor.

If programming fails after the erase, the unit has no bootloader: LED_ON and HB_LED
blink together forever, and the unit needs SWD recovery.

## Flash layout

| Pages | Address | Bootloader 1.0.x | Bootloader 1.1.x | Updater |
|---|---|---|---|---|
| 0-31 | `0x08000000` | bootloader | bootloader | rewritten |
| 32-60 | `0x08010000` | slot | slot | erased (runs here) |
| 61 | `0x0801E800` | slot | presets | erased |
| 62 | `0x0801F000` | runtime config | runtime config | **kept** |
| 63 | `0x0801F800` | anti-rollback floor | anti-rollback floor | erased |

The image (about 52 KB) stays below page 61, so it fits both slots.

## Versions

The **release tag** (e.g. `1.0.0`) versions this repository. The image is signed with
**`FW_VERSION` = 1.2.31** (FwVersion 2143), which `build.yml` pins:

- 1.2.31 is above every fielded console application (1.2.x), so the old bootloader's DFU
  installs it and its floor check accepts it;
- it is below the first application for the new bootloader (1.3.0, 2144), which then
  installs without a downgrade even if the floor were not erased.

## Build

The build needs the Arm GNU Toolchain, CMake and Ninja (the `stm32-build-env` image),
plus the research bootloader binary from its GitHub release:

```sh
gh release download 1.1.0-rc.3 -R OpenwaterHealth/open-lifu-console-bl -p lifu-console-bl.bin -D blimg
cmake --preset Release -DBL_IMAGE=$PWD/blimg/lifu-console-bl.bin -DBL_TAG=1.1.0-rc.3
cmake --build build/Release
bash scripts/check_ramfunc.sh build/Release/open-lifu-console-bl-updater.elf
```

The output is `build/Release/open-lifu-console-bl-updater.bin`, an unsigned slot image.
`scripts/check_ramfunc.sh` fails the build if the SRAM-resident flash code references
anything in flash. Signing it:

```sh
python -m openlifu_sdk.io.LIFUCrypto sign --keys <research keys dir> \
    --firmware build/Release/open-lifu-console-bl-updater.bin --version 1.2.31 --output updater-signed.bin
```

## CI

`.github/workflows/build.yml`:
1. Downloads the embedded bootloader (`BL_RELEASE`) from its GitHub release.
2. Checks that `FW_SIGNING_KEY` is the key the fielded bootloader (`FIELDED_BL`) trusts,
   that it is the embedded bootloader's research key, and that it is not its clinical
   key.
3. Builds, signs with the SDK at `SDK_RELEASE`, then verifies the image with the
   research key and refuses it if it also verifies with the clinical key.
4. Generates the SBOM (`scripts/gen_sbom.py`).
5. On a tag, publishes the GitHub release: `open-lifu-console-bl-updater-signed.bin`,
   the SBOM and `SHA256SUMS`.

`safety-security-scan.yml` runs cppcheck, lizard and a grype scan of the SBOM.

## LEDs

| State | LED_ON (PA10) | HB_LED (PA1) |
|---|---|---|
| Running / checking | on | off |
| Rewriting the bootloader (do not power off) | on | on |
| Done or refused, resetting | off | off |
| Failed after the erase (SWD recovery) | blink | blink |

## Safety notes

- A power loss between the erase and the end of programming leaves the unit with no
  bootloader. The window is a few seconds, and the LEDs show it. Keep the unit on mains
  power and do not unplug USB.
- The updater refuses a unit whose option bytes are not open, so it never touches a
  protected (clinical) unit.
- This is a one-way migration. Once the slot holds an application above 1.2.31, the new
  bootloader will not install the updater again.

## Licence

Apache-2.0, see `LICENSE`. The vendored CMSIS headers (`cmsis/`) are Apache-2.0
(Arm and STMicroelectronics).
