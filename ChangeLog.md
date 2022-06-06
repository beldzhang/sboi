

### 0.02.0003
  ubuntu 22.04: pass
  pop_os 21.10: pass
  manjaro 22.05: pass

  build bootpkg for nboot
  upload to nboot via sboi

  **NOTE**: fstab need fix: remove local entries

### 0.02.0002
  module: re-connect to server
  initrd: parse root-path from reply, allow dynamic setting from dhcp server
  root-path format:
    `sboi://SERVER:PORT`
  archlinux: add hook to build standard initramfs
  nBoot: testing passed.

### 0.02.0001
  protocol v2
  device node change to /dev/sboi0

### 0.01.0001
  initial version, basic concept
