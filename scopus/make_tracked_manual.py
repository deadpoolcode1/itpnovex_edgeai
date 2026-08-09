#!/usr/bin/env python3
"""Rebuild the tester manual and emit a tracked-changes copy for review.

The manual is generated, so its diff normally lives in git — which is fine for
us and useless to the people who actually have to approve a change to a test
procedure. They read Word. So this produces a second .docx carrying real Word
revision marks (w:ins / w:del), openable in Word or LibreOffice with
Review > Track Changes > Show Markup, and accept/reject working as usual.

    python3 scopus/make_tracked_manual.py                    # vs git HEAD
    python3 scopus/make_tracked_manual.py --baseline old.docx

Writes Scopus_Tester_Manual.docx (clean, the source of truth) and
Scopus_Tester_Manual_tracked.docx (the same content, marked up against the
baseline). The clean one is what a tester gets; the tracked one is what a
reviewer gets, and it is not meant to be tested from.

The diff is per paragraph and per table row, not per word: a paragraph whose
wording changed shows as its old text struck out followed by the new text
inserted. That is coarser than Word's own compare, and it is deliberate —
these paragraphs are short, and a word-level diff of regenerated prose
produces interleaved fragments nobody can read.
"""
import argparse
import copy
import difflib
import pathlib
import shutil
import subprocess
import sys
import tempfile
import zipfile

from lxml import etree

HERE = pathlib.Path(__file__).resolve().parent
CLEAN = HERE / "Scopus_Tester_Manual.docx"
TRACKED = HERE / "Scopus_Tester_Manual_tracked.docx"
DOCXML = "word/document.xml"

W = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"


def qn(tag):
    return f"{{{W}}}{tag.split(':', 1)[1]}"


class Rev:
    """Hands out the w:id every revision mark needs, and stamps the author."""

    def __init__(self, author, date):
        self.author = author
        self.date = date
        self._n = 0

    def attrs(self):
        self._n += 1
        return {qn("w:id"): str(self._n),
                qn("w:author"): self.author,
                qn("w:date"): self.date}

    def mark(self, kind):
        el = etree.Element(qn(f"w:{kind}"))
        for k, v in self.attrs().items():
            el.set(k, v)
        return el


def text_of(el):
    return "".join(t.text or "" for t in el.iter(qn("w:t")))


def signature(el):
    """What makes two body elements 'the same' for diffing purposes.

    A table is keyed on its header row only, so that adding a row to the
    troubleshooting table reads as one inserted row rather than as the whole
    table being replaced.
    """
    if el.tag == qn("w:tbl"):
        rows = el.findall(qn("w:tr"))
        return "TBL:" + (text_of(rows[0]) if rows else "")
    return "P:" + text_of(el)


def mark_paragraph(p, kind, rev):
    """Mark every run in a paragraph, and the paragraph mark itself."""
    pPr = p.find(qn("w:pPr"))
    if pPr is None:
        pPr = etree.Element(qn("w:pPr"))
        p.insert(0, pPr)
    rPr = pPr.find(qn("w:rPr"))
    if rPr is None:
        rPr = etree.SubElement(pPr, qn("w:rPr"))
    rPr.insert(0, rev.mark(kind))          # ins/del come first inside w:rPr

    for child in list(p):
        if child.tag not in (qn("w:r"), qn("w:hyperlink")):
            continue
        wrapper = rev.mark(kind)
        p.replace(child, wrapper)
        wrapper.append(child)
        if kind == "del":
            # Deleted text lives in w:delText, or Word shows the run empty.
            for t in child.iter(qn("w:t")):
                t.tag = qn("w:delText")
                t.set("{http://www.w3.org/XML/1998/namespace}space", "preserve")


def mark_row(tr, kind, rev):
    trPr = tr.find(qn("w:trPr"))
    if trPr is None:
        trPr = etree.Element(qn("w:trPr"))
        tr.insert(0, trPr)
    trPr.append(rev.mark(kind))
    for p in tr.iter(qn("w:p")):
        mark_paragraph(p, kind, rev)


def mark_element(el, kind, rev):
    if el.tag == qn("w:tbl"):
        for tr in el.findall(qn("w:tr")):
            mark_row(tr, kind, rev)
    elif el.tag == qn("w:tr"):
        mark_row(el, kind, rev)
    elif el.tag == qn("w:p"):
        mark_paragraph(el, kind, rev)
    return el


