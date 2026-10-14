# AD-49: converts saved Aladhan yearly calendar JSON (ref/*.json) into ref_vectors.csv
# for test_AD_49. Columns: method,school,latadj,lat,lon,year,month,day,tzHours,fajr,
# sunrise,asr (times in minutes since local midnight, -1 if missing).
#
# The JSON is not committed. Fetch it into ref/ first, one file per variant, e.g.
#   curl "https://api.aladhan.com/v1/calendar/2026?latitude=51.2665&longitude=-1.0924&timezonestring=Europe/London&method=3&school=0&latitudeAdjustmentMethod=3" -o ref/m3_s0_l3.json
# (Basingstoke: all methods with school 0 / rule 3, plus MWL and Moonsighting with
# school 1 and rules 0-2; ref/x_tor_m3_s0.json etc. for Toronto and Karachi).
# Then run: python3 -I make_vectors.py
import datetime, glob, json, os, re
from zoneinfo import ZoneInfo

here = os.path.dirname(os.path.abspath(__file__))

def mins(s):
    m = re.match(r"(\d\d):(\d\d)", s)
    return int(m.group(1)) * 60 + int(m.group(2)) if m else -1

with open(os.path.join(here, "ref_vectors.csv"), "w") as out:
    out.write("method,school,latadj,lat,lon,year,month,day,tzHours,fajr,sunrise,asr\n")
    for path in sorted(glob.glob(os.path.join(here, "ref", "*.json"))):
        name = os.path.basename(path)
        m = int(re.search(r"m(\d+)", name).group(1))
        s = int(re.search(r"_s(\d+)", name).group(1))
        l = re.search(r"_l(\d+)", name)
        l = int(l.group(1)) if l else 3
        data = json.load(open(path))["data"]
        for month in sorted(data, key=int):
            for day in data[month]:
                meta, t, g = day["meta"], day["timings"], day["date"]["gregorian"]
                y, mo, d = int(g["year"]), int(g["month"]["number"]), int(g["day"])
                off = datetime.datetime(y, mo, d, 12, tzinfo=ZoneInfo(meta["timezone"])).utcoffset()
                out.write("%d,%d,%d,%.4f,%.4f,%d,%d,%d,%.2f,%d,%d,%d\n" % (
                    m, s, l, meta["latitude"], meta["longitude"], y, mo, d,
                    off.total_seconds() / 3600, mins(t["Fajr"]), mins(t["Sunrise"]), mins(t["Asr"])))
