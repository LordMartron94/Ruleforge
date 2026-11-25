"""End-to-end regressions for structured datasets, matching and safe coalescing."""
import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile

engine = str(Path(sys.argv[1]).resolve())

def call(directory, *args):
    result = subprocess.run([engine, *args], cwd=directory, text=True, capture_output=True)
    if result.returncode:
        raise AssertionError(result.stderr + result.stdout)
    return result.stdout

with tempfile.TemporaryDirectory(prefix="ruleforge-features-") as tmp:
    root = Path(tmp)
    (root / "entry.rf").write_text('''version 1.0.0;
module entry;
import std.conditions;
import std.rules;
import std.economy;
import std.collections;
colorSet Palette { ink = #112233; }
themeSchema Labels { good; bad; }
theme Theme implements Labels {
    good { onShow { Background Palette.ink; Text #FFFFFF; } onHide { Background #112233; } }
    bad { onShow { Background #00000080; } onHide { Background #00000080; } }
}
schema Row { source string; section string; decision string; labelStyle string; conditions list<record>; }
dataset Rows: Row from "rules";
schema Request { id string; class string; count int; profiles list<record>; }
dataset Requests: Request from "request";
schema Weight { league string; weight float; }
dataset Weights: Weight from "weights";
schema Quote { baseType string; name string; class string; league string; chaosValue float; bucket int; listingCount int; source string; state string; tradedUnits float; }
dataset Quotes: Quote from "quotes";
schema Previous { group string; baseType string; stack int; tier int; candidate int; confirmations int; bucket int; }
dataset State: Previous from "state";
export func Product() -> record {
    for request in Requests { return economy.TierHistory(Quotes, request, Weights, State); }
    return collections.MakeRecord([], []);
}
export func main() { include rules.FromRecords(Rows, Theme); }
''')
    settings = '[context]\n'
    for source in ["rules", "request", "weights", "quotes", "state"]:
        settings += f'\n[datasets."{source}"]\nprovider = "json"\npath = "{source}.json"\n'
    (root / "settings.toml").write_text(settings)
    def condition(name, value, op="==", **extra):
        return dict(name=name, value=value, op=op, **extra)
    def row(name, conditions, decision="show", section="Order"):
        return dict(source=name, section=section, decision=decision,
                    labelStyle="good" if decision == "show" else "bad", conditions=conditions)
    rows = [
        row("pattern", [condition("baseType", ["Tattoo"], "contains")], section="Patterns"),
        row("count", [condition("baseType", ["CountFixture"]), condition("hasExplicitMod", ["fire", "cold"], ">=", count=2)], section="Counts"),
        row("sockets", [condition("baseType", ["SocketFixture"]), condition("socketGroup", ["5GGG"], ">=")], section="Sockets"),
        row("rarities", [condition("baseType", ["RarityFixture"]), condition("rarity", ["Magic", "Rare"])], section="Rarities"),
        row("alpha", [condition("baseType", ["Alpha"])]),
        row("block beta", [condition("baseType", ["beta"])], "hide"),
        row("BETA", [condition("baseType", ["BETA"])]),
        row("gamma", [condition("baseType", ["Gamma"])]),
        row("delta", [condition("baseType", ["Delta"])]),
        row("fallback", [], "hide", "Fallback"),
    ]
    (root / "rules.json").write_text(json.dumps(rows))
    request = dict(id="currency", **{"class": "Currency"}, count=3,
                   profiles=[dict(baseType="A", crafting=10), dict(baseType="B", crafting=5), dict(baseType="C", crafting=1)])
    (root / "request.json").write_text(json.dumps([request]))
    (root / "weights.json").write_text('[{"league":"Standard","weight":1.0}]')
    quotes = []
    for bucket in [100000, 114400, 128800]:
        for name, price in [("A", 1000), ("B", 10), ("C", 0.1)]:
            quotes.append(dict(baseType=name, name=name, **{"class": "Currency"}, league="Standard", chaosValue=price,
                               bucket=bucket, listingCount=0, source="exchange", state="reliable", tradedUnits=100))
    (root / "quotes.json").write_text(json.dumps(quotes))
    (root / "state.json").write_text('[]')
    call(root, "compile", "entry.rf", "--settings", "settings.toml", "-o", "out.filter", "--model", "model.json")
    text = (root / "out.filter").read_text()
    assert 'HasExplicitMod >=2 "fire" "cold"' in text, "modifier counts must attach to their operator"
    assert 'HasExplicitMod >= 2' not in text
    model = json.loads((root / "model.json").read_text())
    assert len(model["blocks"]) < len(rows), "equivalent disjoint rules were not grouped"
    assert "# Patterns\n" in text and "# Sockets\n" in text
    assert "SetBackgroundColor 17 34 51 255" in text, "opaque background lost its alpha"
    for block in model["blocks"]:
        first, last = block["lines"]
        assert text.splitlines()[first - 1] in ["Show", "Hide"], (first, text.splitlines()[first - 1])
    samples = [
        (dict(baseType="Tattoo of the Ramako", **{"class":"Stackable Currency"}), "show"),
        (dict(baseType="CountFixture", hasExplicitMod=["fire and cold"]), "hide"),
        (dict(baseType="CountFixture", hasExplicitMod=["fire", "cold"]), "show"),
        (dict(baseType="SocketFixture", socketGroup=["GGGRB"]), "show"),
        (dict(baseType="SocketFixture", socketGroup=["GGRBB"]), "hide"),
        (dict(baseType="RarityFixture", rarity="Magic"), "show"),
        (dict(baseType="RarityFixture", rarity="Rare"), "show"),
        (dict(baseType="beta"), "hide"),
        (dict(baseType="BETA"), "hide"),
        (dict(baseType="Alpha"), "show"),
        (dict(baseType="Gamma"), "show"),
        (dict(baseType="Delta"), "show"),
    ]
    (root / "items.json").write_text(json.dumps([sample for sample, _ in samples]))
    results = json.loads(call(root, "simulate", "entry.rf", "--settings", "settings.toml", "--items", "items.json"))
    for result, (_, wanted) in zip(results, samples, strict=True):
        assert result["action"] == wanted, (result, wanted)
    def product():
        return json.loads(call(root, "eval", "--import", "entry", "--settings", "settings.toml", "-e", "entry.Product()"))
    initial = product()
    best = next(row for row in initial["state"] if row["baseType"] == "A" and row["stack"] == 1)
    assert best["tier"] == 1
    pending = dict(best, tier=3, candidate=1, confirmations=1)
    (root / "state.json").write_text(json.dumps([pending]))
    held = next(row for row in product()["state"] if row["baseType"] == "A" and row["stack"] == 1)
    assert held["tier"] == 3 and held["confirmations"] == 1, held
    for quote in quotes:
        quote["bucket"] += 14400
    (root / "quotes.json").write_text(json.dumps(quotes))
    promoted = next(row for row in product()["state"] if row["baseType"] == "A" and row["stack"] == 1)
    assert promoted["tier"] == 1 and promoted["confirmations"] == 0, promoted
    # Large snapshots are framed only between records. Quoted commas, doubled
    # quotes and embedded newlines must survive all supported record endings.
    (root / "bulk.rf").write_text('''version 1.0.0;
module bulk;
import std.collections;
schema Row { id int; notes string; values list<int>; }
export dataset Rows: Row from "bulk";
export func Summary() -> record {
    var total = 0;
    var last = "";
    for row in Rows { total = total + row.id; last = row.notes; }
    return collections.MakeRecord(["count", "total", "last"], [collections.Count(Rows), total, last]);
}
''')
    (root / "bulk.toml").write_text('[datasets."bulk"]\nprovider = "csv"\npath = "bulk.csv"\n')
    note = 'quoted, "text"\nand another line'
    for ending in ["\n", "\r\n", "\r"]:
        with (root / "bulk.csv").open("w", newline="") as out:
            writer = csv.writer(out, lineterminator=ending)
            writer.writerow(["id", "notes", "values"])
            for index in range(2500):
                writer.writerow([index, note, json.dumps([index, 2])])
        summary = json.loads(call(root, "eval", "--import", "bulk", "--settings", "bulk.toml", "-e", "bulk.Summary()"))
        assert summary == dict(count=2500, total=3123750, last=note), summary
    # An unknown operator must produce a diagnostic rather than untyped filter text.
    rows[0]["conditions"][0]["op"] = "not-an-operator"
    (root / "rules.json").write_text(json.dumps(rows))
    failed = subprocess.run([engine, "compile", "entry.rf", "--settings", "settings.toml"], cwd=root, capture_output=True)
    assert failed.returncode != 0 and b"RF0524" in failed.stderr
print("PASS: matching, enum lists, opacity, section lines, safe coalescing and bucket confirmations")
