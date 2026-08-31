# Harness fixtures

`umockdev` record files cannot carry comments — every record must begin with a
`P:` line — so the rationale lives here.

## `devices.umockdev` (task 3.5)

Without it the harness enumerates the **host's** devices through libudev, so
every captured surface is machine-specific and the `docs/DESIGN.md` §12.2
long-name / long-path rows are exercised only if the machine happens to have
such hardware. `umockdev-run -d` replaces `/sys` for the wrapped process, so the
app sees exactly these five devices and nothing of the host.

Field derivation is per `platform/linux/src/udev_device_mapper.cpp`:

| Device field | Source, first non-empty wins |
| --- | --- |
| `name` | `ID_MODEL_FROM_DATABASE`, `ID_MODEL`, `product` attr, sysname |
| `vendorId` | `ID_VENDOR_ID`, `idVendor` attr |
| `productId` | `ID_MODEL_ID`, `idProduct` attr |
| `status` | `Disabled` when a usb device has `authorized == "0"` |

The five records and what each exercises:

1. `1-1` — an ordinary USB device.
2. `1-2` — the **long-name** row: an 86-character model name, for elision on
   both surfaces.
3. `1-3.1.4.2.7` — the **long-path** row: a deeply nested syspath.
4. `1-4` — `authorized=0`, so the mapper reports `Disabled` rather than `Active`.
5. `0000:00:02.0` — a PCI device, so bus grouping is exercised with more than
   one bus.

Enumeration matches subsystems `pci`, `usb`, `platform`, `virtio`
(`udev_field_mapping.hpp`), so only the first four subsystems can appear.

## `org.freedesktop.fwupd.conf` (task 3.3)

System-bus policy letting the in-container fwupd double own
`org.freedesktop.fwupd` on the private system bus the harness starts. Copied
into the container at run time; never part of any shipped package.

The double itself is `tests/fwupd/devmgr_fake_fwupd`, which reuses
`FakeFwupdDaemon` — the same double the `devmgr_fwupd` suite tests the real
`FwupdUpdateProvider` against. Reusing it keeps one implementation of the fwupd
contract rather than two that can drift.
