# Limited Underground MeshTrail V1 tester guide

Status: draft for the private two-phone pilot (V1 item A12, issue #76). No tester
kit has been released yet. The kit, versions and digests named here are filled in
for each release in the check message below. The Firmware Loader steps are in the
[Loader tester guide](https://github.com/Limited-Underground/Firmware-Loader/blob/main/tools/tester-package/README.md),
which also ships inside the kit as `README.md`.

## What you receive

| Piece | What it is | How it reaches you |
| --- | --- | --- |
| MeshTrail App | One signed Android APK. | Download link from your test coordinator. |
| Firmware Loader kit | A folder with the Windows installer, Firmware Loader and the signed MeshTrail Firmware for your board. | Download link from your test coordinator. |
| Private assignment | A small file that lets one Windows account update your approved board. | Sent privately to the person who sets up the PC. Never shared. |
| Check message | The details you compare before installing anything. | Sent separately from the links, through the agreed private channel. |
| This guide | What to do, in order. | With the kit. |

## What you need

- The approved Heltec V4 board with its approved antenna, and a data USB cable.
- An Android phone with Android 12 or newer.
- A Windows 11 x64 PC. Someone with an administrator account sets up Firmware
  Loader once.

Ask your coordinator first if anything differs.

## 1. Check the kit

Compare each item with the check message before you open anything. Stop and tell
your coordinator if any value differs or if Windows or Android warns about the
publisher.

| Check | Where to look |
| --- | --- |
| Package manifest SHA-256 | Your administrator checks it during Firmware Loader setup. |
| Installer publisher | Windows shows it when setup starts. |
| APK SHA-256 and signer | Your coordinator can check it with you, or you can compare the file hash. |
| App version | MeshTrail shows it in Help and support. |
| Firmware release generation | Firmware Loader shows it before you confirm the update. |

## 2. Update the board with Firmware Loader

1. Your administrator runs Firmware Loader setup once on the PC with the kit and
   the private assignment.
2. Sign in to Windows with your own account. Open Firmware Loader.
3. Connect only your board. Choose it, check the release generation and confirm.
4. Keep the cable connected until Firmware Loader says **Update verified**.
5. Keep the restore folder from the kit. You only use it when your coordinator
   asks you to.

Follow the [Loader tester guide](https://github.com/Limited-Underground/Firmware-Loader/blob/main/tools/tester-package/README.md)
for each screen and each stop message. If an update stops or cannot be verified,
do not retry. Save a support file and contact your coordinator.

## 3. Install MeshTrail on your phone

1. Open the APK from the link your coordinator sent.
2. If Android asks, allow installs from that app for this one install. Turn the
   permission off again afterwards.
3. Stop if your phone blocks the install. Contact your coordinator.
4. Open MeshTrail and check that the version matches the check message.

## 4. Connect your board

1. Turn on Bluetooth. Allow **Nearby devices** when MeshTrail asks.
2. Follow the setup steps in the app. Compare the code on the board with the code
   on your phone before you confirm.
3. Use only the board and antenna listed in the kit.

## 5. Try it

1. Send a quick status and a short message to the other tester. Ask what reached
   their phone.
2. Try the group and location steps in your test invitation.
3. Share your location only when you choose to.

## 6. If something goes wrong

1. Note the app version, your phone model, what you did and the support code if
   one is shown.
2. In **Help and support**, save or share the operation log. Read it before you
   send it.
3. Send it to the support contact in your invitation: attach it to an email, or
   upload it to your own storage and share a link only with the coordinator.
4. Never send pairing codes, the private assignment or firmware files.

## 7. Stop testing or restore the board

Tell your coordinator. They will send the restore steps for your kit. Restoring
uses Firmware Loader and the restore folder from the kit. Uninstall MeshTrail from
the phone the usual Android way.

## Check message template

The coordinator fills this in for each release and sends it separately from the
download links.

| Field | Value |
| --- | --- |
| Release name | |
| MeshTrail App version | |
| APK file name and size | |
| APK SHA-256 | |
| APK signer certificate SHA-256 | |
| Firmware Loader installer publisher | |
| Loader package manifest SHA-256 | |
| Install release generation | |
| Restore release generation | |
| Approved board (label only) | |
| Support contact | |
