# Three-board physical acceptance checklist

Pending manual acceptance. Exact v3 installation, per-boot health/calibration
checks and normal screens pass on all three devices; see `device-results.json`.
PC results are separate evidence.
Use A/B/C for current physical left/middle/right; choose three distinguishable
roles. Begin separated on a nonmetal surface. Do not restart joined boards.

1. **Separated / pairs:** each device can use its own phrase and Play/Pause.
   Join any two horizontally in the same orientation. The right derives its
   phrase from the left; a paused follower shows centered Play and stays silent.
2. **Full chain:** add the third to form A—B—C, trying AB-first and BC-first.
   B follows A and C follows B. Joining/preparation does not start continuous
   sound on a paused device. Try a different physical order as well.
3. **First starts / later entry:** with all paused, press Play only on C. C
   starts promptly; A/B stay paused. Start B and A later: they join the shared
   bar. Repeat with A first and B first. Pause any one: others keep playing.
4. **Musical changes:** while all play, change A's phrase, then B's expression.
   Old sound continues during preparation; affected phrases change together at
   the fixed boundary. Repeat with C paused: C stays silent throughout.
5. **Play during preparation:** pause all, change a phrase and press Play while
   its update is still pending. The current phrase starts promptly. A previously
   locked boundary remains fixed and may shorten this first cycle; unchanged
   continuously playing parts also align there. Then test ordinary first Play
   after the update settles, which starts a full cycle.
6. **Release:** while playing, separate A from B—C. All retain exact current
   phrases and permission; B becomes the leftmost owner and C still follows B.
   Remove B instead: A/C become independent without losing phrases. Repeat
   while paused: no removal or regrouping starts sound.
7. **Deferred recording:** start B's Record while it is independent (or root
   of B—C), then attach A during preparation/recording/reply. B finishes 3/2/1,
   four recording beats and one reply before following A. Existing B—C activity
   remains usable. Test Cancel as well; no extra recording or autoplay occurs.
8. **Settled operation:** leave the chain playing and exercise volume, local
   Pause/Play and a few musical edits. Report any crackle, lost touch, unexpected
   page/reboot or persistent recovery indicator. Retain corresponding boot/log
   evidence; do not infer a sensor fault from a disconnected screen alone.

Record the installed package, identities/physical order, Boot IDs, which steps
were actually exercised and the user's observations. Listening reports establish
experience acceptance within that scope; they do not measure acoustic latency or
clock drift. Sustained RF/memory stress remains a separate unaccepted limit.
