# USB update path and future bootloader choices

Recommendation: retain the installed Maple bootloader for this hardware and
qualify the corrected application/uploader. The false-reset finding was in the
application's entry trigger. Replacing the bootloader is separate work and is
not needed to use PlatformIO, VS Code or eventually STM32CubeIDE.

V2.1.3 uses the complete USB-only SCPI command `SYST:BOOT` followed by LF (CRLF
also works). Extra arguments and question-mark forms are rejected. RS485 cannot
request bootloader entry. The raw `1EAF` receive interrupt hook is disabled.
The existing 500 ms D+ disconnect and reset then enter the installed loader.
The updater selects the legacy handshake only for the one-time transition from
a verified running version at or below V2.1.2; later updates use the SCPI command.

## Practical options

| Option | Fit and tradeoff |
| --- | --- |
| Retain Maple on this board | Smallest change; existing application offset and tested USB update path remain. Our Windows flow still needs the appropriate DFU driver and legacy writer. Qualify an updater/driver package on other PCs before broad field distribution. |
| Replace it with a small USB DFU bootloader in flash | Possible on this MCU. Gives control over device identity, image checks, recovery behavior and supported host tools. Requires a maintained, tested bootloader and may change the flash layout. Install through manufacturing SWD; avoid an unnecessary field bootloader rewrite. |
| Use ST's factory ROM USB DFU on future hardware | My preferred simple option for a redesign when signed updates/rollback are not requirements. Select an exact MCU whose ROM supports USB DFU and provide a recovery entry path that works when the application is broken. The current STM32F103C8 ROM supports USART1, not USB DFU, so this is not a software-only replacement on this board. |
| Use MCUboot on future hardware | Appropriate when authenticated images, upgrade confirmation and rollback are requirements. Adds integration, key management and storage needs. It manages images; a suitable update transport is still needed. Two full application slots cannot fit the current 64 KiB flash with an approximately 54 KiB application. |

ST documents ROM bootloader interfaces by exact MCU in
[AN2606](https://www.st.com/resource/en/application_note/an2606-.pdf).
Its [DFU software documentation](https://www.st.com/en/development-tools/stsw-stm32080.html)
also covers user-flash IAP implementations. A custom loader must implement the
protocol expected by the chosen host tool; arbitrary DFU implementations are not
automatically compatible with STM32CubeProgrammer/DfuSe extensions.
[MCUboot's design](https://docs.mcuboot.com/design.html) describes its image
validation, slot and upgrade strategies.

UART ROM programming and SWD remain manufacturing/service options. They do not
meet this product's USB-only field-update requirement through the existing case.
HID, CDC or UF2 are alternative USB transports for a custom loader; none removes
the need to maintain and qualify the underlying flash/update/recovery logic.

## Requirements for a future production updater

Keep these separate from the current two bug fixes:

- A loader identity that can be matched to a particular unit independently of
  the application, plus board/image compatibility checks.
- Defined recovery after an interrupted application update, without enclosure
  access or a working application. Test actual power interruption.
- Appropriate bootloader write protection and explicit flash bounds.
- A packaged host application/driver flow qualified on the supported operating
  systems and hubs.
- More flash headroom for future commands. The current 54 KiB application budget
  reserves 8 KiB for the bootloader and 2 KiB for settings; V2.1.3 is near that cap.

The current changes retain the bootloader binary and settings layout. They do
not add signature validation, rollback or a new recovery protocol.

The application command can remain `SYST:BOOT` if a future design changes its
bootloader. Its platform reset adapter and the PC uploader would then need the
new loader's entry, device identity, protocol and flash layout.
