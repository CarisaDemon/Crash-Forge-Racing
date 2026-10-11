from pathlib import Path
import re
root=Path(r"C:\Crash Team Racing\CTR v2.0.0 Source")
text=(root/"game/Vehicle/VehFire.c").read_text(encoding="utf-8")
m=re.search(r"(?ms)^#if defined\(CTR_NATIVE\)\s*\nstatic const struct Driver \*s_landingBoostDriver\[8\];.*?^#endif",text)
if not m:
    raise SystemExit("Unable to find live landing guard block exactly")
p=root/"tests/reserve_qa_20261010/landing_guard_under_test.h"
p.write_text(m.group(0)+"\n",encoding="utf-8")
print("LANDING_GUARD_EXTRACTED_LINES="+str(m.group(0).count("\n")+1))
