using OpenTrail.Loader;

var failures = 0;

void Expect(bool condition, string message)
{
    if (!condition)
    {
        Console.Error.WriteLine($"FAIL: {message}");
        failures++;
    }
}

string ValidDocument(bool flashEnabled = false, string extra = "") => $$"""
{
  "schema": "ot_loader_inspection_view_v0",
  "screen": {
    "title": "OpenTrail Firmware Loader",
    "eyebrow": "Connected devices",
    "phase": "Inspection only",
    "summary": "1 found · 1 inspected · 0 ready to flash",
    "notice": "Inspection is not flash permission."
  },
  "global_actions": {
    "refresh": { "enabled": true },
    "select_firmware": { "enabled": false },
    "flash": { "enabled": {{flashEnabled.ToString().ToLowerInvariant()}} },
    "clean_install": { "enabled": false },
    "recovery": { "enabled": false }
  },
  "candidate_count": 1,
  "inspected_count": 1,
  "ready_to_flash_count": 0,
  "privacy": {
    "local_ports_included": false,
    "serial_numbers_included": false,
    "hardware_instance_ids_included": false,
    "device_locations_included": false,
    "raw_responses_included": false,
    "pairing_data_included": false,
    "device_identity_included": false
  },
  "devices": [{
    "candidate": "usb_candidate_1",
    "display_name": "Heltec V4 OLED",
    "installed_runtime": "MeshCore companion",
    "firmware": "v1.16.0-test",
    "connection": "USB",
    "inspection_status": "Connected and inspected",
    "flash_status": "Blocked",
    "blockers": ["Exact hardware profile required"],
    "actions": { "inspect": true, "flash": false },
    "private_extra": "{{extra}}"
  }]
}
""";

var valid = LoaderInspectionDocument.Parse(ValidDocument(extra: "not-retained"));
Expect(valid.CandidateCount == 1, "valid candidate count");
Expect(valid.Devices[0].DisplayName == "Heltec V4 OLED", "valid board label");
Expect(valid.Devices[0].FirmwareDisplay == "v1.16.0-test", "firmware display");
Expect(!valid.GlobalActions.Flash.Enabled, "Flash remains disabled");

try
{
    _ = LoaderInspectionDocument.Parse(ValidDocument(flashEnabled: true));
    Expect(false, "unexpected global Flash permission must fail");
}
catch (InvalidDataException)
{
}

try
{
    _ = LoaderInspectionDocument.Parse(ValidDocument().Replace(
        "\"ready_to_flash_count\": 0", "\"ready_to_flash_count\": 1"));
    Expect(false, "nonzero ready count must fail");
}
catch (InvalidDataException)
{
}

try
{
    _ = LoaderInspectionDocument.Parse(ValidDocument().Replace(
        "\"local_ports_included\": false", "\"local_ports_included\": true"));
    Expect(false, "private local ports must fail");
}
catch (InvalidDataException)
{
}

var refreshAuthority = new LoaderRefreshAuthority();
var firstRefresh = refreshAuthority.Begin();
Expect(firstRefresh != 0 && refreshAuthority.CanPublish(firstRefresh),
    "a newly started refresh owns publication");

var secondRefresh = refreshAuthority.Begin();
Expect(!refreshAuthority.CanPublish(firstRefresh) &&
    refreshAuthority.CanPublish(secondRefresh),
    "a newer refresh invalidates the older result");

Expect(!refreshAuthority.Complete(firstRefresh) &&
    refreshAuthority.Complete(secondRefresh) &&
    !refreshAuthority.CanPublish(secondRefresh),
    "only the current refresh can complete once");

var closingRefresh = refreshAuthority.Begin();
refreshAuthority.InvalidateAll();
Expect(!refreshAuthority.CanPublish(closingRefresh) &&
    !refreshAuthority.CanPublish(0),
    "window close invalidates all refresh publication");

var boundedText = BoundedTextReader.ReadAsync(
    new StringReader("safe"), 4).GetAwaiter().GetResult();
Expect(boundedText == "safe", "bounded reader accepts its exact safe limit");

try
{
    _ = BoundedTextReader.ReadAsync(
        new StringReader("too large"), 4).GetAwaiter().GetResult();
    Expect(false, "bounded reader must reject excessive helper output");
}
catch (InvalidDataException)
{
}

try
{
    _ = BoundedTextReader.ReadAsync(
        new StringReader(string.Empty), 0).GetAwaiter().GetResult();
    Expect(false, "bounded reader must reject an invalid limit");
}
catch (ArgumentOutOfRangeException)
{
}

if (failures != 0)
{
    Console.Error.WriteLine($"{failures} Windows loader assertion(s) failed");
    return 1;
}

Console.WriteLine("PASS: 11 Windows loader document, refresh, and process-boundary scenario groups");
return 0;
