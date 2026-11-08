# bootstrapfs
APFS volume creator/bootstrapper/mounter

**DO NOT DELETE .DOPAMINE_ROOTFUL DIRECTORY, IT IS A FLAG THAT PARTITION IS NOT CORRUPTED**

# Tested Devices and iOS Versions
Special thanks to @Sergey5588 for testing this thing for me.
- iPhone 11: iOS 16.3.1 (20D67)
- iPhone SE 2020: iOS 15.2 (19C56)
- iPhone SE 2016: iOS 15.8.4 (19H390)

# Usage
`makerwapfs create {path} {volName}`

**path**: A directory, where you want to mount APFS partition with r/w. <br>
**volName**: Any string to identify the volume. This is needed to stay far away from entering path to disk device manually.

## How it works
On iOS 16 and newer `create` does not touch APFS: the folder is served out of the bootstrap copy inside the jailbreak root (`/usr` -> `<jbroot>/usr`) by pointing its vnode at that copy, and owner / mode are written into the vnode instead of being applied by path. Every read and write the tool performs goes through the vnode, `libjailbreak` and the kernel r/w primitive it needs are picked up at runtime from the jailbreak root.

On iOS 15 and older the tool keeps the volume based path: the APFS volume is created, the folder is copied into it and it is mounted over the folder.

On iOS 16+ a failure is not hidden behind a fallback, the tool stops and returns the error code:

| code | meaning |
| --- | --- |
| 0 | success |
| 1 | no kernel r/w primitive for the vnode |
| 2 | the target folder has no vnode |
| 3 | the bootstrap copy inside the jailbreak root has no vnode |
| 4 | the vnode redirect was refused |
| 5 | writing the vnode attributes failed |

# Safety tips
(Accidents hurt, safety doesn't) 

1. **UIcache**
Especially important if you mount a partition over /Applications

Do never install applications with same bundle identifiers in different places of the system. For example, if you have Filza installed via trollstore and then you copy Filza to /Applications, after running uicache -a you may (or may not?) corrupt your icon cache, after that SpringBoard will stop loading, crashing on startup and you will get a bootloop. 

2. **More to be added??**

# Building 
Prerequisites:
- Make sure you have theos installed
- Make sure you have specified your sdks in Makefile
Now you can simply run `make package` or `make package THEOS_PACKAGE_SCHEME=rootless` depending on your jailbreak environment.
After that install it via sileo/cydia/zebra/ssh.

# License
GNU GENERAL PUBLIC LICENSE Version 3. See the `LICENSE` file.