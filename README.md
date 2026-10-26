# vms-vtop

A full-screen, live system monitor for OpenVMS x86-64 and IA64, inspired by
[btop](https://github.com/aristocratos/btop): CPU, memory, disk and network panels with
history graphs, and a sortable process list you can act on, plus what only VMS has
(processor modes, page and swap files, shadow sets, queues, the cluster).

Not a port: btop needs C++20, which neither VMS compiler has. VTOP is new C code that
borrows btop's ideas. Status: Phase 0 (probes); see [docs/PLAN.md](docs/PLAN.md).

MIT licence.
