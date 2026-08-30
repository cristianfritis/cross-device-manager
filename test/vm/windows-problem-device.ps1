<#
.SYNOPSIS
Task 12.9 induction + verification: a Windows device in a problem condition
renders through the SHARED status taxonomy, with no extra status row.

.DESCRIPTION
Run INSIDE the disposable Windows acceptance VM, elevated, NEVER on a host. It
deliberately puts one device into a problem condition and then puts it back, so
take a VM snapshot first.

`windows_device_mapper.cpp` `statusFor()` has two problem branches and this
script exercises both against a live devnode:

  * CM_PROB_DISABLED (22) / CM_PROB_HARDWARE_DISABLED (29) -> DeviceStatus::Disabled
    "switched off" is not a fault.
  * every other problem code                               -> DeviceStatus::Error
    induced here as CM_PROB_FAILED_INSTALL (28), by deleting the driver package.

Only the unit suite has covered these branches
(tests/unit/test_windows_device_mapper.cpp: DisabledProblemMapsToDisabled,
OtherProblemsMapToError, ProblemConditionAddsNoStatusDetailRow). This is the
live half of that same evidence.

TARGET SELECTION. Not every devnode can be faulted, and picking a bad one is the
first thing that goes wrong:

  * `Disable-PnpDevice` fails with "Generic failure / HRESULT 0x80041001" when
    the devnode does not support being disabled at all. Root-enumerated and
    bus-fixed devices — a VirtIO serial port among them — are exactly that case.
    The provider does not say so; it just fails. -ListCandidates below decodes
    DEVPKEY_Device_Capabilities so you can see it before you try.
  * Branch B needs a PUBLISHED driver package (an oemNN.inf in the driver store)
    to export, delete and restore. A device on an inbox driver has none, and
    deleting an inbox package is not on.

A passed-through USB device satisfies both: it is removable, so it disables, and
it usually carries a published package. That is what this script prefers, and
the same passthrough the 12.2/12.3 hotplug rows already used supplies it.

The CLI half is asserted automatically. The GUI half is a checklist the script
prints and pauses on, because "no extra status row in the detail pane" is a
thing an owner has to look at. Note there is no colour to check on Windows: the
GUI carries device status as the WORD in the detail pane's `Status:` row
(docs/DESIGN.md section 9 GUI colour exception), and the TUI, which is where the
role colour and glyph live, is not built on Windows.

.PARAMETER ListCandidates
Enumerate present devices with their capability bits and driver package, say
which branches each one can serve, and exit without touching anything. Start
here after a target refuses.

.PARAMETER Devmgr
Path to devmgr.exe. Defaults to the Windows build tree's CLI.

.PARAMETER InstanceId
Device instance ID to abuse. Default: the best USB candidate -ListCandidates
would report — removable, not an input device, ideally with a published driver.

.PARAMETER OutDir
Where the JSON evidence lands. Defaults to .pi\vm-artifacts\windows\12-9-problem-device.

.PARAMETER SkipErrorBranch
Run only the Disabled branch (22). The Error branch is the one that faults the
device, so this is the way to rehearse without doing that.

.PARAMETER ErrorViaService
Induce the Error branch by stopping the device's DRIVER SERVICE instead of
deleting a driver package. This is the route for a device on an INBOX driver —
a USB mass storage reader, say — which has no oemNN.inf to export and delete,
and where deleting the inbox package is not on.

It sets the service's `Start` to 4 (disabled) under
HKLM\SYSTEM\CurrentControlSet\Services, restarts the devnode so Windows
re-attempts the start, and the failed start shows up as CM_PROB_DISABLED_SERVICE
(32) or CM_PROB_FAILED_START (10) — either way a fault, not a switch-off, which
is exactly the `statusFor()` arm branch B exists to prove. The original `Start`
value is saved first and restored in the finally block.

It touches a service, so every device bound to that service is affected for the
duration — all USB mass storage, for USBSTOR. On the disposable VM that is fine
and it is fully reversible; services the machine needs to keep running are
refused outright.

.EXAMPLE
.\windows-problem-device.ps1 -ListCandidates

.EXAMPLE
.\windows-problem-device.ps1 -InstanceId 'USB\VID_046D&PID_C52B\5&1a2b3c4d&0&2'
#>

