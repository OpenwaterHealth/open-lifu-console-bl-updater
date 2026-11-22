# open-lifu-console-bl-updater

One-shot firmware for the Open-LIFU **console** (STM32F072RB). It replaces the secure
bootloader from inside the application slot, then resets into the new bootloader's USB
DFU, where the host installs the application.

It moves a research unit to the research bootloader it embeds. The first use is units on
bootloader **1.0.x** (shipped with console app 1.2.6, bootloader `1.0.3-rc.1`) to bootloader **1.1.x**; since it is signed at
1.3.0 (see "Versions"), it also moves a unit on an older 1.1.x research bootloader to the
embedded one, e.g. 1.1.0-rc.N to the 1.1.0 release. Bootloader 1.1.0
moves the end of the slot and adds the preset page, so those units need it before
console app 1.3.x. Both bootloaders trust the same research key, and this image is
signed with it. Clinical units are built with the new bootloader and never run this.

Host side: [openlifu-sdk](https://github.com/OpenwaterHealth/openlifu-sdk)
`LIFUFirmwareUpdate.update()` installs it automatically when it finds a bootloader
older than 1.1.0, waits for the new bootloader's DFU and installs the application.
See `docs/api/LIFUFirmwareUpdate.md`, "Replacing a 1.0.x bootloader".

## What it does

It runs as an ordinary application launched by the research bootloader (1.0.x or 1.1.x) at `0x08010400`:

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
**`FW_VERSION` = 1.3.0** (FwVersion 2144), the version of the console application released
with it. `build.yml` pins it:

- A bootloader refuses an image below the installed application or its anti-rollback floor.
  1.3.0 is at or above every application the updater runs over: console 1.2.x on a 1.0.x
  bootloader, and 1.3.0 on a 1.1.x bootloader (every 1.3.0-rc.N signs as 1.3.0). So it moves
  both to the embedded bootloader.
- It is no higher than the application installed after it. On success the floor is erased
  anyway; if the updater refuses (option bytes not open), nothing is written, the floor is at
  most 1.3.0, and the release application still installs.
- Raise it with the application. When the console application moves to 1.3.1 or later, sign the
  updater at that version, or units on the newer application refuse it. The SDK refuses
  before DFU when the updater is signed below the installed application.

Before this, the updater was signed 1.2.31 (2143): above every fielded 1.2.x application
and below 1.3.0. That only ran on 1.0.x bootloaders; a 1.1.x bootloader with a 1.3.0
application refused it.

## Build

The build needs the Arm GNU Toolchain, CMake and Ninja (the `stm32-build-env` image),
plus the research bootloader binary from its GitHub release:

```sh
gh release download 1.1.0 -R OpenwaterHealth/open-lifu-console-bl -p lifu-console-bl.bin -D blimg
cmake --preset Release -DBL_IMAGE=$PWD/blimg/lifu-console-bl.bin -DBL_TAG=1.1.0
cmake --build build/Release
bash scripts/check_ramfunc.sh build/Release/open-lifu-console-bl-updater.elf
```

The output is `build/Release/open-lifu-console-bl-updater.bin`, an unsigned slot image.
`scripts/check_ramfunc.sh` fails the build if the SRAM-resident flash code references
anything in flash. Signing it:

```sh
python -m openlifu_sdk.io.LIFUCrypto sign --keys <research keys dir> \
    --firmware build/Release/open-lifu-console-bl-updater.bin --version 1.3.0 --output updater-signed.bin
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

## Clinical variant (research unit to clinical)

`clinical-build.yml` builds the same code around the **clinical** bootloader: the clinical
build of `open-lifu-console-bl` from the private artifact bucket, which trusts the Cloud KMS key and locks
the unit down (RDP level 1, write protection) on its first boot. It is still signed with the
**research** key, so a research unit's bootloader (1.0.x or 1.1.x research) launches it.
openlifu-fw-updater runs it to convert a calibrated research unit to clinical: the updater, then
the clinical application, then the clinical presets. With openlifu-sdk that is
`update(signed_app=<clinical app>, bl_updater=<this image>, replace_bootloader=True)`.

- **Version**: signed as **31.63.30** (FwVersion 65534), the top of the range, so the research
  bootloader takes it whatever research application or floor the unit has. Not 31.63.31: that is
  0xFFFF, what erased flash reads as. The updater erases the slot and the floor; the clinical
  application (1.3.0.x, Cloud KMS signed) installs next.
- **Approval and storage**: run by hand on a release tag in the `clinical` environment
  (`gh workflow run clinical-build.yml --ref <tag>`). The outputs (`<repo>-<tag>-clinical-signed.bin`,
  SBOM, `SHA256SUMS`, `clinical-build-manifest.json`) go to
  `gs://openwater-firmware-artifacts/<repo>/<tag>/` only (write-once), never to the GitHub release.
- **Keys**: the run fails unless `FW_SIGNING_KEY` is the research key the fielded and the current
  research bootloaders trust, and not the clinical key the embedded bootloader trusts. The
  manifest records both fingerprints.
- **One way**: once the clinical bootloader has locked the unit, its bootloader pages are write
  protected and this updater cannot run on it again; it refuses on a unit whose option bytes are
  not open in any case.

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
- A bootloader refuses this updater once the slot holds an application above `FW_VERSION`.
  The updater released with that application, signed at its version, covers those units.

## Licence

Apache-2.0, see `LICENSE`. The vendored CMSIS headers (`cmsis/`) are Apache-2.0
(Arm and STMicroelectronics).