def diff_table(old_tbl, new_tbl, rev):
    """Row-level diff of two tables, returning the new table marked up."""
    out = copy.deepcopy(new_tbl)
    for tr in out.findall(qn("w:tr")):
        out.remove(tr)

    old_rows = old_tbl.findall(qn("w:tr"))
    new_rows = new_tbl.findall(qn("w:tr"))
    sm = difflib.SequenceMatcher(
        a=[text_of(r) for r in old_rows], b=[text_of(r) for r in new_rows])

    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            for r in new_rows[j1:j2]:
                out.append(copy.deepcopy(r))
            continue
        for r in old_rows[i1:i2]:
            out.append(mark_element(copy.deepcopy(r), "del", rev))
        for r in new_rows[j1:j2]:
            out.append(mark_element(copy.deepcopy(r), "ins", rev))
    return out


def build_tracked(baseline, current, out_path, author, date):
    with zipfile.ZipFile(baseline) as z:
        old_tree = etree.fromstring(z.read(DOCXML))
    with zipfile.ZipFile(current) as z:
        new_tree = etree.fromstring(z.read(DOCXML))

    old_body = old_tree.find(qn("w:body"))
    new_body = new_tree.find(qn("w:body"))
    olds = [e for e in old_body if e.tag != qn("w:sectPr")]
    news = [e for e in new_body if e.tag != qn("w:sectPr")]
    sect = new_body.find(qn("w:sectPr"))

    rev = Rev(author, date)
    merged = []
    sm = difflib.SequenceMatcher(a=[signature(e) for e in olds],
                                 b=[signature(e) for e in news])
    stats = {"ins": 0, "del": 0}

    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            for o, n in zip(olds[i1:i2], news[j1:j2]):
                if o.tag == qn("w:tbl") and n.tag == qn("w:tbl"):
                    merged.append(diff_table(o, n, rev))
                else:
                    merged.append(copy.deepcopy(n))
            continue
        # One table swapped for one table: diff its rows instead of
        # replacing the whole thing, which would be unreadable.
        if (i2 - i1 == 1 and j2 - j1 == 1
                and olds[i1].tag == qn("w:tbl") and news[j1].tag == qn("w:tbl")):
            merged.append(diff_table(olds[i1], news[j1], rev))
            continue
        for o in olds[i1:i2]:
            merged.append(mark_element(copy.deepcopy(o), "del", rev))
            stats["del"] += 1
        for n in news[j1:j2]:
            merged.append(mark_element(copy.deepcopy(n), "ins", rev))
            stats["ins"] += 1

    for e in list(new_body):
        new_body.remove(e)
    for e in merged:
        new_body.append(e)
    if sect is not None:
        new_body.append(sect)

    doc_xml = etree.tostring(new_tree, xml_declaration=True,
                             encoding="UTF-8", standalone=True)

    # Copy the current package through verbatim, swapping only document.xml,
    # so styles / numbering / theme stay exactly as generated.
    tmp = pathlib.Path(tempfile.mkdtemp()) / "out.docx"
    with zipfile.ZipFile(current) as src, \
            zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as dst:
        for item in src.infolist():
            data = doc_xml if item.filename == DOCXML else src.read(item.filename)
            dst.writestr(item, data)
    shutil.move(str(tmp), str(out_path))
    return stats


def git_baseline(path):
    """The committed copy of the manual, extracted to a temp file."""
    rel = subprocess.run(["git", "ls-files", "--full-name", str(path)],
                         capture_output=True, text=True, cwd=path.parent)
    name = rel.stdout.strip()
    if not name:
        return None
    blob = subprocess.run(["git", "show", f"HEAD:{name}"],
                          capture_output=True, cwd=path.parent)
    if blob.returncode != 0 or not blob.stdout:
        return None
    tmp = pathlib.Path(tempfile.mkdtemp()) / "baseline.docx"
    tmp.write_bytes(blob.stdout)
    return tmp


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--baseline", help="docx to diff against "
                                       "(default: the committed manual)")
    ap.add_argument("--author", default="Scopus test procedure",
                    help="name shown against each revision in Word")
    ap.add_argument("--date", default="2026-08-09T00:00:00Z")
    ap.add_argument("--out", default=str(TRACKED))
    ap.add_argument("--no-regen", action="store_true",
                    help="diff the manual as it stands, without rebuilding it")
    args = ap.parse_args()

    baseline = (pathlib.Path(args.baseline) if args.baseline
                else git_baseline(CLEAN))
    if baseline is None or not baseline.exists():
        print("No baseline to compare against. Pass --baseline <old.docx>.",
              file=sys.stderr)
        return 2

    if not args.no_regen:
        r = subprocess.run([sys.executable, str(HERE / "make_tester_manual.py")])
        if r.returncode != 0:
            return r.returncode

    stats = build_tracked(baseline, CLEAN, pathlib.Path(args.out),
                          args.author, args.date)
    print(f"wrote {args.out}")
    print(f"  {stats['ins']} block(s) inserted, {stats['del']} deleted, "
          f"plus table rows")
    print("  open with Review > Track Changes > Show Markup: All Markup")
    return 0


if __name__ == "__main__":
    sys.exit(main())