[CmdletBinding()]
param(
    [switch] $ListCandidates,
    [string] $Devmgr = "$PSScriptRoot\..\..\build\cli\Debug\devmgr.exe",
    [string] $InstanceId,
    [string] $OutDir = "$PSScriptRoot\..\..\.pi\vm-artifacts\windows\12-9-problem-device",
    [switch] $SkipErrorBranch,
    [switch] $ErrorViaService
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# --- device capability decoding ---------------------------------------------

# DEVPKEY_Device_Capabilities bits (cfgmgr32.h). Only the ones that decide
# whether this script can do anything with the device are named.
$CM_DEVCAP_REMOVABLE        = 0x00000004
$CM_DEVCAP_SURPRISEREMOVALOK = 0x00000080
$CM_DEVCAP_HARDWAREDISABLED = 0x00000100
$CM_DEVCAP_NONDYNAMIC       = 0x00000200

# Never fault these, over USB or not: the VM has to keep running, a faulted input
# device leaves nobody able to answer the GUI checklist, and a faulted NIC ends
# the session. Storage is refused because the boot volume may be behind it.
$RefusedClasses = @('DiskDrive', 'SCSIAdapter', 'System', 'Computer', 'Processor',
                    'HDC', 'Volume', 'Net', 'HIDClass', 'Keyboard', 'Mouse',
                    'Display', 'Monitor')

# Refused by role rather than class: a hub or host controller IS the passthrough
# path, so disabling one takes the target down with it. Class alone does not
# separate "USB Mass Storage Device" (a fine target) from "USB Root Hub".
$RefusedNamePattern = 'Hub|Host Controller|Root Complex|Composite Device'

function Get-DeviceCapabilities([string] $instance) {
    $p = Get-PnpDeviceProperty -InstanceId $instance -KeyName 'DEVPKEY_Device_Capabilities' `
         -ErrorAction SilentlyContinue
    if ($null -eq $p) { return 0 }
    [int] $p.Data
}

function Get-ProblemCode([string] $instance) {
    $p = Get-PnpDeviceProperty -InstanceId $instance -KeyName 'DEVPKEY_Device_ProblemCode' `
         -ErrorAction SilentlyContinue
    if ($null -eq $p) { return $null }
    [int] $p.Data
}

# The oemNN.inf this device's driver came from, or $null for an inbox driver.
function Get-PublishedDriver([string] $instance) {
    $enum = & pnputil /enum-devices /instanceid "$instance" /drivers 2>$null
    if ($LASTEXITCODE -ne 0 -or $null -eq $enum) { return $null }
    ($enum | Select-String -Pattern 'oem\d+\.inf' -AllMatches |
        ForEach-Object { $_.Matches.Value } | Select-Object -First 1)
}

# The kernel service this devnode is bound to — USBSTOR for a mass storage
# reader — or $null when it has none.
function Get-DeviceService([string] $instance) {
    $p = Get-PnpDeviceProperty -InstanceId $instance -KeyName 'DEVPKEY_Device_Service' `
         -ErrorAction SilentlyContinue
    if ($null -eq $p -or -not $p.Data) { return $null }
    [string] $p.Data
}

# Services the VM needs to keep running, or that carry the input path. Faulting
# one of these does not produce a test, it produces a machine nobody can drive.
$RefusedServices = @('disk', 'storahci', 'stornvme', 'volsnap', 'volmgr', 'partmgr',
                     'pci', 'acpi', 'ntfs', 'fltmgr', 'tcpip', 'netbt',
                     'hidusb', 'kbdclass', 'kbdhid', 'mouclass', 'mouhid',
                     'usbhub', 'usbxhci', 'usbehci', 'usbccgp', 'vioser')

# Why this device can or cannot serve each branch. The reason matters more than
# the verdict: "generic failure" with no explanation is what sent the first run
# into the weeds.
function Get-Candidacy($device) {
    $caps = Get-DeviceCapabilities $device.InstanceId
    $removable = ($caps -band $CM_DEVCAP_REMOVABLE) -ne 0
    $nondynamic = ($caps -band $CM_DEVCAP_NONDYNAMIC) -ne 0
    $alreadyOff = ($caps -band $CM_DEVCAP_HARDWAREDISABLED) -ne 0
    $oem = Get-PublishedDriver $device.InstanceId

    $canDisable = $true
    $why = ''
    $name = if ($device.FriendlyName) { $device.FriendlyName } else { '' }
    if ($device.Class -in $RefusedClasses) {
        $canDisable = $false; $why = "class $($device.Class) is refused"
    } elseif ($name -match $RefusedNamePattern) {
        $canDisable = $false; $why = 'hub or controller — disabling it takes the passthrough down'
    } elseif ($alreadyOff) {
        $canDisable = $false; $why = 'already hardware-disabled'
    } elseif ($nondynamic -and -not $removable) {
        $canDisable = $false; $why = 'non-dynamic and not removable — Disable-PnpDevice returns 0x80041001'
    } elseif (-not $removable -and $device.InstanceId -notlike 'USB\*') {
        $canDisable = $false; $why = 'not removable — Disable-PnpDevice usually returns 0x80041001'
    }

    [pscustomobject]@{
        InstanceId   = $device.InstanceId
        FriendlyName = $device.FriendlyName
        Class        = $device.Class
        Status       = $device.Status
        Capabilities = ('0x{0:X8}' -f $caps)
        Removable    = $removable
        Driver       = if ($oem) { $oem } else { '(inbox)' }
        CanDisable   = $canDisable
        CanFault     = [bool] $oem
        Why          = $why
    }
}

function Get-Candidates {
    Get-PnpDevice -PresentOnly |
        Where-Object { $_.Status -eq 'OK' -and $_.InstanceId } |
        ForEach-Object { Get-Candidacy $_ }
}

# --- listing mode -----------------------------------------------------------

if ($ListCandidates) {
    Write-Host ''
    Write-Host 'Devices that can serve branch A (disable => code 22):' -ForegroundColor Cyan
    Get-Candidates | Where-Object { $_.CanDisable } |
        Sort-Object -Property @{Expression = { $_.CanFault }; Descending = $true}, FriendlyName |
        Format-Table FriendlyName, Class, Removable, Driver, CanFault, InstanceId -AutoSize -Wrap
    Write-Host 'CanFault = has a published oemNN.inf, so branch B (delete driver => code 28) works too.'
    Write-Host ''
    Write-Host 'Rejected, with the reason:' -ForegroundColor DarkGray
    Get-Candidates | Where-Object { -not $_.CanDisable -and $_.Why } |
        Format-Table FriendlyName, Class, Capabilities, Why -AutoSize -Wrap
    exit 0
}

# --- guards -----------------------------------------------------------------

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not ([Security.Principal.WindowsPrincipal]$identity).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this from an elevated PowerShell: it disables a device and removes a driver package.'
}
if (-not (Test-Path $Devmgr)) { throw "devmgr.exe not found at $Devmgr — pass -Devmgr." }

Write-Host ''
Write-Host 'This script puts a device into a problem condition and restores it.' -ForegroundColor Yellow
Write-Host 'It is for the disposable acceptance VM only. Take a VM snapshot first.' -ForegroundColor Yellow
$answer = Read-Host 'Type the word VM to continue'
if ($answer -ne 'VM') { Write-Host 'Aborted.'; exit 2 }

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# --- helpers ----------------------------------------------------------------

$script:failures = @()
# Set by the service route so the restore can verify its own work.
$script:serviceKey = $null
$script:originalStart = $null

function Assert([bool] $ok, [string] $what) {
    if ($ok) {
        Write-Host "  PASS  $what" -ForegroundColor Green
    } else {
        Write-Host "  FAIL  $what" -ForegroundColor Red
        $script:failures += $what
    }
}

# devmgr's device id is fnv1a64 over the case-folded instance id, so it is
# stable across a driver change as long as the instance id itself is. Look it up
# by identity anyway rather than assuming — a re-enumerated devnode is exactly
# the case where the assumption would be wrong.
function Get-DevmgrDevice([string] $instance) {
    $json = & $Devmgr devices list --json
    if ($LASTEXITCODE -ne 0) { throw "devmgr devices list --json exited $LASTEXITCODE" }
    $all = $json | ConvertFrom-Json
    $all | Where-Object { $_.PSObject.Properties.Name -contains 'identity' -and
                          $_.identity -ieq $instance } | Select-Object -First 1
}

# The whole point of the row: the shared taxonomy carries the state, so the
# detail vocabulary must not publish a second status/problem field beside it.
function Assert-SharedStatus($device, [string] $expected, [string] $label) {
    Assert ($null -ne $device) "$label — devmgr still lists the device"
    if ($null -eq $device) { return }
    Assert ($device.status -eq $expected) "$label — status is '$expected' (got '$($device.status)')"

    $detailLabels = @()
    if ($device.PSObject.Properties.Name -contains 'details') {
        $detailLabels = @($device.details | ForEach-Object { $_.label })
    }
    # @() around the pipeline, not around the variable afterwards: under
    # Set-StrictMode a Where-Object that matched nothing yields $null and one
    # that matched once yields a scalar, and .Count on either throws
    # "The property 'Count' cannot be found on this object" — which is exactly
    # how the first live run died AFTER branch A had already passed.
    $extra = @($detailLabels | Where-Object { $_ -match 'Status|Problem|Code' })
    Assert ($extra.Count -eq 0) `
        "$label — no extra status row in the detail fields (offenders: $($extra -join ', '))"

    $raw = @($detailLabels | Where-Object { $_ -match 'DEVPKEY|DEVPROP|CM_PROB' })
    Assert ($raw.Count -eq 0) "$label — no raw platform key in a detail label"
}

function Save-Evidence([string] $name, $content) {
    $path = Join-Path $OutDir $name
    $content | Out-File -FilePath $path -Encoding utf8
    Write-Host "  evidence: $path" -ForegroundColor DarkGray
}

# --- target -----------------------------------------------------------------

if (-not $InstanceId) {
    # Prefer a USB devnode that can serve BOTH branches, then any that can
    # disable. A bare device interface (an instance id with no & separated
    # instance part) is skipped: it is not the devnode the enumerator reports.
    $best = Get-Candidates |
        Where-Object { $_.CanDisable -and $_.InstanceId -like 'USB\*' } |
        Sort-Object -Property @{Expression = { $_.CanFault }; Descending = $true} |
        Select-Object -First 1

    # No silent fallback to a non-USB device. Everything left on a clean VM is
    # something the owner may well not want faulted — the virtual printers are
    # the only CanFault devices here, and auto-picking one would fault a device
    # nobody chose. Name it with -InstanceId to use it deliberately.
    if ($null -eq $best) {
        Write-Host ''
        Write-Host 'No USB candidate is present, and this script will not pick a non-USB device for you.' -ForegroundColor Red
        Write-Host 'Plug in a USB device through the same passthrough the 12.2/12.3 rows used, then re-run.' -ForegroundColor Yellow
        $others = @(Get-Candidates | Where-Object { $_.CanDisable -and $_.CanFault })
        if ($others) {
            Write-Host ''
            Write-Host 'Or pass one of these explicitly with -InstanceId (both branches would work):' -ForegroundColor Yellow
            $others | Format-Table FriendlyName, Class, Driver, InstanceId -AutoSize -Wrap
        }
        exit 3
    }
    $InstanceId = $best.InstanceId
}

$candidacy = Get-Candidacy (Get-PnpDevice -InstanceId $InstanceId)
Write-Host ''
Write-Host "Target: $($candidacy.FriendlyName)"
Write-Host "        $InstanceId"
Write-Host "        class=$($candidacy.Class) caps=$($candidacy.Capabilities) removable=$($candidacy.Removable)"
Write-Host "        driver=$($candidacy.Driver)"

if (-not $candidacy.CanDisable) {
    throw "This device cannot be disabled: $($candidacy.Why). Run -ListCandidates and pick another."
}
$targetService = Get-DeviceService $InstanceId
if ($targetService) { Write-Host "        service=$targetService" }

if (-not $candidacy.CanFault -and -not $SkipErrorBranch -and -not $ErrorViaService) {
    Write-Host ''
    Write-Host 'This device runs an inbox driver, so there is no package to delete and the' -ForegroundColor Yellow
    Write-Host 'driver-package route for branch B cannot run against it.' -ForegroundColor Yellow
    if ($targetService -and $targetService.ToLower() -notin $RefusedServices) {
        Write-Host ''
        Write-Host "Re-run with -ErrorViaService to fault it through its '$targetService' service" -ForegroundColor Yellow
        Write-Host 'instead — no driver store change, and the original Start value is restored.' -ForegroundColor Yellow
    }
    Write-Host 'Branch A will run on its own.' -ForegroundColor Yellow
    $SkipErrorBranch = $true
}
if ($ErrorViaService -and -not $SkipErrorBranch) {
    if (-not $targetService) {
        throw 'This device is bound to no service, so -ErrorViaService has nothing to stop.'
    }
    if ($targetService.ToLower() -in $RefusedServices) {
        throw "Refusing to disable service '$targetService': the VM needs it. Pick another device."
    }
}

$baseline = Get-DevmgrDevice $InstanceId
Assert ($null -ne $baseline) 'baseline — devmgr enumerates the target device'
if ($null -eq $baseline) { throw 'devmgr does not list the target; nothing to compare against.' }
Write-Host "        devmgr id=$($baseline.id) status=$($baseline.status)"
Save-Evidence '00-baseline.json' (& $Devmgr devices show $baseline.id --json)

$restore = @()

try {
    # --- branch A: CM_PROB_DISABLED (22) -> Disabled ------------------------

    Write-Host ''
    Write-Host 'Branch A — disable the device (CM_PROB_DISABLED 22) => Disabled' -ForegroundColor Cyan
    try {
        Disable-PnpDevice -InstanceId $InstanceId -Confirm:$false
    } catch {
        Write-Host ''
        Write-Host "  Disable-PnpDevice refused: $($_.Exception.Message)" -ForegroundColor Red
        Write-Host '  0x80041001 here means the devnode does not support being disabled,' -ForegroundColor Yellow
        Write-Host '  whatever its capability bits claimed. Run -ListCandidates and pick another.' -ForegroundColor Yellow
        throw
    }
    $restore += { Enable-PnpDevice -InstanceId $InstanceId -Confirm:$false }
    Start-Sleep -Seconds 2

    $code = Get-ProblemCode $InstanceId
    Write-Host "  Windows problem code: $code"
    Assert ($code -eq 22) 'branch A — Windows reports CM_PROB_DISABLED (22)'

    $disabled = Get-DevmgrDevice $InstanceId
    Assert-SharedStatus $disabled 'Disabled' 'branch A'
    Save-Evidence '01-disabled-22.json' (& $Devmgr devices show $baseline.id --json)

    Write-Host ''
    Write-Host '  GUI check (branch A) — leave this window open, start devmgr-gui, and confirm:' -ForegroundColor Yellow
    Write-Host '    - the device is still listed on Devices'
    Write-Host '    - its detail pane reads exactly "Status:       Disabled"'
    Write-Host '    - there is no second status/problem row anywhere in that pane'
    Write-Host "    - screenshot to $OutDir\12-9-gui-disabled.png"
    Read-Host '  Press Enter when the GUI check is done'

    Enable-PnpDevice -InstanceId $InstanceId -Confirm:$false
    $restore = @()
    Start-Sleep -Seconds 3
    $back = Get-DevmgrDevice $InstanceId
    Assert ($null -ne $back -and $back.status -eq 'Active') `
        "branch A — re-enabled back to Active (got '$(if ($back) { $back.status } else { 'absent' })')"

    if ($SkipErrorBranch) {
        Write-Host ''
        Write-Host 'Skipping the Error branch.' -ForegroundColor Yellow
    } else {

    # --- branch B: CM_PROB_FAILED_INSTALL (28) -> Error ---------------------
    #
    # Export the driver package BEFORE deleting it, so the restore is this
    # script's job and not the VM snapshot's.

    if ($ErrorViaService) {

    # --- branch B, service route: a failed driver start -> Error ------------
    #
    # For a device on an inbox driver there is no package to delete, so the
    # fault is induced one level down: the service it binds to is set to
    # Start=4 and the devnode is restarted. Windows cannot start the driver and
    # marks the devnode CM_PROB_DISABLED_SERVICE (32) or CM_PROB_FAILED_START
    # (10) — a fault either way, which is the arm being proved. Nothing in the
    # driver store is touched, and the original Start goes back in `finally`.

    Write-Host ''
    Write-Host "Branch B — disable the '$targetService' service (failed start) => Error" -ForegroundColor Cyan

    $serviceKey = "HKLM:\SYSTEM\CurrentControlSet\Services\$targetService"
    if (-not (Test-Path $serviceKey)) { throw "No service key at $serviceKey." }
    $originalStart = (Get-ItemProperty -Path $serviceKey -Name Start).Start
    # Script scope so the finally block can CHECK the value went back, rather
    # than assuming the restore scriptblock did its job.
    $script:serviceKey = $serviceKey
    $script:originalStart = $originalStart
    Write-Host "  $targetService Start = $originalStart (saved)"
    $restore += {
        Set-ItemProperty -Path $serviceKey -Name Start -Value $originalStart
        Enable-PnpDevice -InstanceId $InstanceId -Confirm:$false -ErrorAction SilentlyContinue
        & pnputil /scan-devices | Out-Null
    }

    Set-ItemProperty -Path $serviceKey -Name Start -Value 4
    # Cycle the devnode so Windows re-attempts the start it can no longer do.
    Disable-PnpDevice -InstanceId $InstanceId -Confirm:$false
    Start-Sleep -Seconds 2
    Enable-PnpDevice -InstanceId $InstanceId -Confirm:$false -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 4

    $code = Get-ProblemCode $InstanceId
    # A cycle does not always make Windows re-read the service key. A physical
    # replug always does, and this VM has the passthrough the 12.2/12.3 rows used.
    if ($null -eq $code -or $code -eq 0 -or $code -eq 22) {
        Write-Host ''
        Write-Host "  Problem code is $code — the devnode has not re-attempted the start yet." -ForegroundColor Yellow
        Write-Host '  Physically UNPLUG the device, wait a moment, then PLUG IT BACK IN.' -ForegroundColor Yellow
        Read-Host '  Press Enter once it is plugged back in'
        Start-Sleep -Seconds 4
        $code = Get-ProblemCode $InstanceId
    }

    Write-Host "  Windows problem code: $code"
    Assert ($null -ne $code -and $code -ne 0) 'branch B — Windows reports a problem condition'
    Assert ($code -ne 22 -and $code -ne 29) `
        "branch B — the problem is a fault, not a switch-off (code $code)"

    $faulted = Get-DevmgrDevice $InstanceId
    Assert-SharedStatus $faulted 'Error' 'branch B'
    if ($null -ne $faulted) {
        Save-Evidence '02-error-service-fault.json' (& $Devmgr devices show $faulted.id --json)
        Assert ($faulted.id -eq $baseline.id) `
            'branch B — the devmgr id is unchanged across the fault (identity is the instance id, not the driver)'
    }

    Write-Host ''
    Write-Host '  GUI check (branch B) — in devmgr-gui, confirm:' -ForegroundColor Yellow
    Write-Host '    - the faulted device is listed on Devices'
    Write-Host '    - its detail pane reads exactly "Status:       Error"'
    Write-Host '    - no second status/problem row, and no Windows-specific fault wording'
    Write-Host "    - screenshot to $OutDir\12-9-gui-error.png"
    Read-Host '  Press Enter when the GUI check is done'

    } else {

    Write-Host ''
    Write-Host 'Branch B — delete the driver package (CM_PROB_FAILED_INSTALL 28) => Error' -ForegroundColor Cyan

    $oem = Get-PublishedDriver $InstanceId
    if (-not $oem) {
        throw "No oemNN.inf published driver for $InstanceId — it is inbox, so deleting it is not on. Re-run with -ErrorViaService, or -SkipErrorBranch, or pick another device."
    }
    Write-Host "  driver package: $oem"

    $backup = Join-Path $OutDir "driver-backup-$($oem -replace '\.inf$','')"
    New-Item -ItemType Directory -Force -Path $backup | Out-Null
    & pnputil /export-driver $oem $backup | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "pnputil /export-driver $oem failed ($LASTEXITCODE) — refusing to delete a package I cannot put back." }
    $inf = Get-ChildItem -Path $backup -Filter *.inf -Recurse | Select-Object -First 1
    if (-not $inf) { throw "Export produced no .inf under $backup — refusing to delete the package." }
    Write-Host "  exported to: $($inf.FullName)"

    $restore += {
        & pnputil /add-driver $inf.FullName /install | Out-Null
        & pnputil /scan-devices | Out-Null
    }

    & pnputil /remove-device "$InstanceId" | Out-Null
    & pnputil /delete-driver $oem /uninstall /force | Out-Null
    & pnputil /scan-devices | Out-Null
    Start-Sleep -Seconds 5

    $code = Get-ProblemCode $InstanceId
    Write-Host "  Windows problem code: $code"
    Assert ($null -ne $code -and $code -ne 0) 'branch B — Windows reports a problem condition'
    Assert ($code -ne 22 -and $code -ne 29) `
        "branch B — the problem is a fault, not a switch-off (code $code)"

    $faulted = Get-DevmgrDevice $InstanceId
    Assert-SharedStatus $faulted 'Error' 'branch B'
    if ($null -ne $faulted) {
        Save-Evidence '02-error-fault.json' (& $Devmgr devices show $faulted.id --json)
        Assert ($faulted.id -eq $baseline.id) `
            'branch B — the devmgr id is unchanged across the fault (identity is the instance id, not the driver)'
    }

    Write-Host ''
    Write-Host '  GUI check (branch B) — in devmgr-gui, confirm:' -ForegroundColor Yellow
    Write-Host '    - the faulted device is listed on Devices'
    Write-Host '    - its detail pane reads exactly "Status:       Error"'
    Write-Host '    - no second status/problem row, and no Windows-specific fault wording'
    Write-Host "    - screenshot to $OutDir\12-9-gui-error.png"
    Read-Host '  Press Enter when the GUI check is done'

    }
    }
} finally {
    if ($restore.Count -gt 0) {
        Write-Host ''
        Write-Host 'Restoring the device...' -ForegroundColor Cyan
        # Newest first: the driver package goes back before the enable retries.
        [array]::Reverse($restore)
        foreach ($step in $restore) {
            # A restore step that fails is a FAILURE OF THE RUN, not a note in
            # passing: it leaves the machine faulted. The first live run printed
            # this in red and still exited 0, which is the one outcome a gate
            # script must never produce.
            try { & $step } catch { Assert $false "restore step failed: $($_.Exception.Message)" }
        }
        Start-Sleep -Seconds 5

        # Writing the Start value back does not by itself make Windows re-read
        # it — the same reason inducing the fault needed a devnode cycle. So
        # verify the value, then cycle, then fall back to the physical replug
        # this VM has.
        if ($script:serviceKey) {
            $now = (Get-ItemProperty -Path $script:serviceKey -Name Start -ErrorAction SilentlyContinue).Start
            Assert ($now -eq $script:originalStart) `
                "restore — $targetService Start is back to $($script:originalStart) (got '$now')"
        }
        if (Get-ProblemCode $InstanceId) {
            Write-Host '  still faulted after the restore steps — cycling the devnode' -ForegroundColor Yellow
            Disable-PnpDevice -InstanceId $InstanceId -Confirm:$false -ErrorAction SilentlyContinue
            Start-Sleep -Seconds 2
            Enable-PnpDevice -InstanceId $InstanceId -Confirm:$false -ErrorAction SilentlyContinue
            & pnputil /scan-devices | Out-Null
            Start-Sleep -Seconds 4
        }
        if (Get-ProblemCode $InstanceId) {
            Write-Host ''
            Write-Host '  Still faulted. Physically UNPLUG the device and PLUG IT BACK IN.' -ForegroundColor Yellow
            Read-Host '  Press Enter once it is plugged back in'
            Start-Sleep -Seconds 4
        }
    }
}

# --- roll-up ----------------------------------------------------------------

$final = Get-DevmgrDevice $InstanceId
$finalStatus = if ($final) { $final.status } else { 'absent' }
Write-Host ''
# An assertion, not a printed remark: leaving the machine faulted has to fail
# the run, or a green exit means nothing.
Assert ($null -ne $final -and $final.status -eq 'Active') `
    "restore — the device is back to Active (got '$finalStatus'); if this failed, roll the VM snapshot back"


$build = (Get-CimInstance Win32_OperatingSystem)
Write-Host ''
Write-Host "Windows: $($build.Caption) $($build.Version) (build $($build.BuildNumber))"

if ($script:failures.Count -eq 0) {
    Write-Host '12.9 CLI HALF OK — the problem branches exercised above render through the shared taxonomy.' -ForegroundColor Green
    exit 0
}
Write-Host "12.9 FAILED — $($script:failures.Count) assertion(s):" -ForegroundColor Red
$script:failures | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
exit 1
