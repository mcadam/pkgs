Digigram/Audinate Dante PCIe driver -- patched for modern kernels
==================================================================
This is a patched version of Audinate's dante-pcie-1.2.4rc03 driver
source, fixed up for compatibility with modern kernels (originally
patched for Ubuntu 24.04 / kernel 6.8). See dante-pcie.c.patch for the
exact diff against the original vendor source. The fixes cover:

  - ALSA buffer allocation modernized for kernel >= 5.14
    (snd_pcm_lib_malloc_pages/free_pages -> snd_pcm_set_managed_buffer_all)
  - ioremap_nocache -> ioremap (removed in newer kernels)
  - MSI interrupt fixup guarded for kernel >= 5.17
    (msi_list/msi_attrib were removed from the kernel's MSI struct;
    the Gennum fixup is skipped on 5.17+ rather than failing to build)

Installing on a new machine
============================
    sudo ./install.sh

That installs dkms + matching kernel headers, registers this source
with dkms (package "dante-pcie", version "1.2.4rc03"), builds it,
installs it, and loads it. From then on, every future kernel update
on that machine rebuilds dante-pcie automatically before you even
reboot into the new kernel -- no manual rebuild step required.

NOTE: install.sh only handles the kernel module. It does NOT restore
the ALSA config (asound.conf / DantePCIe.conf) needed for the card to
be usable through ALSA -- restore that separately from its own backup.

Checking status
================
    dkms status
    cat /proc/asound/cards

Manually loading/unloading the module (troubleshooting only --
normal operation is handled automatically by dkms/modprobe)
=============================================================
The module depends on other ALSA modules, load once if needed:
    root# modprobe snd_pcm
To load manually:
    root# insmod kmod/dante-pcie.ko
Check dmesg for confirmation:
    $ dmesg
    .....
    [   33.348670] dante_pcie: module license 'Proprietary' taints kernel.
    [   33.348681] Disabling lock debugging due to kernel taint
    [   33.348728] dante_pcie: module verification failed: signature and/or required key missing - tainting kernel
    [   33.350181] Dante PCIe Soundcard Driver v1.2.4rc02 (0x20010010)
To unload:
    root# rmmod dante-pcie

Testing the Dante PCIe Card
============================
NOTE: The "arecord" tool is not installed by default.
You may need to install it with the following command:
          sudo apt-get install alsa-utils
Configuration of sample rate, audio routing and other parameters
is done using the Dante Controller application. Dante Controller
can be downloaded from the Audinate website (www.audinate.com).
The Dante PCIe card is configured via the network interface on the
card. Dante Controller must therefore be run on a computer connected
to the same network as the Dante PCIe card.
The Dante PCIe card appears as a standard ALSA device:
    audinate@juno:~$ arecord -l
    **** List of CAPTURE Hardware Devices ****
    ...
    card 1: DantePCIe [DantePCIe], device 0: DantePCIe [DantePCIe PCM]
      Subdevices: 1/1
      Subdevice #0: subdevice #0
NOTE: the Dante PCIe card uses the ALSA MMAP_NONINTERLEAVED access
NOTE: method for maximum performance.
Example, recording 5 seconds of audio from the Dante PCIe card:
    audinate@juno:~$ arecord -v -M -D hw:1,0 -f S32_LE -r 48000 -d 5 out.wav
Arguments of particular note are:
    -M          use MMAP to access the audio
    -D hw:1,0   device name, obtained from arecord -l
    -f S32_LE   use 32 bit audio
    -r 48000    use 48kHz sample rate
    -c 2        record 2 channels

Other Notes
============
Low-latency audio performance depends strongly on software
configuration and the number of competing tasks running on the
machine. You may need to tune the system to disable competing or
background tasks such as:
  - periodic syncing of filesystem data to disk
  - backing up files
  - re-indexing manual pages, etc
  - your window system
  - desktop applications (e.g. web browser)
  - etc.
