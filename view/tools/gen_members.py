#!/usr/bin/env python3
"""view/tools/gen_members.py -- synthetic member records for the INTENT-6
membership application (view/glon/membership_app.glon).

    python3 -I view/tools/gen_members.py N OUT.tsv

Deterministic: the same N always gives the same file. Every record is made
up (names from small lists, 555 phone numbers, example.org addresses); none
is a real person or a real association's member.

Format (one record per line, tab-separated, UTF-8):
    glon-members<TAB>1                       header: format and version
    ID NAME EMAIL PHONE VILLAGE POSTAL? POSTAL STATUS YEAR NOTES
POSTAL? is 0 or 1; a newline inside NOTES or POSTAL is byte 0x1F (the
application writes it that way and reads it back).
"""
import sys

SURNAMES = ["Wong", "Chan", "Lee", "Young", "Ng", "Chung", "Lowe", "Kwok", "Ho", "Leung", "Chin", "Lum", "Fong", "Joe", "Kwan", "Mok"]
GIVEN = ["Ka Ming", "Mei Ling", "David", "Grace", "Peter", "Susan", "Wai Kit", "Lily", "Michael", "Helen", "Kevin", "Amy",
         "Sui Fong", "Robert", "Jenny", "Tak Wing", "Alice", "Henry", "Rose", "Stanley"]
VILLAGES = ["Petone", "Lower Hutt", "Wainuiomata", "Karori", "Newtown", "Johnsonville", "Porirua", "Masterton",
            "Levin", "Otaki", "Upper Hutt", "Island Bay"]
STATUSES = ["active", "active", "active", "lapsed", "life", "honorary"]
NOTES = ["", "", "", "Prefers email.", "Committee member 2021-2023.", "Helps at the Moon Festival.",
         "Newsletter by post.\x1fCall before visiting."]

def lcg(seed):
    x = seed
    while True:
        x = (1103515245 * x + 12345) % 2147483648
        yield x

def main():
    n = int(sys.argv[1]); out = sys.argv[2]
    r = lcg(20261126)
    lines = ["glon-members\t1"]
    for i in range(1, n + 1):
        s = SURNAMES[next(r) % len(SURNAMES)]; g = GIVEN[next(r) % len(GIVEN)]
        village = VILLAGES[next(r) % len(VILLAGES)]
        status = STATUSES[next(r) % len(STATUSES)]
        year = 2015 + next(r) % 12
        postal = 1 if next(r) % 4 == 0 else 0
        mid = "M%05d" % i
        email = "%s.%s%d@example.org" % (g.split()[0].lower(), s.lower(), i)
        phone = "021 555 %04d" % (next(r) % 10000)
        paddr = ("PO Box %d\x1f%s" % (100 + next(r) % 900, village)) if postal else ""
        note = NOTES[next(r) % len(NOTES)]
        lines.append("\t".join([mid, "%s %s" % (s, g), email, phone, village, str(postal), paddr, status, str(year), note]))
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")

if __name__ == "__main__":
    main()
