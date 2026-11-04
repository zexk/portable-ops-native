Portable Ops Native / Portable Ops+ Native — Linux x86-64

Extract the entire archive and launch PortableOpsNative or
PortableOpsPlusNative. Keep lib/, overlays/ and fonts/ beside the program.
No compiler, source checkout or development tools are needed.

On first launch, setup asks for your disc image:
  Portable Ops:  North America, ULUS-10202, version 1.02
  Portable Ops+: North America, ULUS-10290, version 1.01
Setup checks the image, prepares game data, then starts the game. Copying
is the default; if you choose to use the image in place, do not move it.
The archive contains native game code, but no disc image or PSP executable.

Game data, settings and saves live under your user data directory, normally:
  ~/.local/share/PortableOpsNative/ULUS10202/
  ~/.local/share/PortableOpsPlusNative/ULUS10290/
Replacing this program folder does not replace your saves.
Run the program with --install to reopen setup.

A working Vulkan graphics driver is required. BUILDINFO.txt alongside the
archive records the minimum glibc version. The lowercase launcher also
raises the stack limit on systems that need it.

These are local builds. Online play remains incomplete: finding and joining
other players' games is not yet working. Portable Ops+ includes
mpo-server, an experimental server of its own (run it with --help). See licenses/ for bundled software.
