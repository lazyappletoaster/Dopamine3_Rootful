<img src="https://github.com/opa334/Dopamine/assets/52459150/ed04dd3e-d879-456d-9aa3-d4ed44819c7e" width="64" />

# Dopamine - Rootful Edition
Official website / download original Dopamine: https://ellekit.space/dopamine/

A rootful semi-untethered jailbreak for iOS 15.x (arm64 and arm64e). More details will follow here soon.<br>
This version includes full support for tweaks and is rootful.<br>

**Please note that all issues related to version support will be deleted without response.**<br>

# WARNING - ONLY FOR DEVELOPERS
Feel free to fix it if you want to, but note that offsets for userspace PAC.<br>
This thing might break your device, was not tested on iOS 16+<br>
On iOS 16 it will NOT work out-of-box due to missing LaunchConstraints bypass and I dont have a device to develop one. <br>

# Tested Devices and iOS Versions
- iPhone SE 2020: iOS 15.2
- iPhone SE 2016: iOS 15.8.3

# Building
1. Make sure you have Xcode installed
2. Clone this repo
3. `git submodule init`
4. `git submodule update`
5. `cd BaseBin/XPF` and run steps 3 and 4
6. Go back to BaseBin directory
7. Run `make` (note1)
8. Go to the root of the project
9. `cd Packages`
10. `make`
11. Go to the root of the project
12. Go to `Application/Dopamine/Resources`
13. Run `./download_bootstraps.sh`
14. Go to the root of the project
15. Run `make`

Note1: If bootstrapfs doesnt build - correct the sdk/ios version in Makefile

# Installing
Simply install the ipa/tipa file via Xcode or Trollstore

# Bootstrapping
No manual bootstrap extraction is needed. The bootstrap is installed automatically while
jailbreaking, using [bootstrapfs](https://github.com/ghh-jb/bootstrapfs) instead of extracting a
tarball into `/`.

The system volume is SSV (Signed System Volume) sealed and must never be written to. To get
around this, once the launchd hook has been injected, Dopamine runs
`/var/jb/basebin/bootstrapfs create <path> <volume>` for the following directories:

| Directory | APFS volume |
| --- | --- |
| `/usr` | `Usr` |
| `/Library` | `Library` |
| `/Applications` | `Applications` |
| `/private/etc` | `Etc` |
| `/sbin` | `Sbin` |
| `/bin` | `Bin` |

For each of them `bootstrapfs` creates a new APFS volume, copies the stock content of that
directory onto the volume and then mounts the volume over the original path. The bootstrap
archive bundled inside the app (`bootstrap_<version>.tar.zst`, e.g. `bootstrap_1800.tar.zst` for
iOS 15) is decompressed and extracted into `/var/tmp`, and only `usr`, `Library`, `Applications`,
`sbin`, `bin` and `private/etc` from it are merged into the mounted volumes. Every other entry
in the archive is ignored, so nothing is ever written to the sealed system volume and the SSV
seal stays intact.

Notes:
- The volumes only exist while jailbroken. After a full reboot they are unmounted and the device
  boots from the unmodified system volume.
- Each volume contains a `.Dopamine_Rootful` directory that marks it as valid. Do not delete it,
  `bootstrapfs` treats a volume without it as corrupted and recreates the volume.
- The bootstrap is installed once (marked by `/.installed_dopamine` in the jailbreak root), later
  jailbreaks only remount the existing volumes.
- If mounting or installing fails, Dopamine aborts with a `bootstrapfs` error instead of leaving
  a half-written bootstrap behind; fix the reported mount failure and jailbreak again.

After this you will have a rootful bootstrap with the binaries needed for the following steps.

# Fixing up dependencies
In order to install Sileo and your preferred tweak injector (libhooker and ElleKit were tested) you need to install stub packages. Those are basically:
1. cy+cpu.arm64
2. cy+cpu.arm64e
3. gsc.camera-flash
4. cy+model.iphone
5. firmware
6. libkrw0-plugin

Note: libkrw will not work in this case, build it from source if you need its features<br>
To begin using dpkg/apt normally create deb packages with these names in "Package: " field. Here is example:

```
Package: firmware
Name: system_fixes
Description: Compat package for Dopamine_Rootful jailbreak
Maintainer: ghh-jb
Author: ghh-jb
Section: System
Architecture: iphoneos-arm
Version: 15.2
```

Read about how to construct debian package `control` file at https://www.debian.org/doc/debian-policy/ch-controlfields.html<br>
I recommend extractng some other package and modifying its control file.

# Installing Sileo
To install Sileo - visit [iOS Repo Updates](https://www.ios-repo-updates.com/) and download rootful (iphoneos-arm) .deb file from there, share with filza and install the same way you did with the fixup packages<br>
After that you will be able to use it as normal, add sources and install packages.<br>

When done correctly, you should now have a rootful jailbreak which supports all Tweaks!

# Checking if everything works
Install [Amber](https://github.com/PoomSmart/Amber) tweak. It is an excellent thing to test your setup on as it injects to the SpringBoard AND into mediaserverd. If it works - the majority of tweaks will work too.

# Credits
- [Dopamine with subprojects](https://github.com/opa334/Dopamine) - without original repository this would not be possible
- [bootstrapfs](https://github.com/ghh-jb/bootstrapfs) - my tool, used for creating APFS devices and mounting volumes over read-only directories in /

# License
Dopamine: MIT. See the `LICENSE.md` file.
bootsrtapfs: GNU GENERAL PUBLIC LICENSE Version 3. See the `BaseBin/bootstrapfs/LICENSE` file.