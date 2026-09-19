"""Patch MAME's upd765 family core (src/devices/machine/upd765.cpp) for the Wholehog II's polled (PIO) floppy driver.
usage: patch_upd765.py <path to upd765.cpp>

1. MSR: the non-DMA execution-mode bit (EXM/NDM) was only reported during sector data transfers.  During FORMAT TRACK
   the host also has to feed ID bytes through the FIFO; without EXM the Hog's state machine takes the RQM as a request
   for the next *command* byte and aborts the format.
2. FORMAT TRACK asked for the first sector's four ID bytes twice (once in WRITE_TRACK_PRE_SECTORS and again at
   WRITE_TRACK_SECTOR start), so a host supplying exactly 4 x SC bytes ran out one sector early.
"""
import sys
p = sys.argv[1]
s = open(p).read()
def rep(old, new):
    global s
    if new in s:
        print("already applied:", new.strip()[:60]); return
    assert old in s, old[:60]
    s = s.replace(old, new, 1)

rep("""		if((spec & SPEC_ND) && xfer_in_progress)
			msr |= MSR_EXM;""",
"""		if((spec & SPEC_ND) && (xfer_in_progress || internal_drq))   // wholehog2: format track needs EXM too
			msr |= MSR_EXM;""")
rep("""		case WRITE_TRACK_SECTOR:
			if(!cur_live.byte_counter) {
				command[3]--;
				fifo_expect(4, true);
			}""",
"""		case WRITE_TRACK_SECTOR:
			if(!cur_live.byte_counter) {
				command[3]--;
				if(fifo_pos + fifo_expected < 4)   // the first sector's ID bytes were already requested in WRITE_TRACK_PRE_SECTORS
					fifo_expect(4, true);
			}""")
open(p, "w").write(s)
print("upd765 patch applied")
